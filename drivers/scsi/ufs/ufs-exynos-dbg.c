/*
 * UFS debugging functions for Exynos specific extensions
 *
 * Copyright (C) 2016 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Kiwoong <kwmad.kim@samsung.com>
 */

#include <linux/clk.h>
#include <linux/smc.h>
#include <linux/debug-snapshot.h>

#include "ufshcd.h"
#include "unipro.h"
#include "mphy.h"
#include "ufs-exynos.h"

#include "ufs-dbg-9610.h"

static void exynos_ufs_get_misc(struct ufs_hba *hba)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);
	struct exynos_ufs_clk_info *clki;
	struct list_head *head = &ufs->debug.misc.clk_list_head;

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk))
			clki->freq = clk_get_rate(clki->clk);
	}
//	ufs->debug.misc.isolation = readl(ufs->phy.reg_pmu);
}

static void exynos_ufs_get_sfr(struct ufs_hba *hba,
					struct exynos_ufs_sfr_log* cfg)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);
	int sel_api = 0;

	while(cfg) {
		if (!cfg->name)
			break;

		if (cfg->offset >= LOG_STD_HCI_SFR) {
			/* Select an API to get SFRs */
			sel_api = cfg->offset;
		} else {
			/* Fetch value */
			if (sel_api == LOG_STD_HCI_SFR)
				cfg->val = ufshcd_readl(hba, cfg->offset);
			else if (sel_api == LOG_VS_HCI_SFR)
				cfg->val = hci_readl(ufs, cfg->offset);
#ifdef CONFIG_EXYNOS_SMC_LOGGING
			else if (sel_api == LOG_FMP_SFR)
				cfg->val = exynos_smc(SMC_CMD_FMP_SMU_DUMP, 0, 0, cfg->offset);
#endif
			else if (sel_api == LOG_UNIPRO_SFR)
				cfg->val = unipro_readl(ufs, cfg->offset);
			else if (sel_api == LOG_PMA_SFR)
				cfg->val = phy_pma_readl(ufs, cfg->offset);
			else
				cfg->val = 0xFFFFFFFF;
		}

		/* Next SFR */
		cfg++;
	}
}

static void exynos_ufs_get_attr(struct ufs_hba *hba,
					struct exynos_ufs_attr_log* cfg)
{
	u32 i;
	u32 intr_enable;

	/* Disable and backup interrupts */
	intr_enable = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
	ufshcd_writel(hba, 0, REG_INTERRUPT_ENABLE);

	while(cfg) {
		if (cfg->offset == 0)
			break;

		/* Send DME_GET */
		ufshcd_writel(hba, cfg->offset, REG_UIC_COMMAND_ARG_1);
		ufshcd_writel(hba, UIC_CMD_DME_GET, REG_UIC_COMMAND);

		i = 0;
		while(!(ufshcd_readl(hba, REG_INTERRUPT_STATUS) &
					UIC_COMMAND_COMPL)) {
			if (i++ > 20000) {
				dev_err(hba->dev,
					"Failed to fetch a value of %x",
					cfg->offset);
				goto out;
			}
		}

		/* Clear UIC command completion */
		ufshcd_writel(hba, UIC_COMMAND_COMPL, REG_INTERRUPT_STATUS);

		/* Fetch result and value */
		cfg->res = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2 &
				MASK_UIC_COMMAND_RESULT);
		cfg->val = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3);

		/* Next attribute */
		cfg++;
	}

out:
	/* Restore and enable interrupts */
	ufshcd_writel(hba, intr_enable, REG_INTERRUPT_ENABLE);
}

static void exynos_ufs_dump_misc(struct ufs_hba *hba)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);
	struct exynos_ufs_misc_log* cfg = &ufs->debug.misc;
	struct exynos_ufs_clk_info *clki;
	struct list_head *head = &cfg->clk_list_head;

	dev_err(hba->dev, ": --------------------------------------------------- \n");
	dev_err(hba->dev, ": \t\tMISC DUMP\n");
	dev_err(hba->dev, ": --------------------------------------------------- \n");

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk)) {
			dev_err(hba->dev, "%s: %lu\n",
					clki->name, clki->freq);
		}
	}
	dev_err(hba->dev, "iso: %d\n", ufs->debug.misc.isolation);
}

static void exynos_ufs_dump_sfr(struct ufs_hba *hba,
					struct exynos_ufs_sfr_log* cfg)
{
	dev_err(hba->dev, ": --------------------------------------------------- \n");
	dev_err(hba->dev, ": \t\tREGISTER DUMP\n");
	dev_err(hba->dev, ": --------------------------------------------------- \n");

	while(cfg) {
		if (!cfg->name)
			break;

		/* Dump */
		dev_err(hba->dev, ": %s(0x%04x):\t\t\t\t0x%08x\n",
				cfg->name, cfg->offset, cfg->val);

		/* Next SFR */
		cfg++;
	}
}

static void exynos_ufs_dump_attr(struct ufs_hba *hba,
					struct exynos_ufs_attr_log* cfg)
{
	dev_err(hba->dev, ": --------------------------------------------------- \n");
	dev_err(hba->dev, ": \t\tATTRIBUTE DUMP\n");
	dev_err(hba->dev, ": --------------------------------------------------- \n");

	while(cfg) {
		if (!cfg->offset)
			break;

		/* Dump */
		dev_err(hba->dev, ": 0x%04x:\t\t0x%08x\t\t0x%08x\n",
				cfg->offset, cfg->val, cfg->res);

		/* Next SFR */
		cfg++;
	}
}

