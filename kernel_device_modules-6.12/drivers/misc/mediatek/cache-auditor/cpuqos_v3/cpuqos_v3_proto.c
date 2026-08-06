// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/sched.h>
#include <linux/percpu-defs.h>
#include <trace/events/task.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <trace/hooks/fpsimd.h>
#include <trace/hooks/cgroup.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include "cpuqos_v3.h"
#include "cpuqos_sys_common.h"
#include "common.h"

static void __iomem *l3ctl_sram_base_addr;
#define SIZE_USAGE_OFS	0x4
#define RESOURCE_USAGE_OFS	0xC
#define CUS_INT_STA 0xCC
#define PER_GRP 0x4
#define GRP_NUM 6
#define MAX_BITMASK 0xFF

#define CREATE_TRACE_POINTS
#include <cpuqos_v3_trace.h>
#undef CREATE_TRACE_POINTS
#undef TRACE_INCLUDE_PATH

#define CREATE_TRACE_POINTS
#include <met_cpuqos_v3_events.h>
#undef CREATE_TRACE_POINTS
#undef TRACE_INCLUDE_PATH

#define CPUQOS_L3CTL_M_OFS      0x84
#define SLC_CPU_DEBUG0_R_OFS    0x88
#define SLC_CPU_DEBUG1_R_OFS    0x8C
#define SLC_SRAM_SIZE           0x100

#define TAG "cpuqos"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jing-Ting Wu");

static int cpuqos_subsys_id = cpu_cgrp_id;
static struct device_node *node;
static int plat_enable;
static int ram_base;
static int q_pid = -1;
static int boot_complete;
/* For ftrace */
static void cpu_qos_handler(struct work_struct *work);
static int cpu_qos_track_enable;
static int cpu_qos_delay;
static struct workqueue_struct *cpuqos_workq;
static DECLARE_DELAYED_WORK(cpu_qos_tracker, cpu_qos_handler);

#ifdef CONFIG_OPLUS_FEATURE_SLC
#include <linux/sched/task.h>
#include <linux/rwlock.h>

enum {
	STALL_CNTR = 0,
	CYCLE_CNTR,
	INST_CNTR,
	CONST_CNTR,
	MAX_CNTR
};

struct cntr {
	u64 cntr[MAX_CNTR];
	u64 prev[MAX_CNTR];
};
static DEFINE_PER_CPU(struct cntr, amu_cntr);
static DEFINE_PER_CPU(int, custpd);
static int amu_ctl;
static int amu_per_cpu_collect;
module_param(amu_per_cpu_collect, int, 0644);

/* tbl mapping to stall ratio pct */
static int cm_qos;
static int cm_qos_tbl[11] = {0, 384, 256, 160, 160, 160, 160, 160, 160, 160, 160}; /* 0 ~ 10 lvs */
module_param_array(cm_qos_tbl, int, NULL, 0664);

static int pd_qos;
static int pd_qos_active;
static int pd_qos_tbl[11] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1}; /* 0 ~ 10 lvs */
module_param_array(pd_qos_tbl, int, NULL, 0664);

/* freq thres, consider require at least target freq */
static int cm_qos_thres[8] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
module_param_array(cm_qos_thres, int, NULL, 0664);

static int amu_sample_period = 1000; /* 1000 ms */
module_param(amu_sample_period, int, 0644);

static int amu_debug;
module_param(amu_debug, int, 0644);

static int custpd_max_bit = 8;
module_param(custpd_max_bit, int, 0644);

static int set_custpd(int mode);

static void amu_ctl_handler(struct work_struct *work);
static DECLARE_DELAYED_WORK(amu_work, amu_ctl_handler);
#endif

/*
 * cgroup path -> PD map
 *
 * Could be made better with cgroupv2: we could have some files that userspace
 * could write into which would give us a {path, PD} pair. We could then
 * translate the path to a cgroup with cgroup_get_from_path(), and save the
 * css->id mapping then.
 */
static char *cpuqos_v3_path_pd_map[] = {
	"/",
	"/foreground",
	"/background",
	"/top-app",
	"/rt",
	"/system",
	"/system-background"
};

#define CSS_MAX 50

/*
 * cgroup css->id -> PD cache
 *
 * Not sure how stable those IDs are supposed to be. If we are supposed to
 * support cgroups being deleted, we may need more hooks to cache that.
 */
static int cpuqos_v3_css_pd_map[CSS_MAX] = { [0 ... 49] = -1 };

/*
 * group number by cpuqos_v3_path_pd_map -> css->id
 *
 */
static int cpuqos_v3_group_css_map[CSS_MAX] = { [0 ... 49] = -1 };

/* The CPUQOS REG in use by a given CPU */
static DEFINE_PER_CPU(int, cpuqos_v3_local_pd);

enum perf_mode {
	AGGRESSIVE,
	BALANCE,
	CONSERVATIVE,
	DISABLE
};

enum pd_grp {
	PD0,
	PD1,
	PD2,
	PD3,
	PD4,
	PD5,
	PD6,
	PD7,
	PD8,
	PD9
};

enum pd_rank {
	GROUP_RANK,
	TASK_RANK
};

enum {
	RAM_BASE_SLC,
	RAM_BASE_SYSRAM,
	RAM_BASE_TCM,
};


static enum perf_mode cpuqos_perf_mode = DISABLE;

