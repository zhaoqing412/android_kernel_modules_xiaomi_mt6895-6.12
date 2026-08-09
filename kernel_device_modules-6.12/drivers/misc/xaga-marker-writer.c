// SPDX-License-Identifier: GPL-2.0
/*
 * xaga boot-stage marker module (XAGR ring writer).
 *
 * Writes an "XAGR" header + circular text ring into the log_store reserved
 * region (0x7ffbf000). Markers survive an AP watchdog reboot in DRAM and are
 * dumped by the xaga-marker reader built into the lineage_xaga kernel
 * (xaga/baselines/kernel/lineage_xaga), which prints them in dmesg on the
 * next boot, so a boot hang can be located even when the kernel dies before
 * the ramoops console is up.
 *
 * Layout matches the reader (lineage_xaga drivers/misc/xaga-marker.c):
 *   u32 magic @0x0000, u32 cursor @0x0004, u32 total @0x0008,
 *   u32 stage @0x1000, text ring @0x2000 (0xE000 bytes).
 *
 * The ring lives in log_store (0x7ffbf000), NOT minirdump (0x48170000):
 * writing minirdump triggers MTK's mrdump machinery and reboots the device
 * immediately. log_store is a non-secure reserved area not managed by
 * mrdump/aee.
 *
 * A module (not built-in): this tree's kernel Image is built from the OPPO
 * common kernel only, so built-in objects here never reach vmlinux. The
 * module must sit FIRST in vendor ramdisk modules.load (pack_vendor does
 * that) so its module-load notifier logs every later module load; a hang in
 * a module probe leaves that module's name as the last ring entry.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/printk.h>

/* Writing the minirdump region (0x48170000) triggers MTK's mrdump machinery
 * and reboots the device immediately (observed on device 2026-08-09; also
 * documented in the xaga-marker reader). Use the log_store region instead:
 * a non-secure reserved area (0x7ffbf000) that is not managed by mrdump/aee,
 * survives the watchdog reboot in DRAM, and is read by the lineage_xaga
 * reader (also NS, no TZASC violation). */
#define XAGA_MRDUMP_PA	0x7ffbf000UL
#define XAGA_MRDUMP_SZ	0x10000UL
#define XAGA_RING_OFF	0x2000U
#define XAGA_RING_SZ	0xE000U
#define XAGA_MAGIC	0x52474158UL	/* "XAGR" */
#define XAGA_MAX_MSG	256

static void __iomem *xaga_mr_base;
static u32 __iomem *xaga_mr_cursor;
static u32 __iomem *xaga_mr_total;
static u32 __iomem *xaga_mr_stage;

static void xaga_marker_map(void)
{
	if (xaga_mr_base)
		return;
	xaga_mr_base = ioremap(XAGA_MRDUMP_PA, XAGA_MRDUMP_SZ);
	if (!xaga_mr_base) {
		pr_info("xaga-marker-writer: ioremap 0x%08lx failed\n",
			XAGA_MRDUMP_PA);
		return;
	}
	xaga_mr_cursor = xaga_mr_base + 0x0004;
	xaga_mr_total = xaga_mr_base + 0x0008;
	xaga_mr_stage = xaga_mr_base + 0x1000;
	/* fresh ring per boot: only the last boot's markers survive */
	writel(XAGA_MAGIC, xaga_mr_base + 0x0000);
	writel(0, xaga_mr_cursor);
	writel(0, xaga_mr_total);
	writel(0, xaga_mr_stage);
	pr_info("xaga-marker-writer: ring armed at 0x%08lx\n", XAGA_MRDUMP_PA);
}

static void xaga_marker_ring_write(const char *buf, int n)
{
	u32 cursor = readl(xaga_mr_cursor);
	u32 total = readl(xaga_mr_total);
	void __iomem *ring = xaga_mr_base + XAGA_RING_OFF;
	int i;

	/* MTK aee/mrdump_mini rewrites the region header on its init (LK
	 * registers 0x48170000 as mrdump_mini_header, magic "addr"/"size"),
	 * so re-assert our XAGR magic on every write - the next module load
	 * after aee restores it. */
	writel(XAGA_MAGIC, xaga_mr_base + 0x0000);
	for (i = 0; i < n; i++)
		writeb(buf[i], ring + ((cursor + i) % XAGA_RING_SZ));
	writel(cursor + n, xaga_mr_cursor);
	writel(total + n, xaga_mr_total);
}

void xaga_marker_put(const char *fmt, ...)
{
	va_list args;
	char buf[XAGA_MAX_MSG];
	int n;

	xaga_marker_map();
	if (!xaga_mr_base)
		return;
	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (n <= 0)
		return;
	xaga_marker_ring_write(buf, n);
}
EXPORT_SYMBOL_GPL(xaga_marker_put);

void xaga_marker_stage(u32 stage)
{
	xaga_marker_map();
	if (!xaga_mr_base)
		return;
	writel(stage, xaga_mr_stage);
	xaga_marker_put("stage=%u\n", stage);
}
EXPORT_SYMBOL_GPL(xaga_marker_stage);

static int xaga_marker_module_nb(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct module *mod = data;

	switch (action) {
	case MODULE_STATE_COMING:
	case MODULE_STATE_LIVE:
		xaga_marker_put("module: %s\n", mod->name);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block xaga_marker_nb = {
	.notifier_call = xaga_marker_module_nb,
};

static int __init xaga_marker_w_init(void)
{
	xaga_marker_stage(1);
	xaga_marker_put("marker module init\n");
	register_module_notifier(&xaga_marker_nb);
	return 0;
}
module_init(xaga_marker_w_init);

static void __exit xaga_marker_w_exit(void)
{
	unregister_module_notifier(&xaga_marker_nb);
}
module_exit(xaga_marker_w_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("xaga boot-stage marker writer (XAGR ring)");