/*
 * Functions to be provied externally
 *
 * There are two classes that are to initialize data structures for debug
 * and to define actual behavior.
 */
void exynos_ufs_get_uic_info(struct ufs_hba *hba)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);

	if (!(ufs->misc_flags & EXYNOS_UFS_MISC_TOGGLE_LOG))
		return;

	exynos_ufs_get_sfr(hba, ufs->debug.sfr);
	exynos_ufs_get_attr(hba, ufs->debug.attr);
	exynos_ufs_get_misc(hba);

	ufs->misc_flags &= ~(EXYNOS_UFS_MISC_TOGGLE_LOG);
}

void exynos_ufs_dump_uic_info(struct ufs_hba *hba)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);

	/* secure log */
#ifdef CONFIG_EXYNOS_SMC_LOGGING
	exynos_smc(SMC_CMD_UFS_LOG, 1, 0, hba->secure_log.paddr);
#endif

	exynos_ufs_get_sfr(hba, ufs->debug.sfr);
	exynos_ufs_get_attr(hba, ufs->debug.attr);
	exynos_ufs_get_misc(hba);

	exynos_ufs_dump_sfr(hba, ufs->debug.sfr);
	exynos_ufs_dump_attr(hba, ufs->debug.attr);
	exynos_ufs_dump_misc(hba);
}

void exynos_ufs_show_uic_info(struct ufs_hba *hba)
{
	exynos_ufs_get_sfr(hba, ufs_show_sfr);
	exynos_ufs_get_attr(hba, ufs_show_attr);

	exynos_ufs_dump_sfr(hba, ufs_show_sfr);
	exynos_ufs_dump_attr(hba, ufs_show_attr);
}


int exynos_ufs_init_dbg(struct ufs_hba *hba)
{
	struct exynos_ufs *ufs = to_exynos_ufs(hba);
	struct list_head *head = &hba->clk_list_head;
	struct ufs_clk_info *clki;
	struct exynos_ufs_clk_info *exynos_clki;

	ufs->debug.sfr = ufs_log_sfr;
	ufs->debug.attr = ufs_log_attr;
	INIT_LIST_HEAD(&ufs->debug.misc.clk_list_head);

	if (!head || list_empty(head))
		return 0;

	list_for_each_entry(clki, head, list) {
		exynos_clki = devm_kzalloc(hba->dev, sizeof(*exynos_clki), GFP_KERNEL);
		if (!exynos_clki) {
			return -ENOMEM;
		}
		exynos_clki->clk = clki->clk;
		exynos_clki->name = clki->name;
		exynos_clki->freq = 0;
		list_add_tail(&exynos_clki->list, &ufs->debug.misc.clk_list_head);
	}
#ifdef CONFIG_DEBUG_SNAPSHOT
	hba->secure_log.paddr = dbg_snapshot_get_spare_paddr(0);
	hba->secure_log.vaddr = (u32 *)dbg_snapshot_get_spare_vaddr(0);
#endif

	return 0;
}

static	struct ufs_cmd_info   ufs_cmd_queue;
static	struct ufs_cmd_logging_category ufs_cmd_log;

static void exynos_ufs_putItem_start(struct ufs_cmd_info *cmdQueue, struct ufs_cmd_logging_category *cmdData)
{
	cmdQueue->addr_per_tag[cmdData->tag] = &cmdQueue->data[cmdQueue->last];
	cmdQueue->data[cmdQueue->last].cmd_opcode = cmdData->cmd_opcode;
	cmdQueue->data[cmdQueue->last].tag = cmdData->tag;
	cmdQueue->data[cmdQueue->last].lba = cmdData->lba;
	cmdQueue->data[cmdQueue->last].sct = cmdData->sct;
	cmdQueue->data[cmdQueue->last].retries = cmdData->retries;
	cmdQueue->data[cmdQueue->last].start_time = cmdData->start_time;
	cmdQueue->data[cmdQueue->last].outstanding_reqs = cmdData->outstanding_reqs;
	cmdQueue->last = (cmdQueue->last + 1) % MAX_CMD_LOGS;
}



void exynos_ufs_cmd_log_start(struct ufs_hba *hba, struct scsi_cmnd *cmd)
{
	int cpu = raw_smp_processor_id();

	unsigned long lba = (cmd->cmnd[2] << 24) |
					(cmd->cmnd[3] << 16) |
					(cmd->cmnd[4] << 8) |
					(cmd->cmnd[5] << 0);
	unsigned int sct = (cmd->cmnd[7] << 8) |
					(cmd->cmnd[8] << 0);

	ufs_cmd_log.start_time = cpu_clock(cpu);
	ufs_cmd_log.cmd_opcode = cmd->cmnd[0];
	ufs_cmd_log.tag = cmd->request->tag;
	ufs_cmd_log.outstanding_reqs = hba->outstanding_reqs;

	if(cmd->cmnd[0] != UNMAP)
		ufs_cmd_log.lba = lba;

	ufs_cmd_log.sct = sct;
	ufs_cmd_log.retries = cmd->allowed;

	exynos_ufs_putItem_start(&ufs_cmd_queue, &ufs_cmd_log);
}


void exynos_ufs_cmd_log_end(struct ufs_hba *hba, int tag)
{
	int cpu = raw_smp_processor_id();

	ufs_cmd_queue.addr_per_tag[tag]->end_time = cpu_clock(cpu);
}