int task_curr_clone(const struct task_struct *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

unsigned int get_task_pd(struct task_struct *p)
{
	unsigned int pd;
	struct cpuqos_task_struct *cqts = &((struct mtk_task *)android_task_vendor_data(p))->cpuqos_task;

	pd = READ_ONCE(cqts->pd);

	return pd;
}

unsigned int get_task_rank(struct task_struct *p)
{
	unsigned int rank;
	struct cpuqos_task_struct *cqts = &((struct mtk_task *)android_task_vendor_data(p))->cpuqos_task;

	rank = READ_ONCE(cqts->rank);

	return rank;
}

void (*mtk_cpuqos_set)(int pd);
EXPORT_SYMBOL(mtk_cpuqos_set);

int (*mtk_cpuqos_css_map)(unsigned int id);
EXPORT_SYMBOL(mtk_cpuqos_css_map);
int css_init;

int mtk_cpuqos_map(int id)
{
	int num = 0;
	unsigned int pvid = id;

	if (mtk_cpuqos_css_map)
		num = mtk_cpuqos_css_map(pvid);
	else
		num = PD0;

	return num;
}

void css_map(void)
{
	int i;

	if (!css_init) {
		for (i = 0; i < ARRAY_SIZE(cpuqos_v3_path_pd_map); i++) {
			int css_id = cpuqos_v3_group_css_map[i];

			if (css_id >= 0) {
				WRITE_ONCE(cpuqos_v3_css_pd_map[css_id],
				mtk_cpuqos_map(i));
			}
		}
		css_init = 1;
	}
}

void mtk_register_cpuqos_set(int pd)
{
	if (mtk_cpuqos_set) {
		mtk_cpuqos_set(pd);
	}
}

/* Get the css:pd mapping */
static int cpuqos_v3_map_css_pd(struct cgroup_subsys_state *css)
{
	int pd;

	if (!css)
		goto no_match;

	if ((css->id < 0) || (css->id >= CSS_MAX))
		goto no_match;

	/*
	 * The map is stable post init so no risk of two concurrent tasks
	 * cobbling each other
	 */
	pd = READ_ONCE(cpuqos_v3_css_pd_map[css->id]);
	if (pd >= 0)
		return pd;

no_match:
	/* No match, use sensible default */
	pd = PD3;

	return pd;
}

/*
 * Get the task_struct:pd mapping
 * This is the place to add special logic for a task-specific (rather than
 * cgroup-wide) PD.
 */
static int cpuqos_v3_map_task_pd(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	int pd;
	int rank = get_task_rank(p);

	if (rank == TASK_RANK) {
		/* task rank */
		pd = get_task_pd(p);
		goto out;
	} else {
		/* group rank */
		rcu_read_lock();
		css = task_css(p, cpuqos_subsys_id);
		pd = cpuqos_v3_map_css_pd(css);
		rcu_read_unlock();
		goto out;
	}
out:
	return pd;
}

/*
 * Write the PD to use on the local CPU.
 */
static void cpuqos_v3_write_pd(int pd)
{
	this_cpu_write(cpuqos_v3_local_pd, pd);

	/* Write to e.g. CPUQOS REG here */
	mtk_register_cpuqos_set(this_cpu_read(cpuqos_v3_local_pd));
}

#ifdef CONFIG_OPLUS_FEATURE_SLC
extern int cm_mgr_cm_passive_rescure(int val);
static int pd_tbl[8] = { 10, 10, 10, 10, 11, 11, 11, 12};
module_param_array(pd_tbl, int, NULL, 0664);
static inline int custpd_get_pd(void)
{
	if (pd_qos && pd_qos_active)
		return pd_tbl[smp_processor_id()];
	return this_cpu_read(custpd);
}

static void amu_smp_collect(void __always_unused *info)
{
	int cpu = smp_processor_id();
	per_cpu(amu_cntr, cpu).cntr[STALL_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_MEM_STALL);
	per_cpu(amu_cntr, cpu).cntr[CYCLE_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
	per_cpu(amu_cntr, cpu).cntr[INST_CNTR]  = read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
	per_cpu(amu_cntr, cpu).cntr[CONST_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_CONST_EL0);
}
#endif

/*
 * Sync @p's associated PD with this CPU's register.
 */
static void cpuqos_v3_sync_task(struct task_struct *p)
{
	struct cgroup_subsys_state *css;
	int old_pd = this_cpu_read(cpuqos_v3_local_pd);

#ifdef CONFIG_OPLUS_FEATURE_SLC
	int this_pd = custpd_get_pd();

	/* update stall ratio */
	if (amu_ctl && amu_per_cpu_collect) {
		int cpu = smp_processor_id();
		per_cpu(amu_cntr, cpu).cntr[STALL_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_MEM_STALL);
		per_cpu(amu_cntr, cpu).cntr[CYCLE_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
		per_cpu(amu_cntr, cpu).cntr[INST_CNTR]  = read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
		per_cpu(amu_cntr, cpu).cntr[CONST_CNTR] = read_sysreg_s(SYS_AMEVCNTR0_CONST_EL0);
	}

	/* switch to specific pd */
	if (this_pd) {
		cpuqos_v3_write_pd(this_pd);
		trace_cpuqos_cpu_pd(smp_processor_id(), p->pid,
				-1, old_pd,
				this_pd,
				get_task_rank(p),
				cpuqos_perf_mode);
		return;
	}
#endif

	rcu_read_lock();
	css = task_css(p, cpuqos_subsys_id);
	rcu_read_unlock();

	cpuqos_v3_write_pd(cpuqos_v3_map_task_pd(p));
	trace_cpuqos_cpu_pd(smp_processor_id(), p->pid,
			css->id, old_pd,
			this_cpu_read(cpuqos_v3_local_pd),
			get_task_rank(p),
			cpuqos_perf_mode);
}

/*
 * Same as cpuqos_v3_sync_task(), with a pre-filter for the current task.
 */
static void cpuqos_v3_sync_current(void *task)
{
	int prev_pd;
	int next_pd;

	if (task && task != current)
		return;

	if (trace_CPUQOS_V3_CT_task_enter_enabled() && trace_CPUQOS_V3_CT_task_leave_enabled()) {
		prev_pd = this_cpu_read(cpuqos_v3_local_pd);
		next_pd = cpuqos_v3_map_task_pd(current);

		if ((prev_pd == PD2) && (next_pd == PD3))
			trace_CPUQOS_V3_CT_task_leave(current->pid, current->comm);

		if (next_pd == PD2)
			trace_CPUQOS_V3_CT_task_enter(current->pid, current->comm);
	}

	cpuqos_v3_sync_task(current);
}

/*
 * Same as cpuqos_v3_sync_current(), with an explicit mb for pd mapping changes.
 * Note: technically not required for arm64+GIC since we get explicit barriers
 * when raising and handling an IPI. See:
 * f86c4fbd930f ("irqchip/gic: Ensure ordering between read of INTACK and shared data")
 */
static void cpuqos_v3_sync_current_mb(void *task)
{
	if (task && task != current)
		return;

	/* Pairs with smp_wmb() following cpuqos_v3_cgroup_pd_map[] updates */
	smp_rmb();
	cpuqos_v3_sync_task(current);
}

static void cpuqos_v3_kick_task(struct task_struct *p, int pd)
{
	struct cpuqos_task_struct *cqts = &((struct mtk_task *)android_task_vendor_data(p))->cpuqos_task;

	if (pd >= 0)
		WRITE_ONCE(cqts->pd, pd);

	/*
	 * If @p is no longer on the task_cpu(p) we see here when the smp_call
	 * actually runs, then it had a context switch, so it doesn't need the
	 * explicit update - no need to chase after it.
	 */
	if (task_curr_clone(p))
		smp_call_function_single(task_cpu(p), cpuqos_v3_sync_current, p, 1);
}

/*
 * Set group use which pd
 * group_id: depend on cpuqos_v3_path_pd_map list
 *           0: "/",
 *           1: "/foreground"
 *           2: "/background"
 *	     3: "/top-app"
 *           4: "/rt",
 *           5: "/system",
 *           6: "/system-background"
 * pd: if >= 0, set group is pd;
 *         if < 0, set group is PD3.
 * Return: 0: success,
 *        -1: perf mode is disable / group_id is not exist.
 */
int set_group_pd(int group_id, int pd)
{
	int css_id = -1;
	int old_pd;
	int new_pd;

	if ((group_id >= ARRAY_SIZE(cpuqos_v3_path_pd_map)) || (group_id < 0) ||
		(plat_enable == 0))
		return -1;

	css_id = cpuqos_v3_group_css_map[group_id];
	if ((css_id < 0) || (css_id >= CSS_MAX))
		return -1;

	old_pd = cpuqos_v3_css_pd_map[css_id];

	if (pd >= 0)
		new_pd = pd;
	else
		new_pd = PD3;

	trace_cpuqos_set_group_pd(group_id, css_id, pd,
				old_pd, new_pd,
				cpuqos_perf_mode);

	if (new_pd != old_pd) {
		cpuqos_v3_css_pd_map[css_id] = new_pd;

		/*
		 * Ensure the pd map update is visible before kicking the CPUs.
		 * Pairs with smp_rmb() in cpuqos_v3_sync_current_mb().
		 */
		smp_wmb();
		smp_call_function(cpuqos_v3_sync_current_mb, NULL, 1);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(set_group_pd);

/*
 * Set task use which pd
 * pid: task pid
 * pd: if >= 0, set task is pd(ignore group setting);
 *         if < 0, set task use its group pd.
 * Return: 0: success,
	   -1: perf mode is disable / p is not exist.
 */
int set_task_pd(int pid, int pd)
{
	struct task_struct *p;
	struct cgroup_subsys_state *css;
	int old_pd;
	int new_pd;
	struct cpuqos_task_struct *cqts;

	if ((plat_enable == 0) || (pid <= 0))
		return -1;

	rcu_read_lock();
	p = find_task_by_vpid(pid);

	if (p) {
		get_task_struct(p);
		cqts = &((struct mtk_task *)android_task_vendor_data(p))->cpuqos_task;
	}
	rcu_read_unlock();

	if (!p)
		return -1;

	rcu_read_lock();
	css = task_css(p, cpuqos_subsys_id);
	rcu_read_unlock();

	old_pd = cpuqos_v3_map_task_pd(p); /* before rank change */

	if (pd >= 0) { /* set task is critical task */
		WRITE_ONCE(cqts->rank, TASK_RANK);
		new_pd = pd;
	} else { /* reset to group setting */
		WRITE_ONCE(cqts->rank, GROUP_RANK);
		new_pd = cpuqos_v3_map_task_pd(p); /* after rank change */
	}

	trace_cpuqos_set_task_pd(p->pid, css->id, pd,
				old_pd, new_pd,
				get_task_rank(p), cpuqos_perf_mode);

	if (new_pd != old_pd)
		cpuqos_v3_kick_task(p, new_pd);

	put_task_struct(p);

	return 0;
}
EXPORT_SYMBOL_GPL(set_task_pd);

/*
 * Set group is critical task(CT)/non-critical task(NCT)
 * group_id: depend on cpuqos_v3_path_pd_map list
 *           0: "/",
 *           1: "/foreground"
 *           2: "/background"
 *	     3: "/top-app"
 *           4: "/rt",
 *           5: "/system",
 *           6: "/system-background"
 * set: if true, set group is CT;
 *      if false, set group is NCT.
 * Return: 0: success,
 *        -1: perf mode is disable / group_id is not exist.
 */
int set_ct_group(int group_id, bool set)
{
	int ret = -1;

	if (set)
		ret = set_group_pd(group_id, PD2);
	else
		ret = set_group_pd(group_id, PD3);

	return ret;
}
EXPORT_SYMBOL_GPL(set_ct_group);

/*
 * Set task is critical task(CT) or use its group pd
 * pid: task pid
 * set: if true, set task is CT(ignore group setting);
 *      if false, set task use its group pd.
 * Return: 0: success,
	   -1: perf mode is disable / p is not exist.
 */
int set_ct_task(int pid, bool set)
{
	int ret = -1;

	if (set) /* set task is critical task */
		ret = set_task_pd(pid, PD2);
	else /* reset to group setting */
		ret = set_task_pd(pid, -1);

	return ret;
}
EXPORT_SYMBOL_GPL(set_ct_task);

int set_cpuqos_mode(int mode)
{
	if (mode > DISABLE || mode < AGGRESSIVE || (plat_enable == 0))
		return -1;

	switch (mode) {
	case AGGRESSIVE:
		cpuqos_perf_mode = AGGRESSIVE;
		break;
	case BALANCE:
		cpuqos_perf_mode = BALANCE;
		break;
	case CONSERVATIVE:
		cpuqos_perf_mode = CONSERVATIVE;
		break;
	case DISABLE:
		cpuqos_perf_mode = DISABLE;
		break;
	}

	trace_cpuqos_set_cpuqos_mode(cpuqos_perf_mode);

	if (ram_base != RAM_BASE_SLC)
		iowrite32(cpuqos_perf_mode, l3ctl_sram_base_addr);

	/*
	 * Ensure the pd map update is visible before kicking the CPUs.
	 * Pairs with smp_rmb() in cpuqos_v3_sync_current_mb().
	 */
	smp_wmb();
	smp_call_function(cpuqos_v3_sync_current_mb, NULL, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(set_cpuqos_mode);

/*
 * Set task to use user group
 * pid: task pid
 * group: if > 0, user group 1~6
	  if < 0, set task use its group pd.
 * Return: 0: success, -1: group over / perf mode is disable / p is not exist.
 */
int set_task_user_group(int pid, int group)
{
	int ret = -1;

	if (group > 0 && group <= 6) {
		ret = set_task_pd(pid, group + USER_GROUP_START_IDX);
	} else if (group < 0) {
		ret = set_task_pd(pid, -1);
	} else
		pr_info("cpuqos: set p=%d to group=%d is failed.\n",
			pid, group);

	trace_cpuqos_set_task_user_group(pid, group);

	return ret;
}
EXPORT_SYMBOL_GPL(set_task_user_group);

/*
 * Set cache control to user group
 * group: user group 1~6
 * bitmask: cache portion bitmap
 * Return: 0: success
 * -1: group over
 */
int set_cache_ctl_user_group(int bitmask, int group)
{
	int ret = -1;
	unsigned int offset;

	if (group > 0 && group <= 6 && bitmask <= MAX_BITMASK) {
		offset = (group - 1) * PER_GRP;
		iowrite8(bitmask, l3ctl_sram_base_addr + CUS_INT_STA + offset);
		ret = 0;
	} else
		pr_info("cpuqos: set bitmask=%d to group=%d cache setting is failed.\n",
			bitmask, group);

	trace_cpuqos_set_ccl_user_group(bitmask, group);

	return ret;
}
EXPORT_SYMBOL_GPL(set_cache_ctl_user_group);

static void cpuqos_tracer(void)
{
	unsigned int csize = 0;

	csize = ioread32(l3ctl_sram_base_addr + SIZE_USAGE_OFS);

	if (!boot_complete) {
		pr_info("cpuqos_mode=%d, slices=%u, cache way mode=%u",
			cpuqos_perf_mode,
			(csize & 0xC)>>2, csize & 0x3);
	}

	trace_cpuqos_debug_info(cpuqos_perf_mode,
			(csize & 0xC)>>2, csize & 0x3);
}

static void cpu_qos_handler(struct work_struct *work)
{
	if (cpu_qos_track_enable) {
		cpuqos_tracer();
		queue_delayed_work(cpuqos_workq, &cpu_qos_tracker,
				msecs_to_jiffies(cpu_qos_delay));
	}
}

static ssize_t set_trace_enable(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *ubuf,
		size_t cnt)
{
	int enable = 0;

	if (!kstrtoint(ubuf, 10, &enable)) {
		if (enable) {
			cpu_qos_track_enable = 1;
			queue_delayed_work(cpuqos_workq, &cpu_qos_tracker,
					msecs_to_jiffies(cpu_qos_delay));
		} else
			cpu_qos_track_enable = 0;
	}
	return cnt;
}

static ssize_t show_trace_enable(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	unsigned int len = 0;
	unsigned int max_len = 4096;

	len += snprintf(buf+len, max_len-len,
			"CPU QoS trace enable:%d\n",
			cpu_qos_track_enable);

	return len;
}

static ssize_t set_boot_complete(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *ubuf,
		size_t cnt)
{
	int enable = 0;

	if (!kstrtoint(ubuf, 10, &enable)) {
		if (!boot_complete && (enable == 1)) {
			set_cpuqos_mode(BALANCE);
			boot_complete = 1;
			css_map();
			pr_info("cpuqos working!\n");
		}
	}

	return cnt;
}

static ssize_t show_resource_pct(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	unsigned int len = 0;
	unsigned int max_len = 4096;
	unsigned int temp;
	unsigned int ct_res, nct_res, ct_res_pct, total_res;

	temp = ioread32(l3ctl_sram_base_addr + RESOURCE_USAGE_OFS);

	ct_res = (temp & (0xf << 4)) >> 4;
	nct_res = temp & 0xf;
	total_res = ct_res + nct_res;
	ct_res_pct = (ct_res*100)/total_res;
	len += snprintf(buf+len, max_len-len,
			"Resource percentage CT:NCT = %u/\%u\n",
			ct_res_pct, 100 - ct_res_pct);

	return len;
}

struct kobj_attribute trace_enable_attr =
__ATTR(cpuqos_trace_enable, 0600, show_trace_enable, set_trace_enable);

struct kobj_attribute boot_complete_attr =
__ATTR(cpuqos_boot_complete, 0600, NULL, set_boot_complete);

struct kobj_attribute resource_pct_attr =
__ATTR(resource_percentage, 0600, show_resource_pct, NULL);

static void cpuqos_v3_hook_attach(void __always_unused *data,
			     struct cgroup_subsys *ss, struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct task_struct *p;

	if (ss->id != cpuqos_subsys_id)
		return;

	cgroup_taskset_first(tset, &css);
	cgroup_taskset_for_each(p, css, tset)
		cpuqos_v3_kick_task(p, -1);
}

static void cpuqos_v3_hook_switch(void __always_unused *data,
				struct task_struct *prev, struct task_struct *next)
{
	int prev_pd;
	int next_pd;

	if (!prev || !next)
		return;

	if (trace_CPUQOS_V3_CT_task_enter_enabled() && trace_CPUQOS_V3_CT_task_leave_enabled()) {
		prev_pd = this_cpu_read(cpuqos_v3_local_pd);
		next_pd = cpuqos_v3_map_task_pd(next);

		if (prev && (prev_pd == PD2))
			trace_CPUQOS_V3_CT_task_leave(prev->pid, prev->comm);

		if (next && (next_pd == PD2))
			trace_CPUQOS_V3_CT_task_enter(next->pid, next->comm);
	}

	cpuqos_v3_sync_task(next);
}

static void cpuqos_v3_task_newtask(void __always_unused *data,
				struct task_struct *p, unsigned long clone_flags)
{
	struct cpuqos_task_struct *cqts = &((struct mtk_task *)android_task_vendor_data(p))->cpuqos_task;

	WRITE_ONCE(cqts->pd, PD3); /* pd */
	WRITE_ONCE(cqts->rank, GROUP_RANK); /* rank */
}

/* Check if css' path matches any in cpuqos_v3_path_pd_map and cache that */
static void __init __map_css_pd(struct cgroup_subsys_state *css, char *tmp, int pathlen)
{
	int i;

	cgroup_path(css->cgroup, tmp, pathlen);

	for (i = 0; i < ARRAY_SIZE(cpuqos_v3_path_pd_map); i++) {
		if (!strcmp(cpuqos_v3_path_pd_map[i], tmp) && (css->id >= 0)) {
			WRITE_ONCE(cpuqos_v3_group_css_map[i], css->id);

			/* init group_pd */
			WRITE_ONCE(cpuqos_v3_css_pd_map[css->id], mtk_cpuqos_map(css->id));

			pr_info("group_id=%d, path=%s, cpuqos_v3_path=%s, css_id=%d, group_css=%d, pd_map=%d\n",
				i, tmp, cpuqos_v3_path_pd_map[i], css->id,
				cpuqos_v3_group_css_map[i],
				cpuqos_v3_css_pd_map[css->id]);
		}
	}
}

/* Recursive DFS */
static void __init __map_css_children(struct cgroup_subsys_state *css, char *tmp, int pathlen)
{
	struct cgroup_subsys_state *child;

	list_for_each_entry_rcu(child, &css->children, sibling) {
		if (!child || !child->cgroup || (child->id >= CSS_MAX))
			continue;

		__map_css_pd(child, tmp, pathlen);
		__map_css_children(child, tmp, pathlen);
	}
}

static int __init cpuqos_v3_init_cgroup_pd_map(void)
{
	struct cgroup_subsys_state *css;
	struct cgroup *cgroup;
	char buf[50];
	int ret = 0;

	rcu_read_lock();
	/*
	 * cgroup_get_from_path() would be much cleaner, but that seems to be v2
	 * only. Getting current's cgroup is only a means to get a cgroup handle,
	 * use that to get to the root. Clearly doesn't work if several roots
	 * are involved.
	 */
	cgroup = task_cgroup(current, cpuqos_subsys_id);
	if (IS_ERR(cgroup)) {
		ret = PTR_ERR(cgroup);
		goto out_unlock;
	}

	cgroup = &cgroup->root->cgrp;
	css = rcu_dereference(cgroup->subsys[cpuqos_subsys_id]);
	if (IS_ERR_OR_NULL(css)) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if ((css->id < 0) || (css->id >= CSS_MAX)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	__map_css_pd(css, buf, 50);
	__map_css_children(css, buf, 50);

out_unlock:
	rcu_read_unlock();
	return ret;
}

static int platform_cpuqos_v3_probe(struct platform_device *pdev)
{
	int ret = 0, retval = 0;
	struct platform_device *pdev_temp;
	struct resource *sram_res = NULL;
	int i;

	node = pdev->dev.of_node;

	ret = of_property_read_u32(node,
			"enable", &retval);
	if (!ret)
		plat_enable = retval;
	else
		pr_info("%s unable to get plat_enable\n", __func__);

	pr_info("cpuqos_v3 plat_enable=%d\n", plat_enable);

	ret = of_property_read_u32(node, "ram-base", &ram_base);
	if (ret) {
		pr_info("failed to find ram-base @ %s\n", __func__);
		//set default to sysram, 0 : slc, 1 : l3ctl, 2 : tcm
		ram_base = RAM_BASE_SLC;
	}

	pdev_temp = of_find_device_by_node(node);
	if (!pdev_temp)
		pr_info("failed to find cpuqos_v3 pdev @ %s\n", __func__);
	else
		sram_res = platform_get_resource(pdev_temp, IORESOURCE_MEM, 0);

	if (sram_res) {
		l3ctl_sram_base_addr = ioremap(sram_res->start,
				resource_size(sram_res));
	} else {
		pr_info("%s can't get cpuqos_v3 resource\n", __func__);
	}

	/* Initial cache setting */
	for (i = 1; i <= GRP_NUM; i++)
		set_cache_ctl_user_group(MAX_BITMASK, i);

	return 0;
}

static const struct of_device_id platform_cpuqos_v3_of_match[] = {
	{ .compatible = "mediatek,cpuqos-v3", },
	{},
};

static const struct platform_device_id platform_cpuqos_v3_id_table[] = {
	{ "cpuqos-v3", 0},
	{ },
};

static ssize_t show_l3m_status(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	unsigned int len = 0;
	unsigned int max_len = 4096;
	int css_id = -1;
	struct task_struct *p;
	int i;
	int ct_flag = 0;

	len += snprintf(buf+len, max_len-len,
			"L3 manage perf mode = %d, CT task group = ", cpuqos_perf_mode);

	for (i = 0; i < ARRAY_SIZE(cpuqos_v3_path_pd_map); i++) {
		css_id = cpuqos_v3_group_css_map[i];
		if ((css_id < 0) || (css_id >= CSS_MAX))
			continue;
		switch (cpuqos_v3_css_pd_map[css_id]) {
		case PD2:
		case PD6:
		case PD7:
		case PD8:
		case PD9:
			len += snprintf(buf+len, max_len-len, "%s ", cpuqos_v3_path_pd_map[i]);
		}
	}

	if (q_pid > -1) {
		rcu_read_lock();
		p = find_task_by_vpid(q_pid);
		if (!p) {
			rcu_read_unlock();
			goto out;
		}

		get_task_struct(p);
		rcu_read_unlock();

		switch (cpuqos_v3_map_task_pd(p)) {
		case PD2:
		case PD6:
		case PD7:
		case PD8:
		case PD9:
			ct_flag = 1;
		}

		len += snprintf(buf+len, max_len-len, ", pid %d is %s",
				q_pid, ct_flag?"CT":"NCT");
		put_task_struct(p);
	}

out:

	len += snprintf(buf+len, max_len-len, "\n");
	return len;
}

static ssize_t set_l3m_query_pid(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *ubuf,
		size_t cnt)
{

	int query_pid = 0;

	if (!kstrtoint(ubuf, 10, &query_pid))
		q_pid = query_pid;

	return cnt;
}

#define GROUP_PART_OFS	0xCC
static ssize_t show_group_partition(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	unsigned int len = 0;
	unsigned int max_len = 4096;
	int i;
	unsigned int temp;
	unsigned int gid, bitmap;

	len += snprintf(buf+len, max_len-len,
			"Group partition :\n");
	len += snprintf(buf+len, max_len-len,
			"GID\tbitmap\n");

	for (i = 0;i<6;i++) {
		temp = ioread32(l3ctl_sram_base_addr + GROUP_PART_OFS + i*4);
		gid = i+1;
		bitmap = temp & 0xFF;
		len += snprintf(buf+len, max_len-len,
				"%x\t%x\n", gid, bitmap);
	}

	return len;
}

static ssize_t set_group_partition(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *ubuf,
		size_t cnt)
{
	int ret = 0;
	unsigned int gid, bitmap;

	ret = sscanf(ubuf, "%u,%u", &gid, &bitmap);
	if (ret != 2) {
		pr_info("Format error\n");
		return cnt;
	}

	set_cache_ctl_user_group(bitmap, gid);

	return cnt;
}

struct kobj_attribute show_L3m_status_attr =
__ATTR(l3m_status_info, 0600, show_l3m_status, set_l3m_query_pid);

struct kobj_attribute show_group_partition_attr =
__ATTR(group_partition, 0600, show_group_partition, set_group_partition);

static struct platform_driver mtk_platform_cpuqos_v3_driver = {
	.probe = platform_cpuqos_v3_probe,
	.driver = {
		.name = "cpuqos_v3",
		.owner = THIS_MODULE,
		.of_match_table = platform_cpuqos_v3_of_match,
	},
	.id_table = platform_cpuqos_v3_id_table,
};

void init_cpuqos_v3_platform(void)
{
	platform_driver_register(&mtk_platform_cpuqos_v3_driver);
}

void exit_cpuqos_v3_platform(void)
{
	plat_enable = 0;
	platform_driver_unregister(&mtk_platform_cpuqos_v3_driver);
}

static unsigned long cpuqos_ctl_copy_from_user(void *pvTo,
		const void __user *pvFrom, unsigned long ulBytes)
{
	if (access_ok(pvFrom, ulBytes))
		return __copy_from_user(pvTo, pvFrom, ulBytes);

	return ulBytes;
}

static int cpuqos_ctl_show(struct seq_file *m, void *v)
{
	return 0;
}

static int cpuqos_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, cpuqos_ctl_show, inode->i_private);
}

static long cpuqos_ctl_ioctl_impl(struct file *filp,
		unsigned int cmd, unsigned long arg, void *pKM)
{
	int ret = 0;
	void __user *ubuf_cpuqos = (struct _CPUQOS_V3_PACKAGE *)arg;
	struct _CPUQOS_V3_PACKAGE msgKM_cpuqos = {0};

	switch (cmd) {
	case CPUQOS_V3_SET_CPUQOS_MODE:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_cpuqos_mode(msgKM_cpuqos.mode);
		break;
	case CPUQOS_V3_SET_CT_TASK:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_ct_task(msgKM_cpuqos.pid, msgKM_cpuqos.set_task);
		break;
	case CPUQOS_V3_SET_CT_GROUP:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_ct_group(msgKM_cpuqos.group_id, msgKM_cpuqos.set_group);
		break;
	case CPUQOS_V3_SET_TASK_TO_USER_GROUP:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_task_user_group(msgKM_cpuqos.user_pid, msgKM_cpuqos.set_user_group);
		break;
	case CPUQOS_V3_SET_CCL_TO_USER_GROUP:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_cache_ctl_user_group(msgKM_cpuqos.bitmask, msgKM_cpuqos.set_user_group);
		break;
#ifdef CONFIG_OPLUS_FEATURE_SLC
	case CPUQOS_V3_SET_CUSTPD:
		if (cpuqos_ctl_copy_from_user(&msgKM_cpuqos, ubuf_cpuqos,
				sizeof(struct _CPUQOS_V3_PACKAGE)))
			return -1;

		ret = set_custpd(msgKM_cpuqos.mode);
		break;
#endif
	default:
		pr_info("%s: %s %d: unknown cmd %x\n",
			TAG, __FILE__, __LINE__, cmd);
		ret = -1;
		goto ret_ioctl;
	}

ret_ioctl:
	return ret;
}

static long cpuqos_ctl_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	return cpuqos_ctl_ioctl_impl(filp, cmd, arg, NULL);
}

static const struct proc_ops cpuqos_ctl_Fops = {
	.proc_ioctl = cpuqos_ctl_ioctl,
	.proc_compat_ioctl = cpuqos_ctl_ioctl,
	.proc_open = cpuqos_ctl_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

#ifdef CONFIG_OPLUS_FEATURE_SLC
static int set_custpd(int mode)
{
	if (mode == 1) {
		per_cpu(custpd, 0) = 14; /* user group 5 */
		per_cpu(custpd, 1) = 14;
		per_cpu(custpd, 2) = 14;
		per_cpu(custpd, 3) = 14;
		per_cpu(custpd, 4) = PD2;
		per_cpu(custpd, 5) = PD2;
		per_cpu(custpd, 6) = PD2;
		per_cpu(custpd, 7) = PD2;
	} else {
		per_cpu(custpd, 0) = 0;
		per_cpu(custpd, 1) = 0;
		per_cpu(custpd, 2) = 0;
		per_cpu(custpd, 3) = 0;
		per_cpu(custpd, 4) = 0;
		per_cpu(custpd, 5) = 0;
		per_cpu(custpd, 6) = 0;
		per_cpu(custpd, 7) = 0;
	}
	return 0;
}

static ssize_t custpd_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	unsigned int len;

	len = sprintf(s, "%d,%d,%d,%d,%d,%d,%d,%d\n",
		per_cpu(custpd, 0),
		per_cpu(custpd, 1),
		per_cpu(custpd, 2),
		per_cpu(custpd, 3),
		per_cpu(custpd, 4),
		per_cpu(custpd, 5),
		per_cpu(custpd, 6),
		per_cpu(custpd, 7));

	return simple_read_from_buffer(buf, count, ppos, s, len);
}

static ssize_t custpd_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	int val[8] = {0}, i;

	if (simple_write_to_buffer(s, sizeof(s), ppos, buf,count) <= 0)
		return -EINVAL;

	if (sscanf(s, "%d,%d,%d,%d,%d,%d,%d,%d\n",
		&val[0], &val[1], &val[2], &val[3],
		&val[4], &val[5], &val[6], &val[7]) <= 0) {
		pr_err("cpuqos_v3: error setting argument. argument should be greater than or equal to zero.\n");
		return -EINVAL;
	}

	for (i = 0; i < 8; ++i)
		per_cpu(custpd, i) = val[i];

	pr_err("custpd: %d,%d,%d,%d,%d,%d,%d,%d\n",
		per_cpu(custpd, 0),
		per_cpu(custpd, 1),
		per_cpu(custpd, 2),
		per_cpu(custpd, 3),
		per_cpu(custpd, 4),
		per_cpu(custpd, 5),
		per_cpu(custpd, 6),
		per_cpu(custpd, 7));

	return count;
}

static struct proc_ops custpd_proc_ops = {
	.proc_read = custpd_proc_read,
	.proc_write = custpd_proc_write,
	.proc_lseek = default_llseek,
};

static void amu_cntr_calculate(void)
{
	int i, j, set = 0;
	u64 val[MAX_CNTR][8] = {0}, max_val[MAX_CNTR] = {0};
	u64 delta[MAX_CNTR][8] = {0};
	u64 delta_pct[MAX_CNTR][8] = {0};
	int res[MAX_CNTR][8] = {0};

	if (!amu_per_cpu_collect) {
		/* collect during sampling period */
		smp_call_function(amu_smp_collect, NULL, 1);
	}

	for (i = 0; i < 8; ++i) {
		for (j = 0; j < MAX_CNTR; ++j) {
			struct cntr *ptr;
			ptr = &per_cpu(amu_cntr, i);
			val[j][i] = ptr->cntr[j];
			delta[j][i] = val[j][i] - ptr->prev[j];
			ptr->prev[j] = val[j][i];
			max_val[j] = max(max_val[j], delta[j][i]);
		}
	}

	for (i = 0; i < MAX_CNTR; ++i) {
		if (max_val[i] == 0)
			max_val[i] = ULLONG_MAX;
	}

	/* calculate stall ratio */
	for (i = 0; i < 8; ++i) {
		delta_pct[STALL_CNTR][i] = delta[STALL_CNTR][i] * 100 / max_val[STALL_CNTR];
	}

	for (i = 0; i < 8; ++i) {
		int bit = 0;
		res[STALL_CNTR][i] = ((delta_pct[STALL_CNTR][i] + 10) * custpd_max_bit) / 100;
		for (j = 0; j < res[STALL_CNTR][i]; ++j) {
			bit |= (1 << j);
		}
		res[STALL_CNTR][i] = bit;
	}

	if (pd_qos) {
		/* for cpu 0-3 */
		set = 0;
		for (i = 0; i < 4; ++i)
			set = max(set, res[STALL_CNTR][i]);
		set_cache_ctl_user_group(set, 1);

		/* for cpu 4-6 */
		set = 0;
		for (i = 4; i < 7; ++i)
			set = max(set, res[STALL_CNTR][i]);
		set_cache_ctl_user_group(set, 2);

		/* for cpu 7 */
		set = 0;
		for (i = 7; i < 8; ++i)
			set = max(set, res[STALL_CNTR][i]);
		set_cache_ctl_user_group(set, 3);
	}

	if (cm_qos) {
		u64 max_stall_ratio = 0UL;
		int target_cm = 0;
		for (i = 0; i < 8; ++i) {
			u64 cycle_cntr = delta[CYCLE_CNTR][i];
			u64 const_cntr = delta[CONST_CNTR][i];
			u64 stall_cntr = delta[STALL_CNTR][i];
			if (const_cntr && cycle_cntr && (cycle_cntr * 1000 / const_cntr) >= cm_qos_thres[i])
				max_stall_ratio = max(max_stall_ratio, stall_cntr * 100UL / cycle_cntr);
		}
		max_stall_ratio /= 10UL;
		max_stall_ratio = min(max_stall_ratio, 10UL);
		target_cm = cm_qos_tbl[max_stall_ratio];
		pd_qos_active = pd_qos_tbl[max_stall_ratio];

		if (unlikely(amu_debug)) {
			pr_err("cm_qos: max_stall_ratio %llu, target_cm %d\n", max_stall_ratio, target_cm);
		}

		cm_mgr_cm_passive_rescure(target_cm);
	}

	if (unlikely(amu_debug)) {
		u64 total[MAX_CNTR] = {0};

		pr_err("custpd: %llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
			val[STALL_CNTR][0], val[STALL_CNTR][1], val[STALL_CNTR][2], val[STALL_CNTR][3],
			val[STALL_CNTR][4], val[STALL_CNTR][5], val[STALL_CNTR][6], val[STALL_CNTR][7]
			);
		pr_err("custpd_delta: %llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
			delta[STALL_CNTR][0], delta[STALL_CNTR][1], delta[STALL_CNTR][2], delta[STALL_CNTR][3],
			delta[STALL_CNTR][4], delta[STALL_CNTR][5], delta[STALL_CNTR][6], delta[STALL_CNTR][7]
			);
		pr_err("custpd_delta_pct: %llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
			delta_pct[STALL_CNTR][0], delta_pct[STALL_CNTR][1],
			delta_pct[STALL_CNTR][2], delta_pct[STALL_CNTR][3],
			delta_pct[STALL_CNTR][4], delta_pct[STALL_CNTR][5],
			delta_pct[STALL_CNTR][6], delta_pct[STALL_CNTR][7]
			);
		pr_err("custpd_delta_res: %d,%d,%d,%d,%d,%d,%d,%d\n",
			res[STALL_CNTR][0], res[STALL_CNTR][1], res[STALL_CNTR][2], res[STALL_CNTR][3],
			res[STALL_CNTR][4], res[STALL_CNTR][5], res[STALL_CNTR][6], res[STALL_CNTR][7]
			);

		for (j = 0; j < MAX_CNTR; ++j) {
			pr_err("amu,type%d,delta,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
					j,
					delta[j][0], delta[j][1], delta[j][2], delta[j][3],
					delta[j][4], delta[j][5], delta[j][6], delta[j][7]);
			for (i = 0; i < 8; ++i) {
				total[j] += delta[j][i];
			}
		}
		pr_err("amu,total,%llu,%llu,%llu,%llu\n",
			total[STALL_CNTR], total[CYCLE_CNTR], total[INST_CNTR], total[CONST_CNTR]);
		pr_err("amu,stall_ratio,%llu\n",
			total[STALL_CNTR] * 100 /  (total[CYCLE_CNTR] == 0 ? 1 : total[CYCLE_CNTR]));
		pr_err("amu,ipc,%llu\n",
			total[INST_CNTR] * 100 /  (total[CYCLE_CNTR] == 0 ? 1 : total[CYCLE_CNTR]));
		pr_err("amu,cpi,%llu\n",
			total[CYCLE_CNTR] * 100 /  (total[INST_CNTR] == 0 ? 1 : total[INST_CNTR]));
	}
}

static void amu_ctl_handler(struct work_struct *work)
{
	if (amu_ctl) {
		/* calculate stall ratio */
		amu_cntr_calculate();

		/* update */
		queue_delayed_work(cpuqos_workq, &amu_work,
				msecs_to_jiffies(amu_sample_period));
	}
}

static ssize_t amu_ctl_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	unsigned int len;

	len = sprintf(s, "%d\n", amu_ctl);

	return simple_read_from_buffer(buf, count, ppos, s, len);
}

static ssize_t amu_ctl_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	int val, i;

	if (simple_write_to_buffer(s, sizeof(s), ppos, buf,count) <= 0)
		return -EINVAL;

	if (sscanf(s, "%d\n", &val) <= 0) {;
		pr_err("cpuqos_v3: error setting argument. argument should be greater than or equal to zero.\n");
		return -EINVAL;
	}

	if (amu_ctl == val)
		return count;

	amu_ctl = val;
	if (amu_ctl) {
		queue_delayed_work(cpuqos_workq, &amu_work,
				msecs_to_jiffies(amu_sample_period));
	} else {
		cancel_delayed_work_sync(&amu_work);

		/* reset group resource */
		pd_qos = 0;
		for (i = 1; i <= GRP_NUM; i++)
			set_cache_ctl_user_group(MAX_BITMASK, i);

		/* reset cm_passive_rescure */
		cm_qos = 0;
		cm_mgr_cm_passive_rescure(0);
	}

	return count;
}

static struct proc_ops amu_ctl_proc_ops = {
	.proc_read = amu_ctl_proc_read,
	.proc_write = amu_ctl_proc_write,
	.proc_lseek = default_llseek,
};

static ssize_t pd_qos_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	unsigned int len;

	len = sprintf(s, "%d\n", pd_qos);

	return simple_read_from_buffer(buf, count, ppos, s, len);
}

static ssize_t pd_qos_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	int val, i;

	if (simple_write_to_buffer(s, sizeof(s), ppos, buf,count) <= 0)
		return -EINVAL;

	if (sscanf(s, "%d\n", &val) <= 0) {;
		pr_err("cpuqos_v3: error setting argument. argument should be greater than or equal to zero.\n");
		return -EINVAL;
	}

	if (pd_qos == val)
		return count;

	pd_qos = val;
	if (!val) {
		/* reset group resource */
		for (i = 1; i <= GRP_NUM; i++)
			set_cache_ctl_user_group(MAX_BITMASK, i);
		pd_qos_active = 0;
	}

	return count;
}

