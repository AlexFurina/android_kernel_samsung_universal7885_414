/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Exynos - Support SoC specific Reboot
 * Author: Hosung Kim <hosung0.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/soc/samsung/exynos-soc.h>
#include <soc/samsung/acpm_ipc_ctrl.h>
#include <asm/cputype.h>
#include <soc/samsung/exynos-pmu.h>

extern void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);
static void __iomem *exynos_pmu_base = NULL;

static const char * const mngs_cores[] = {
	"arm,mongoose",
	"arm,cortex-a73",
	NULL,
};

static bool is_mngs_cpu(struct device_node *cn)
{
	const char * const *lc;
	for (lc = mngs_cores; *lc; lc++)
		if (of_device_is_compatible(cn, *lc))
			return true;
	return false;
}

int soc_has_mongoose(void)
{
	struct device_node *cn = NULL;
	u32 mngs_cpu_cnt = 0;

	/* find arm,mongoose compatable in device tree */
	while ((cn = of_find_node_by_type(cn, "cpu"))) {
		if (is_mngs_cpu(cn))
			mngs_cpu_cnt++;
	}
	return mngs_cpu_cnt;
}

/* defines for MNGS reset */
#define PEND_MNGS				(1 << 1)
#define PEND_APOLLO				(1 << 0)
#define DEFAULT_VAL_CPU_RESET_DISABLE		(0xFFFFFFFC)
#define RESET_DISABLE_GPR_CPUPORESET		(1 << 15)
#define RESET_DISABLE_WDT_CPUPORESET		(1 << 12)
#define RESET_DISABLE_CORERESET			(1 << 9)
#define RESET_DISABLE_CPUPORESET		(1 << 8)
#define RESET_DISABLE_WDT_PRESET_DBG		(1 << 25)
#define RESET_DISABLE_PRESET_DBG		(1 << 18)
#define DFD_EDPCSR_DUMP_EN			(1 << 0)
#define RESET_DISABLE_L2RESET			(1 << 16)
#define RESET_DISABLE_WDT_L2RESET		(1 << 31)

#define EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET	(0x041C)
#define EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET	(0x0414)
#define EXYNOS_PMU_ATLAS_CPU0_RESET			(0x200C)
#define EXYNOS_PMU_ATLAS_DBG_RESET			(0x244C)
#define EXYNOS_PMU_ATLAS_NONCPU_RESET			(0x240C)
#define EXYNOS_PMU_SWRESET				(0x0400)
#define EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION	(0x0500)
#define EXYNOS_PMU_PS_HOLD_CONTROL			(0x330C)
#define EXYNOS_PMU_SYSIP_DAT0                  (0x0810)

#define DFD_EDPCSR_DUMP_EN			(1 << 0)
#define DFD_L2RSTDISABLE_APOLLO1_EN		(1 << 13)
#define DFD_DBGL1RSTDISABLE_APOLLO1_EN		(1 << 12)
#define DFD_L2RSTDISABLE_MNGS_EN		(1 << 11)
#define DFD_DBGL1RSTDISABLE_MNGS_EN		(1 << 10)
#define DFD_L2RSTDISABLE_APOLLO_EN		(1 << 9)
#define DFD_DBGL1RSTDISABLE_APOLLO_EN		(1 << 8)
#define DFD_CLEAR_L2RSTDISABLE_MNGS		(1 << 7)
#define DFD_CLEAR_DBGL1RSTDISABLE_MNGS		(1 << 6)
#define DFD_CLEAR_L2RSTDISABLE_APOLLO		(1 << 5)
#define DFD_CLEAR_DBGL1RSTDISABLE_APOLLO	(1 << 4)
#define DFD_CLEAR_L2RSTDISABLE_APOLLO1		(1 << 3)
#define DFD_CLEAR_DBGL1RSTDISABLE_APOLLO1	(1 << 2)

#ifdef CONFIG_SOC_EXYNOS7885
#define DFD_L2RSTDISABLE		(DFD_L2RSTDISABLE_MNGS_EN		\
					| DFD_DBGL1RSTDISABLE_MNGS_EN		\
					| DFD_L2RSTDISABLE_APOLLO_EN		\
					| DFD_DBGL1RSTDISABLE_APOLLO_EN		\
					| DFD_L2RSTDISABLE_APOLLO1_EN		\
					| DFD_DBGL1RSTDISABLE_APOLLO1_EN)
#define DFD_CLEAR_L2RSTDISABLE		(DFD_CLEAR_L2RSTDISABLE_MNGS 		\
					| DFD_CLEAR_DBGL1RSTDISABLE_MNGS	\
					| DFD_CLEAR_L2RSTDISABLE_APOLLO		\
					| DFD_CLEAR_DBGL1RSTDISABLE_APOLLO	\
					| DFD_CLEAR_L2RSTDISABLE_APOLLO1	\
					| DFD_CLEAR_DBGL1RSTDISABLE_APOLLO1)
#else
#define DFD_L2RSTDISABLE		(DFD_L2RSTDISABLE_MNGS_EN		\
					| DFD_DBGL1RSTDISABLE_MNGS_EN		\
					| DFD_L2RSTDISABLE_APOLLO_EN		\
					| DFD_DBGL1RSTDISABLE_APOLLO_EN)
