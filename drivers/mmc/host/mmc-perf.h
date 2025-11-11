/*
 * IO Performance mode with MMC
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
#ifndef _MMC_PERF_H_
#define _MMC_PERF_H_

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
//#include <soc/samsung/exynos_pm.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/sched/clock.h>
#include <linux/interrupt.h>

#include "mmc-perf-v1.h"

enum {
	__TOKEN_FAIL,
	__TOKEN_NUM,
};

static const char *__res_token[__TOKEN_NUM] = {
	"fail to",
};

enum mmc_perf_op {
	MMC_PERF_OP_R = 0,
	MMC_PERF_OP_W,
	MMC_PERF_OP_S,
	MMC_PERF_OP_NONE,
	MMC_PERF_OP_MAX,
};

enum mmc_perf_ctrl {
	MMC_PERF_CTRL_NONE = 0,	/* Not used to run handler */
	MMC_PERF_CTRL_LOCK,
	MMC_PERF_CTRL_RELEASE,
};

enum mmc_perf_entry {
	MMC_PERF_ENTRY_QUEUED = 0,
	MMC_PERF_ENTRY_RESET,
};

/* private */
enum policy_res {
	R_OK = 0,
	R_CTRL,
};

enum {
	__UPDATE_V1 = 0,
	__UPDATE_MAX,
};
#define UPDATE_V1	BIT(__UPDATE_V1)

#if 0
const char *control = {
	DVFS,
	AFFINITY,
	MAX
};
#endif

enum {
	__CTRL_REQ_DVFS = 0,
	__CTRL_REQ_AFFINITY,
	__CTRL_REQ_MAX,
};
#define CTRL_REQ_DVFS		BIT(__CTRL_REQ_DVFS)
#define CTRL_REQ_AFFINITY	BIT(__CTRL_REQ_AFFINITY)

enum ctrl_op {
	CTRL_OP_NONE = 0,
	//CTRL_OP_DWELL,
	CTRL_OP_UP,
	CTRL_OP_DOWN,
};

struct policy{
	u32	con;
};

struct mmc_perf {
	/* stat from device tree */
	u32 update_bits;

	/* interface from request to control */
	u32 ctrl_handle[__CTRL_REQ_MAX];
	spinlock_t lock_handle;

	/* handler */
	struct task_struct *handler;
	struct completion completion;	/* wake-up source */
	enum ctrl_op ctrl_state[__CTRL_REQ_MAX];
	spinlock_t lock_state;

	/* control knobs */
	struct pm_qos_request	pm_qos_int;
	s32			pm_qos_int_value;
	struct pm_qos_request	pm_qos_mif;
	s32			pm_qos_mif_value;
	struct pm_qos_request	pm_qos_cluster0;
	s32			pm_qos_cluster0_value;
	struct pm_qos_request	pm_qos_cluster1;
	s32			pm_qos_cluster1_value;
	struct pm_qos_request	pm_qos_cluster2;
	s32			pm_qos_cluster2_value;
	pid_t cmdq_pid;

	struct mmc_perf_stat_v1 stat_v1;

	//
	struct cmdq_host *cq_host;
	cpumask_t		big_cl_mask;
	cpumask_t		small_cl_mask;
	enum policy_res (*update[__POLICY_MAX])(struct mmc_perf *perf, u32 qd, enum mmc_perf_op op, enum mmc_perf_entry);
	int (*ctrl[__CTRL_REQ_MAX])(struct mmc_perf *perf, enum ctrl_op op);

	/* perf parser */
	u32	perf_update;
	u32	perf_policy_num;
	struct policy *policy;

};

/* EXTERNAL FUNCTIONS */
void mmc_perf_update(void *data, u32 qd, enum mmc_perf_op op, u32 len);
void mmc_perf_reset(void *data);
bool mmc_perf_init(void **data, struct cmdq_host *cq_host, struct device *dev);
void mmc_perf_exit(void *data);

/* from stats */
int mmc_perf_init_v1(struct mmc_perf *perf);
void mmc_perf_exit_v1(struct mmc_perf *perf);

/* to stats */
void mmc_perf_complete(struct mmc_perf *perf);

#endif /* _MMC_PERF_H_ */