static struct proc_ops pd_qos_proc_ops = {
	.proc_read = pd_qos_proc_read,
	.proc_write = pd_qos_proc_write,
	.proc_lseek = default_llseek,
};

static ssize_t cm_qos_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	unsigned int len;

	len = sprintf(s, "%d\n", cm_qos);

	return simple_read_from_buffer(buf, count, ppos, s, len);
}

static ssize_t cm_qos_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char s[32] = {0};
	int val;

	if (simple_write_to_buffer(s, sizeof(s), ppos, buf,count) <= 0)
		return -EINVAL;

	if (sscanf(s, "%d\n", &val) <= 0) {;
		pr_err("cpuqos_v3: error setting argument. argument should be greater than or equal to zero.\n");
		return -EINVAL;
	}

	if (cm_qos == val)
		return count;

	cm_qos = val;
	if (!val) {
		/* reset cm_passive_rescure */
		cm_mgr_cm_passive_rescure(0);
	}

	return count;
}

static struct proc_ops cm_qos_proc_ops = {
	.proc_read = cm_qos_proc_read,
	.proc_write = cm_qos_proc_write,
	.proc_lseek = default_llseek,
};

static inline void cgroup_get_group_name(struct cgroup_subsys_state *css, char *buf)
{
	char *bufend = buf + 128 - 3;

	memset(buf, 0, 32); /* only first 32 bytes reset */
	cgroup_path(css->cgroup, buf, bufend - buf);
	buf[31] = '\0';
}