#define DFD_CLEAR_L2RSTDISABLE		(DFD_CLEAR_L2RSTDISABLE_MNGS 		\
					| DFD_CLEAR_DBGL1RSTDISABLE_MNGS	\
					| DFD_CLEAR_L2RSTDISABLE_APOLLO		\
					| DFD_CLEAR_DBGL1RSTDISABLE_APOLLO)
#endif
#define DFD_RESET_PEND			(PEND_MNGS | PEND_APOLLO)
#define DFD_DISABLE_RESET		(RESET_DISABLE_WDT_CPUPORESET	\
					| RESET_DISABLE_CORERESET	\
					| RESET_DISABLE_CPUPORESET)
#define DFD_MNGS_DBG_RESET		(RESET_DISABLE_WDT_PRESET_DBG	\
					| RESET_DISABLE_PRESET_DBG)
#define DFD_NONCPU_RESET		(RESET_DISABLE_L2RESET		\
					| RESET_DISABLE_WDT_L2RESET)

#define PMU_CPU_OFFSET		(0x80)

#define DUMPGPR_EN		(0x1 << 0)
#define DUMPGPR_EN_MASK		(0x1 << 0)
#define CACHE_RESET_EN		(0xF << 4)
#define CACHE_RESET_EN_MASK	(0xF << 4)
#define CLUSTER_RSTCON_EN_MASK	(0xF << 8)

static void dfd_set_dump_gpr(int en)
{
	u32 reg_val;

	if (en & CACHE_RESET_EN_MASK) {
		reg_val = (DUMPGPR_EN_MASK & en) | DFD_L2RSTDISABLE;
		exynos_pmu_write(EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION, reg_val);
	} else {
		exynos_pmu_read(EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION, &reg_val);
		if (reg_val) {
			reg_val = (DUMPGPR_EN_MASK & en) | DFD_CLEAR_L2RSTDISABLE;
			exynos_pmu_write(EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION, reg_val);
		}
	}
}

void mngs_reset_control(int en)
{
	u32 reg_val, val;
	u32 mngs_cpu_cnt = soc_has_mongoose();
	u32 check_dumpGPR;

	if (mngs_cpu_cnt == 0 || !exynos_pmu_base)
		return;

	exynos_pmu_read(EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION, &check_dumpGPR);
	if (!(check_dumpGPR & DFD_EDPCSR_DUMP_EN))
		return;

	if (en) {
		/* reset disable for MNGS */
		exynos_pmu_read(EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET, &reg_val);
		if (reg_val != DEFAULT_VAL_CPU_RESET_DISABLE)
			exynos_pmu_update(EXYNOS_PMU_CPU_RESET_DISABLE_FROM_SOFTRESET,
					DFD_RESET_PEND, 0);

		exynos_pmu_read(EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET, &reg_val);
		if (reg_val != DEFAULT_VAL_CPU_RESET_DISABLE)
			exynos_pmu_update(EXYNOS_PMU_CPU_RESET_DISABLE_FROM_WDTRESET,
					DFD_RESET_PEND, 0);

		for (val = 0; val < mngs_cpu_cnt; val++) {
			exynos_pmu_update(EXYNOS_PMU_ATLAS_CPU0_RESET + (val * PMU_CPU_OFFSET),
					DFD_DISABLE_RESET, DFD_DISABLE_RESET);
		}

		if ((read_cpuid_implementor() == ARM_CPU_IMP_SEC)
				&& (read_cpuid_part_number() == ARM_CPU_PART_MONGOOSE)) {
			exynos_pmu_update(EXYNOS_PMU_ATLAS_DBG_RESET, DFD_MNGS_DBG_RESET,
					DFD_MNGS_DBG_RESET);
		}

		exynos_pmu_update(EXYNOS_PMU_ATLAS_NONCPU_RESET, DFD_NONCPU_RESET,
				DFD_NONCPU_RESET);
	} else {
		/* reset enable for MNGS */
		for (val = 0; val < mngs_cpu_cnt; val++) {
			exynos_pmu_update(EXYNOS_PMU_ATLAS_CPU0_RESET + (val * PMU_CPU_OFFSET),
					DFD_DISABLE_RESET, 0);
		}

		if ((read_cpuid_implementor() == ARM_CPU_IMP_SEC)
				&& (read_cpuid_part_number() == ARM_CPU_PART_MONGOOSE)) {
			exynos_pmu_update(EXYNOS_PMU_ATLAS_DBG_RESET, DFD_MNGS_DBG_RESET, 0);
		}

		exynos_pmu_update(EXYNOS_PMU_ATLAS_NONCPU_RESET, DFD_NONCPU_RESET, 0);
	}
}


#define REBOOT_MODE_NORMAL	0x00
#define REBOOT_MODE_CHARGE	0x0A
/* Reboot into fastbootd mode */
#define REBOOT_MODE_FASTBOOT_BY_USER	0xFB
/* Reboot into fastboot mode */
#define REBOOT_MODE_BOOTLOADER	0xFE
/* Reboot into recovery */
#define REBOOT_MODE_RECOVERY	0xFF


