// SPDX-License-Identifier: GPL-2.0
/*
 * xaga_oops_log - save kernel panic/oops log to the "oops" block partition
 *
 * Xaga (Redmi Note 11T Pro / POCO X4 GT / Redmi K50i, MT6895) reserves a
 * dedicated "oops" partition (by default /dev/block/sdc81) for crash logs.
 * This module registers a kmsg dumper and writes the dmesg buffer into that
 * partition in a panic-safe way (polling bio writes, no sleeping).
 *
 * The partition is treated as a raw text log area: every panic/oops
 * overwrites it from offset 0 with a 64-byte header followed by the log
 * payload as plain text. To read it back on the device:
 *     dd if=/dev/block/sdc81 bs=64 skip=1 of=/tmp/oops.log   # or skip header
 *     strings /dev/block/sdc81                                # just grep text
 *
 * Module parameters:
 *   blkdev      - oops partition block device path (default /dev/block/sdc81)
 *   max_reason  - highest KMSG_DUMP_* reason to store (default 2 = OOPS)
 *   log_size_kb - max log payload to capture (default 128, must be >= 4)
 */

#include <linux/module.h>
#include <linux/kmsg_dump.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/processor.h>
#include <linux/timekeeping.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/fs.h>

#define XAGA_OOPS_MAGIC		0x41474158	/* "XAGA" little-endian */
#define XAGA_OOPS_VERSION	1
#define XAGA_OOPS_MAX_LOG	(1024 * 1024)	/* 1 MiB hard cap */

/* 4+4+4+4 + 8 + 4 + 4*9 = 64; keep in sync with the struct below */
#define XAGA_OOPS_HEADER_SIZE	64

struct xaga_oops_header {
	__le32 magic;
	__le32 version;
	__le32 reason;
	__le32 len;
	__le64 ts_nsec;
	__le32 seq;			/* boot-time crash counter */
	__le32 reserved[9];
} __packed;

static_assert(sizeof(struct xaga_oops_header) == XAGA_OOPS_HEADER_SIZE);

struct xaga_oops_bio_done {
	struct completion done;
	int err;
};

static char *blkdev = "/dev/block/sdc81";
module_param(blkdev, charp, 0444);
MODULE_PARM_DESC(blkdev, "oops partition block device path");

static int max_reason = KMSG_DUMP_OOPS;
module_param(max_reason, int, 0444);
MODULE_PARM_DESC(max_reason, "highest kmsg dump reason to store (KMSG_DUMP_OOPS=2)");

static int log_size_kb = 128;
module_param(log_size_kb, int, 0444);
MODULE_PARM_DESC(log_size_kb, "max log payload to capture, in KiB (multiple of 4)");

static struct file *oops_file;
static struct block_device *oops_bdev;
static char *oops_buf;			/* header + log payload */
static size_t oops_payload_size;
static struct kmsg_dumper oops_dumper;
static raw_spinlock_t oops_lock = __RAW_SPIN_LOCK_UNLOCKED(oops_lock);
static atomic_t crash_seq = ATOMIC_INIT(0);

static void xaga_oops_bio_endio(struct bio *bio)
{
	struct xaga_oops_bio_done *bd = bio->bi_private;

	bd->err = blk_status_to_errno(bio->bi_status);
	complete(&bd->done);
	bio_put(bio);
}

/*
 * Write @len bytes from @buf at byte offset @pos on the oops partition.
 * Polls for completion instead of sleeping, so it is safe to call from
 * panic/oops context where scheduling is impossible. The poll is bounded:
 * under panic all IRQs are masked and the bio completion IRQ may never
 * fire, so give up after a while instead of hanging the CPU forever
 * (the write has already been submitted and will land in flash anyway).
 */
#define XAGA_OOPS_POLL_MAX	(100000000)	/* ~hundreds of ms busy-poll */