/* used for dump task and pd info */
static int dump_proc_read(struct seq_file *m, void *v)
{
	struct task_struct *g, *task;

	seq_puts(m, "tg_comm,tg_pid,comm,pid,rank,css,css_name,tpd,gpd\n");

	read_lock(&tasklist_lock);
	for_each_process_thread(g, task) {
		struct cgroup_subsys_state *css;
		int task_pd, group_pd;
		int rank = get_task_rank(task);
		char cgroup_name[128] = {0};

		/* get task pd */
		task_pd = get_task_pd(task);

		/* get group pd */
		rcu_read_lock();
		css = task_css(task, cpuqos_subsys_id);
		group_pd = cpuqos_v3_map_css_pd(css);
		if (css->cgroup)
			cgroup_get_group_name(css, cgroup_name);
		else
			cgroup_name[0] = '\\';
		rcu_read_unlock();

		seq_printf (m, "%s,%d,%s,%d,%d,%d,%s,%d,%d\n",
			g->comm, g->pid,
			task->comm, task->pid,
			rank, css->id, cgroup_name,
			task_pd, group_pd);
	}
	read_unlock(&tasklist_lock);
	return 0;
}

static int dump_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_proc_read, inode);
}

static struct proc_ops dump_proc_ops = {
	.proc_open = dump_proc_open,
	.proc_read = seq_read,
	.proc_lseek = default_llseek,
};

