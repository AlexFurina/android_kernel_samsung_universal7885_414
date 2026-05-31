/*
 * IO performance mode with MMC
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Authors:
 *	Kiwoong <kwmad.kim@samsung.com>
 */
#include <linux/of.h>
#include <linux/mmc/host.h>
#include "cmdq_hci.h"
#include "mmc-perf.h"

#define CREATE_TRACE_POINTS
#include <trace/events/mmc_perf.h>

static const char *mmc_perf_v1 = "mmc-perf-v1";

void mmc_perf_complete(struct mmc_perf *perf)
{
	complete(&perf->completion);
}

static int mmc_perf_handler_new(void *data)
{
	struct mmc_perf *perf = (struct mmc_perf *)data;
	unsigned long flags;
	u32 ctrl_handle[__CTRL_REQ_MAX];
	int i;
	int idle;

	init_completion(&perf->completion);

	while (true) {
		if (kthread_should_stop())
			break;

		/* get requests */
		spin_lock_irqsave(&perf->lock_handle, flags);
		for (i = 0; i < __CTRL_REQ_MAX; i++) {
			ctrl_handle[i] = perf->ctrl_handle[i];
			perf->ctrl_handle[i] = CTRL_OP_NONE;
		}
		spin_unlock_irqrestore(&perf->lock_handle, flags);

		/* execute */
		idle = 0;
		for (i = 0; i < __CTRL_REQ_MAX; i++) {
			trace_mmc_perf_lock("active", ctrl_handle[i]);
			if (ctrl_handle[i] == CTRL_OP_NONE) {
				idle++;
			} else if (perf->ctrl[i]) {
				/* TODO: implement */
				perf->ctrl[i](perf, ctrl_handle[i]);
			}
		}
		if (idle == __CTRL_REQ_MAX)
		{
			trace_mmc_perf_lock("sleep", ctrl_handle[0]);
			wait_for_completion(&perf->completion);
			trace_mmc_perf_lock("wake-up", ctrl_handle[0]);
		}
	}

	return 0;
}

/* entry for stats */
void mmc_perf_update(void *data, u32 qd, enum mmc_perf_op op, u32 len)
{
	struct mmc_perf *perf = (struct mmc_perf *)data;
	struct cmdq_host *cq_host = perf->cq_host;
	struct mmc_host *mmc = cq_host->mmc;
	enum policy_res res = R_OK;
	ktime_t time = ktime_get();
	enum policy_res res_t;
	int index;

	/* set once after mmc_init_queue */
	if (perf->cmdq_pid <= 0 && mmc)
		perf->cmdq_pid = mmc->cmdq_pid;

	for (index = 0; index < __UPDATE_MAX; index++) {
		if (!(BIT(index) & perf->update_bits))
			continue;
		res_t = perf->update[index](perf, qd, op, MMC_PERF_ENTRY_QUEUED);
		if (res_t == R_CTRL)
			res = res_t;
	}

	/* wake-up thread */
	if (res == R_CTRL)
		mmc_perf_complete(perf);

	trace_mmc_perf("update", op, qd, ktime_to_us(ktime_sub(ktime_get(), time)), res, len);
}

void mmc_perf_reset(void *data)
{
	struct mmc_perf *perf = (struct mmc_perf *)data;
	enum policy_res res = R_OK;
	enum policy_res res_t;
	int index;

	for (index = 0; index < __UPDATE_MAX; index++) {
		if (!(BIT(index) & perf->update_bits))
			continue;
		res_t = perf->update[index](perf, 0, MMC_PERF_OP_NONE, MMC_PERF_ENTRY_RESET);
		if (res_t == R_CTRL)
			res = res_t;
	}

	/* wake-up thread */
	if (res_t == R_CTRL)
		complete(&perf->completion);
}

static void mmc_perf_parser_dt(struct mmc_perf *perf,
		struct device *dev)
{
	struct device_node *np, *dn;
	u32 temp, count;
	int i;

	np = of_find_node_by_name(dev->of_node, mmc_perf_v1);
	if (!np)
		dev_info(dev, "get node(%s) doesn't exist\n", mmc_perf_v1);

	count = of_get_child_count(np);
	perf->policy = kcalloc(count, sizeof(struct policy), GFP_KERNEL);

	if (!of_property_read_u32(np, "update", &temp))
		perf->perf_update = BIT(temp);
	else
		perf->perf_update = 0;

	if (of_property_read_u32(np, "policy-num", &perf->perf_policy_num))
		perf->perf_policy_num = 0;

	i = 0;
	for_each_child_of_node(np, dn) {
		if(of_property_read_u32(dn, "control", &perf->policy[i].con))
			perf->policy[i].con = 0;
		i++;
	}
}

bool mmc_perf_init(void **data, struct cmdq_host *cq_host, struct device *dev)
{
	struct mmc_perf *perf;
	bool ret = false;

	/* perf and perf->handler is used to check using performance mode */
	*data = devm_kzalloc(dev, sizeof(struct mmc_perf), GFP_KERNEL);
	if (*data == NULL)
		goto out;

	perf = (struct mmc_perf *)(*data);

	mmc_perf_parser_dt(perf, dev);

	spin_lock_init(&perf->lock_handle);
	spin_lock_init(&perf->lock_state);

	perf->handler = kthread_run(mmc_perf_handler_new, perf,
				"mmc_perf_%d", 0);
	if (IS_ERR(perf->handler))
		goto out;

	pm_qos_add_request(&perf->pm_qos_int, PM_QOS_FSYS_THROUGHPUT, 0);
	pm_qos_add_request(&perf->pm_qos_mif, PM_QOS_MEMORY_BANDWIDTH, 0);
	pm_qos_add_request(&perf->pm_qos_cluster0, PM_QOS_CLUSTER0_FREQ_MIN, 0);
	pm_qos_add_request(&perf->pm_qos_cluster1, PM_QOS_CLUSTER1_FREQ_MIN, 0);

	perf->cmdq_pid = -1;
	perf->cq_host = cq_host;

	perf->pm_qos_int_value = 533000;
	perf->pm_qos_mif_value = 1794000;
	perf->pm_qos_cluster0_value = 2184000;
	perf->pm_qos_cluster1_value = 2184000;

	cpumask_clear(&perf->big_cl_mask);
	cpumask_clear(&perf->small_cl_mask);
	cpumask_set_cpu(6, &perf->big_cl_mask);
	cpumask_set_cpu(7, &perf->big_cl_mask);
	cpumask_set_cpu(0, &perf->small_cl_mask);
	cpumask_set_cpu(1, &perf->small_cl_mask);
	cpumask_set_cpu(2, &perf->small_cl_mask);
	cpumask_set_cpu(3, &perf->small_cl_mask);

	/* register updates and ctrls */
	mmc_perf_init_v1(perf);

	/* enable bits */
	perf->update_bits = perf->perf_update;
	ret = true;
out:
	return ret;
}

void mmc_perf_exit(void *data)
{
	struct mmc_perf *perf = (struct mmc_perf *)data;

	mmc_perf_exit_v1(perf);

	if (perf && !IS_ERR(perf->handler)) {
		pm_qos_remove_request(&perf->pm_qos_int);
		pm_qos_remove_request(&perf->pm_qos_mif);
		pm_qos_remove_request(&perf->pm_qos_cluster0);
		pm_qos_remove_request(&perf->pm_qos_cluster1);

		complete(&perf->completion);
		kthread_stop(perf->handler);
	}
}

MODULE_AUTHOR("Kiwoong Kim <kwmad.kim@samsung.com>");
MODULE_DESCRIPTION("Exynos MMC performance booster");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