static int xaga_oops_poll_done(struct xaga_oops_bio_done *bd)
{
	unsigned int spins = 0;

	while (!completion_done(&bd->done)) {
		if (++spins >= XAGA_OOPS_POLL_MAX)
			return -ETIMEDOUT;
		cpu_relax();
	}
	return 0;
}

static int xaga_oops_blk_write(loff_t pos, const void *buf, size_t len)
{
	struct xaga_oops_bio_done bd;
	struct bio *bio = NULL;
	size_t written = 0;
	int ret = 0;

	while (written < len) {
		struct page *page = virt_to_page(buf + written);
		unsigned int off = offset_in_page(buf + written);
		size_t chunk = min(len - written, (size_t)(PAGE_SIZE - off));

		if (!bio) {
			init_completion(&bd.done);
			bd.err = 0;
			bio = bio_alloc(oops_bdev, 16, REQ_OP_WRITE,
					GFP_ATOMIC | __GFP_NOWARN);
			if (!bio) {
				ret = -ENOMEM;
				break;
			}
			bio->bi_iter.bi_sector = (pos + written) >> SECTOR_SHIFT;
			bio->bi_private = &bd;
			bio->bi_end_io = xaga_oops_bio_endio;
		}

		if (bio_add_page(bio, page, chunk, off) != chunk) {
			/* bio vector full - flush and retry this chunk.
			 * bio is owned by the IO layer from submit_bio() on;
			 * drop the reference here so the error path below can
			 * never submit it a second time (use-after-free).
			 */
			submit_bio(bio);
			bio = NULL;
			ret = xaga_oops_poll_done(&bd);
			if (!ret && bd.err)
				ret = bd.err;
			if (ret)
				break;
			continue;
		}
		written += chunk;
	}

	if (bio) {
		submit_bio(bio);
		ret = xaga_oops_poll_done(&bd);
		if (!ret && bd.err)
			ret = bd.err;
	}

	return ret;
}

static void xaga_oops_dump(struct kmsg_dumper *dumper,
			   struct kmsg_dump_detail *detail)
{
	struct kmsg_dump_iter iter;
	struct xaga_oops_header *hdr;
	unsigned long flags;
	size_t len = 0;
	int ret;

	if (!oops_bdev || !oops_buf)
		return;

	/* Serialize concurrent dumps; trylock keeps panic paths non-blocking. */
	if (!raw_spin_trylock_irqsave(&oops_lock, flags))
		return;

	kmsg_dump_rewind(&iter);
	if (!kmsg_dump_get_buffer(&iter, true,
				  oops_buf + XAGA_OOPS_HEADER_SIZE,
				  oops_payload_size, &len))
		goto out;

	hdr = (struct xaga_oops_header *)oops_buf;
	memset(hdr, 0, sizeof(*hdr));
	hdr->magic = cpu_to_le32(XAGA_OOPS_MAGIC);
	hdr->version = cpu_to_le32(XAGA_OOPS_VERSION);
	hdr->reason = cpu_to_le32(detail->reason);
	hdr->len = cpu_to_le32(len);
	hdr->ts_nsec = cpu_to_le64(ktime_get_real_fast_ns());
	hdr->seq = cpu_to_le32(atomic_inc_return(&crash_seq));

	ret = xaga_oops_blk_write(0, oops_buf,
				  round_up(XAGA_OOPS_HEADER_SIZE + len,
					   SECTOR_SIZE));
	if (ret)
		pr_err("xaga_oops_log: failed to save crash log: %d\n", ret);
out:
	raw_spin_unlock_irqrestore(&oops_lock, flags);
}

/*
 * The oops partition node may not exist yet when this module loads (early
 * vendor ramdisk). Retry the setup from a delayed work until it appears
 * instead of failing once and silently losing all crash logs.
 */
#define XAGA_OOPS_RETRY_MS	2000
#define XAGA_OOPS_RETRY_MAX	30