struct amu_counter_data {
    u64 stall_counter;
    u64 cycle_counter;
    u64 instruction_counter;
    u64 const_cycle_counter;
};

/* Per-CPU data storage */
static DEFINE_PER_CPU(struct amu_counter_data, amu_data);

/* Callback to read AMU counters on each CPU */
static void read_amu_counters_on_cpu(void *info)
{
    struct amu_counter_data *data = this_cpu_ptr(&amu_data);

    /* Read AMU counters for this CPU */
    data->stall_counter = read_sysreg_s(SYS_AMEVCNTR0_MEM_STALL);
    data->cycle_counter = read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
    data->instruction_counter = read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
    data->const_cycle_counter = read_sysreg_s(SYS_AMEVCNTR0_CONST_EL0);
}

/* Read AMU counters for all CPUs */
static void read_all_amu_counters(void)
{
    int cpu;

    for_each_online_cpu(cpu) {
        smp_call_function_single(cpu, read_amu_counters_on_cpu, NULL, 1);
    }
}

static int amu_stat_proc_read(struct seq_file *m, void *v)
{
    int cpu;
    struct amu_counter_data *data;

    /* Read counters for all CPUs */
    read_all_amu_counters();

    /* Print counters for each CPU */
    for_each_online_cpu(cpu) {
        data = per_cpu_ptr(&amu_data, cpu);
        seq_printf(m, "%llu,%llu,%llu,%llu,",
			data->stall_counter, data->cycle_counter,
			data->instruction_counter, data->const_cycle_counter);
    }

    seq_puts(m, "\n");
    return 0;
}