#if !defined(CONFIG_SEC_REBOOT)
#ifdef CONFIG_OF
static void exynos_power_off(void)
{
	int poweroff_try = 0;
	int power_gpio = -1;
	unsigned int keycode = 0;
	struct device_node *np, *pp;

	np = of_find_node_by_path("/gpio_keys");
	if (!np)
		return;

	for_each_child_of_node(np, pp) {
		if (!of_find_property(pp, "gpios", NULL))
			continue;
		of_property_read_u32(pp, "linux,code", &keycode);
		if (keycode == KEY_POWER) {
			pr_info("%s: <%u>\n", __func__,  keycode);
			power_gpio = of_get_gpio(pp, 0);
			break;
		}
	}

	of_node_put(np);

	if (!gpio_is_valid(power_gpio)) {
		pr_err("Couldn't find power key node\n");
		return;
	}

	while (1) {
		/* wait for power button release &&
		 * after exynos_acpm_reboot is called
		 * power on status cannot be read */
		if (gpio_get_value(power_gpio)) {
			exynos_acpm_reboot();

			pr_emerg("%s: Set PS_HOLD Low.\n", __func__);
			writel(readl(exynos_pmu_base + EXYNOS_PMU_PS_HOLD_CONTROL) & 0xFFFFFEFF,
						exynos_pmu_base + EXYNOS_PMU_PS_HOLD_CONTROL);
			++poweroff_try;
			pr_emerg("%s: Should not reach here! (poweroff_try:%d)\n", __func__, poweroff_try);
		} else {
			/* if power button is not released, wait and check TA again */
			 pr_emerg("%s: PowerButton is not released.\n", __func__);
		}
		mdelay(1000);
	}
}
#else
static void exynos_power_off(void)
{
	pr_info("Exynos power off does not support.\n");
}
#endif
#endif

static void exynos_reboot(enum reboot_mode mode, const char *cmd)
{
	u32 soc_id;
	void __iomem *addr;

	if (!exynos_pmu_base)
		return;

	exynos_acpm_reboot();

	addr = exynos_pmu_base + EXYNOS_PMU_SYSIP_DAT0;

	if (cmd) {
		if (!strcmp(cmd, "charge")) {
			__raw_writel(REBOOT_MODE_CHARGE, addr);
		} else if (!strcmp(cmd, "fastboot") || !strcmp(cmd, "fb")) {
			__raw_writel(REBOOT_MODE_FASTBOOT_BY_USER, addr);
		} else if (!strcmp(cmd, "bootloader") || !strcmp(cmd, "bl")) {
			__raw_writel(REBOOT_MODE_BOOTLOADER, addr);
		} else if (!strcmp(cmd, "recovery")) {
			__raw_writel(REBOOT_MODE_RECOVERY, addr);
		}
	}

	/* Check by each SoC */
	soc_id = exynos_soc_info.product_id;
	switch(soc_id) {
	case EXYNOS7872_SOC_ID:
	case EXYNOS7885_SOC_ID:
	case EXYNOS8890_SOC_ID:
	case EXYNOS8895_SOC_ID:
		/* Check reset_sequencer_configuration register */
		if (readl(exynos_pmu_base + EXYNOS_PMU_RESET_SEQUENCER_CONFIGURATION) & DFD_EDPCSR_DUMP_EN) {
			mngs_reset_control(0);
			dfd_set_dump_gpr(0);
		}
		break;
	default:
		break;
	}

	/* Do S/W Reset */
	pr_emerg("%s: Exynos SoC reset right now\n", __func__);
	__raw_writel(0x1, exynos_pmu_base + EXYNOS_PMU_SWRESET);
}

static int __init exynos_reboot_setup(struct device_node *np)
{
	int err = 0;
	u32 id;

	if (!of_property_read_u32(np, "pmu_base", &id)) {
		exynos_pmu_base = ioremap(id, SZ_16K);
		if (!exynos_pmu_base) {
			pr_err("%s: failed to map to exynos-pmu-base address 0x%x\n",
				__func__, id);
			err = -ENOMEM;
		}
	}

	of_node_put(np);
	mngs_reset_control(1);
	return err;
}

static const struct of_device_id reboot_of_match[] __initconst = {
	{ .compatible = "exynos,reboot", .data = exynos_reboot_setup},
	{},
};

typedef int (*reboot_initcall_t)(const struct device_node *);
static int __init exynos_reboot_init(void)
{
	struct device_node *np;
	const struct of_device_id *matched_np;
	reboot_initcall_t init_fn;

	np = of_find_matching_node_and_match(NULL, reboot_of_match, &matched_np);
	if (!np)
		return -ENODEV;

	arm_pm_restart = exynos_reboot;
#if !defined(CONFIG_SEC_REBOOT)
	pm_power_off = exynos_power_off;
#endif
	init_fn = (reboot_initcall_t)matched_np->data;

	return init_fn(np);
}
subsys_initcall(exynos_reboot_init);
