/* linux/arch/arm64/mach-exynos/include/mach/exynos-devfreq.h
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EXYNOS_DEVFREQ_H_
#define __EXYNOS_DEVFREQ_H_

#include <linux/devfreq.h>
#include <linux/pm_qos.h>
#include <linux/clk.h>
#include <soc/samsung/exynos-devfreq-dep.h>
#ifdef CONFIG_EXYNOS_DVFS_MANAGER
#include <soc/samsung/exynos-dm.h>
#endif

#define EXYNOS_DEVFREQ_MODULE_NAME	"exynos-devfreq"
#define VOLT_STEP			25000
#define MAX_NR_CONSTRAINT		DM_TYPE_END
#define DATA_INIT			5
#define SET_CONST			1
#define RELEASE				2

#ifdef CONFIG_ARM_EXYNOS7885_BUS_DEVFREQ
enum exynos_devfreq_type {
	DEVFREQ_MIF = 0,
	DEVFREQ_INT,
	DEVFREQ_DISP,
	DEVFREQ_CAM,
	DEVFREQ_INTCAM,
	DEVFREQ_AUD,
	DEVFREQ_FSYS,
	DEVFREQ_TYPE_END
};
#endif

/* DEVFREQ GOV TYPE */
#define SIMPLE_INTERACTIVE 0

struct exynos_devfreq_opp_table {
	u32 idx;
	u32 freq;
	u32 volt;
};

#ifdef CONFIG_ARM_EXYNOS7885_BUS_DEVFREQ
struct um_exynos;

struct exynos_devfreq_ops {
	/* ops.init(struct exynos_devfreq_data *data) */
	int (*init)(struct exynos_devfreq_data *);
	/* ops.exit(struct exynos_devfreq_data *data) */
	int (*exit)(struct exynos_devfreq_data *);
	/* ops.init_freq_table(struct exynos_devfreq_data *data) */
	int (*init_freq_table)(struct exynos_devfreq_data *);
	/* ops.get_volt_table(struct device *dev, u32 max_state, struct exynos_devfreq_opp_table *opp_list) */
	int (*get_volt_table)(struct device *, u32, struct exynos_devfreq_opp_table *);
	/* ops.ppmu_register(struct exynos_devfreq_data *data) */
	int (*um_register)(struct exynos_devfreq_data *);
	/* ops.ppmu_unregister(struct exynos_devfreq_data *data) */
	int (*um_unregister)(struct exynos_devfreq_data *);
	/* ops.suspend(struct exynos_devfreq_data *data) */
	int (*suspend)(struct exynos_devfreq_data *);
	/* ops.resume(struct exynos_devfreq_data *data */
	int (*resume)(struct exynos_devfreq_data *);
	/* ops.reboot(struct exynos_devfreq_data *data */
	int (*reboot)(struct exynos_devfreq_data *);
	/* ops.get_switch_voltage(struct device *dev, u32 cur_freq, u32 new_freq, u32 cur_volt, u32 new_volt, u32 *switch_volt) */
	int (*get_switch_voltage)(struct device *, u32, u32, u32, u32, u32 *);
	/* ops.set_voltage_prepare(struct exynos_devfreq_data *data) */
	void (*set_voltage_prepare)(struct exynos_devfreq_data *);
	/* ops.set_voltage_post(struct exynos_devfreq_data *data) */
	void (*set_voltage_post)(struct exynos_devfreq_data *);
	/* ops.get_switch_freq(struct device *dev, u32 cur_freq, u32 new_freq, u32 *switch_freq) */
	int (*get_switch_freq)(struct device *, u32, u32, u32 *);
	/* ops.get_freq(struct device *dev, u32 *cur_freq, struct clk *clk) */
	int (*get_freq)(struct device *, u32 *, struct clk *, struct exynos_devfreq_data *);
	/* ops.set_freq(struct device *dev, u32 new_freq, struct clk *clk) */
	int (*set_freq)(struct device *, u32, struct clk *, struct exynos_devfreq_data *);
	/* ops.set_freq_prepare(struct exynos_devfreq_data *data) */
	int (*set_freq_prepare)(struct exynos_devfreq_data *);
	/* ops.set_freq_post(struct exynos_devfreq_data) */
	int (*set_freq_post)(struct exynos_devfreq_data *);
	/* ops.change_to_switch_freq(struct device *dev, void *private_data, struct clk *sw_clk, u32 switch_freq, u32 cur_freq, u32 new_freq) */
	int (*change_to_switch_freq)(struct device *, void *, struct clk *, u32, u32, u32);
	/* ops.restore_from_switch_freq(struct device *dev, void *priave_data, struct clk *clk, u32 cur_freq, u32 new_freq) */
	int (*restore_from_switch_freq)(struct device *, void *, struct clk *, u32, u32);
	/* ops.get_dev_status(struct exynos_devfreq_data *data) */
	int (*get_dev_status)(struct exynos_devfreq_data *);
	/* ops.cl_dvfs_start(struct device *dev) */
	int (*cl_dvfs_start)(struct device *);
	/* ops.cl_dvfs_stop(struct device *dev, u32 target_idx) */
	int (*cl_dvfs_stop)(struct device *, u32);
	/* ops.cmu_dump(struct exynos_devfreq_data *data) */
	int (*cmu_dump)(struct exynos_devfreq_data *);
	/* ops.pm_suspend_prepare(struct exynos_devfreq_data *data) */
	int (*pm_suspend_prepare)(struct exynos_devfreq_data *);
	/* ops.pm_post_suspend(struct exynos_devfreq_data *data) */
	int (*pm_post_suspend)(struct exynos_devfreq_data *);
};