static int amu_stat_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, amu_stat_proc_read, inode);
}

static struct proc_ops amu_stat_proc_ops = {
	.proc_open = amu_stat_proc_open,
	.proc_read = seq_read,
	.proc_lseek = default_llseek,
};
#endif

static int __init cpuqos_v3_proto_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pe, *parent;

	/* boot configuration */
	boot_complete = cpu_qos_track_enable = 0;
	cpu_qos_delay = 25;
	cpuqos_workq = NULL;

	init_cpuqos_v3_platform();
	if (!plat_enable) {
		pr_info("cpuqos_v3 is disable at this platform\n");
		goto out;
	}

	ret = init_cpuqos_common_sysfs();
	if (ret) {
		pr_info("init cpuqos sysfs failed\n");
		goto out;
	}

	/* Set DISABLE mode to initial mode */
	cpuqos_perf_mode = DISABLE;

	ret = cpuqos_v3_init_cgroup_pd_map();
	if (ret) {
		pr_info("init cpuqos failed\n");
		goto out;
	}

	ret = register_trace_android_vh_cgroup_attach(cpuqos_v3_hook_attach, NULL);
	if (ret) {
		pr_info("register android_vh_cgroup_attach failed\n");
		goto out;
	}

	ret = register_trace_android_vh_is_fpsimd_save(cpuqos_v3_hook_switch, NULL);
	if (ret) {
		pr_info("register android_vh_is_fpsimd_save failed\n");
		goto out_attach;
	}

	ret = register_trace_task_newtask(cpuqos_v3_task_newtask, NULL);
	if (ret) {
		pr_info("register trace_task_newtask failed\n");
		goto out_attach;
	}

	/*
	 * Ensure the cpuqos mode/pd map update is visible
	 * before kicking the CPUs.
	 */
	pr_info("init cpuqos mode = %d\n", cpuqos_perf_mode);
	ret = set_cpuqos_mode(cpuqos_perf_mode);
	if (ret) {
		pr_info("init set cpuqos mode failed\n");
		goto out_attach;
	}
	cpuqos_tracer();
	cpuqos_workq = create_workqueue("cpuqos_wq");

	/* init cpuqos ioctl */
	pr_info("%s: start to init cpuqos_ioctl driver\n", TAG);
	parent = proc_mkdir("cpuqosmgr", NULL);
	pe = proc_create("cpuqos_ioctl", 0660, parent, &cpuqos_ctl_Fops);
	if (!pe) {
		pr_info("%s: %s failed with %d\n", TAG,
				"Creating file node ", ret);
		ret = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_OPLUS_FEATURE_SLC
	proc_create_data("custpd", 0660, parent, &custpd_proc_ops, NULL);
	proc_create_data("amu_ctl", 0660, parent, &amu_ctl_proc_ops, NULL);
	proc_create_data("pd_qos", 0660, parent, &pd_qos_proc_ops, NULL);
	proc_create_data("cm_qos", 0660, parent, &cm_qos_proc_ops, NULL);
	proc_create_data("dump", 0400, parent, &dump_proc_ops, NULL);
	proc_create_data("amu_stat", 0400, parent, &amu_stat_proc_ops, NULL);
#endif

	return 0;

out_attach:
	unregister_trace_android_vh_cgroup_attach(cpuqos_v3_hook_attach, NULL);
out:
	return ret;
}

static void cpuqos_v3_reset_pd(void __always_unused *info)
{
	cpuqos_v3_write_pd(PD0);
}

static void cpuqos_v3_proto_exit(void)
{
	if (cpuqos_workq)
		destroy_workqueue(cpuqos_workq);

	unregister_trace_android_vh_is_fpsimd_save(cpuqos_v3_hook_switch, NULL);
	unregister_trace_android_vh_cgroup_attach(cpuqos_v3_hook_attach, NULL);
	unregister_trace_task_newtask(cpuqos_v3_task_newtask, NULL);

	smp_call_function(cpuqos_v3_reset_pd, NULL, 1);
	cleanup_cpuqos_common_sysfs();
	exit_cpuqos_v3_platform();
}

module_init(cpuqos_v3_proto_init);
module_exit(cpuqos_v3_proto_exit);