static struct delayed_work xaga_oops_retry_work;
static unsigned int xaga_oops_retry_cnt;

static int xaga_oops_setup(void)
{
	struct inode *inode;
	int ret;

	if (oops_dumper.registered)
		return 0;

	oops_file = filp_open(blkdev, O_RDWR | O_DSYNC | O_NOATIME | O_EXCL, 0);
	if (IS_ERR(oops_file)) {
		ret = PTR_ERR(oops_file);
		oops_file = NULL;
		return ret;
	}

	inode = oops_file->f_mapping->host;
	if (!S_ISBLK(inode->i_mode)) {
		pr_err("xaga_oops_log: %s is not a block device\n", blkdev);
		ret = -ENODEV;
		goto put_file;
	}
	oops_bdev = I_BDEV(inode);

	if (bdev_nr_bytes(oops_bdev) <
	    XAGA_OOPS_HEADER_SIZE + oops_payload_size) {
		pr_err("xaga_oops_log: %s too small (%lld bytes, need >= %zu)\n",
		       blkdev, bdev_nr_bytes(oops_bdev),
		       XAGA_OOPS_HEADER_SIZE + oops_payload_size);
		ret = -ENOSPC;
		goto put_file;
	}

	oops_dumper.dump = xaga_oops_dump;
	oops_dumper.max_reason = max_reason;
	ret = kmsg_dump_register(&oops_dumper);
	if (ret)
		goto put_file;

	pr_info("xaga_oops_log: crash log -> %s (payload %zu bytes, max_reason %d)\n",
		blkdev, oops_payload_size, max_reason);
	return 0;

put_file:
	filp_close(oops_file, NULL);
	oops_file = NULL;
	oops_bdev = NULL;
	return ret;
}

static void xaga_oops_retry_work_fn(struct work_struct *work)
{
	if (xaga_oops_setup() == 0)
		return;

	if (++xaga_oops_retry_cnt >= XAGA_OOPS_RETRY_MAX) {
		pr_err("xaga_oops_log: giving up on %s after %u retries\n",
		       blkdev, xaga_oops_retry_cnt);
		return;
	}
	schedule_delayed_work(&xaga_oops_retry_work, XAGA_OOPS_RETRY_MS);
}

static int __init xaga_oops_log_init(void)
{
	size_t log_size = (size_t)log_size_kb * 1024;
	int ret;

	if (log_size_kb < 4 || log_size > XAGA_OOPS_MAX_LOG) {
		pr_err("xaga_oops_log: log_size_kb out of range\n");
		return -EINVAL;
	}

	oops_payload_size = log_size;
	/* extra SECTOR_SIZE-1 slack so the sector-aligned write never
	 * reads past the allocation */
	oops_buf = kzalloc(XAGA_OOPS_HEADER_SIZE + oops_payload_size +
			   SECTOR_SIZE - 1, GFP_KERNEL);
	if (!oops_buf)
		return -ENOMEM;

	INIT_DELAYED_WORK(&xaga_oops_retry_work, xaga_oops_retry_work_fn);
	ret = xaga_oops_setup();
	if (ret) {
		/* partition node may appear later (vendor ramdisk ordering) */
		pr_warn("xaga_oops_log: %s not ready (%d), retrying in background\n",
			blkdev, ret);
		schedule_delayed_work(&xaga_oops_retry_work, XAGA_OOPS_RETRY_MS);
	}
	return 0;
}

static void __exit xaga_oops_log_exit(void)
{
	cancel_delayed_work_sync(&xaga_oops_retry_work);
	kmsg_dump_unregister(&oops_dumper);
	if (oops_file) {
		filp_close(oops_file, NULL);
		oops_file = NULL;
	}
	oops_bdev = NULL;
	kfree(oops_buf);
	oops_buf = NULL;
}

module_init(xaga_oops_log_init);
module_exit(xaga_oops_log_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Save kernel panic/oops log to the xaga oops partition");