struct um_exynos {
	struct list_head node;
	void __iomem **va_base;
	u32 *pa_base;
	u32 *mask_v;
	u32 *mask_a;
	u32 *channel;
	unsigned int um_count;
	u64 val_ccnt;
	u64 val_pmcnt;
};
#endif

struct exynos_devfreq_data {
	struct device				*dev;
	struct devfreq				*devfreq;
	struct mutex				lock;
	struct clk				*clk;

	bool					devfreq_disabled;

	u32		devfreq_type;

	struct exynos_devfreq_opp_table		*opp_list;

	u32					default_qos;

	u32					max_state;
	struct devfreq_dev_profile		devfreq_profile;

	u32		gov_type;
	const char				*governor_name;
	u32					cal_qos_max;
	void					*governor_data;
#if IS_ENABLED(CONFIG_DEVFREQ_GOV_SIMPLE_INTERACTIVE)
	struct devfreq_simple_interactive_data	simple_interactive_data;
#endif
	u32					dfs_id;
	s32					old_idx;
	s32					new_idx;
	u32					old_freq;
	u32					new_freq;
	u32					min_freq;
	u32					max_freq;
	u32					reboot_freq;
	u32					boot_freq;

	u32					old_volt;
	u32					new_volt;

	u32					pm_qos_class;
	u32					pm_qos_class_max;
	struct pm_qos_request			sys_pm_qos_min;
#ifdef CONFIG_ARM_EXYNOS_DEVFREQ_DEBUG
	struct pm_qos_request			debug_pm_qos_min;
	struct pm_qos_request			debug_pm_qos_max;
#endif
	struct pm_qos_request			default_pm_qos_min;
	struct pm_qos_request			default_pm_qos_max;
	struct pm_qos_request			boot_pm_qos;
	u32					boot_qos_timeout;

	struct notifier_block			reboot_notifier;

	u32					ess_flag;

	s32					target_delay;

#ifdef CONFIG_EXYNOS_DVFS_MANAGER
	u32		dm_type;
	u32		nr_constraint;
	struct exynos_dm_constraint		**constraint;
#endif
	void					*private_data;
#ifdef CONFIG_ARM_EXYNOS7885_BUS_DEVFREQ
	struct exynos_devfreq_ops		ops;
#endif
	bool					use_acpm;
	bool					bts_update;
	bool					update_fvp;
	struct exynos_pm_domain *pm_domain;
};

s32 exynos_devfreq_get_opp_idx(struct exynos_devfreq_opp_table *table,
				unsigned int size, u32 freq);
#if defined(CONFIG_ARM_EXYNOS_DEVFREQ) && defined(CONFIG_EXYNOS_DVFS_MANAGER)
u32 exynos_devfreq_get_dm_type(u32 devfreq_type);
u32 exynos_devfreq_get_devfreq_type(int dm_type);
struct devfreq *find_exynos_devfreq_device(void *devdata);
int find_exynos_devfreq_dm_type(struct device *dev, int *dm_type);
#endif
#endif	/* __EXYNOS_DEVFREQ_H_ */
