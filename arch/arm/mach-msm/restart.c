/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/mfd/pmic8058.h>
#include <linux/mfd/pmic8901.h>
#include <linux/mfd/pm8xxx/misc.h>
#include <linux/sched.h>
#include <linux/console.h>

#include <asm/mach-types.h>

#include <mach/msm_iomap.h>
#include <mach/restart.h>
#include <mach/socinfo.h>
#include <mach/irqs.h>
#include <mach/scm.h>
#include <mach/board_htc.h>
#include <mach/mdm.h>
#include <mach/msm_watchdog.h>
#include "timer.h"
#include "pm.h"
#include "smd_private.h"
#if defined(CONFIG_MSM_RMT_STORAGE_CLIENT)
#include <linux/rmt_storage_client.h>
#endif

#define WDT0_RST	0x38
#define WDT0_EN		0x40
#define WDT0_BARK_TIME	0x4C
#define WDT0_BITE_TIME	0x5C

#define PSHOLD_CTL_SU (MSM_TLMM_BASE + 0x820)

#define RESTART_REASON_ADDR 0xF00
#define DLOAD_MODE_ADDR     0x0

#define MSM_REBOOT_REASON_BASE	(MSM_IMEM_BASE + RESTART_REASON_ADDR)
#define SZ_DIAG_ERR_MSG 0xC8

#define SCM_IO_DISABLE_PMIC_ARBITER	1

static int restart_mode;

struct htc_reboot_params {
	unsigned reboot_reason;
	unsigned radio_flag;
	char reserved[256 - SZ_DIAG_ERR_MSG - 8];
	char msg[SZ_DIAG_ERR_MSG];
};

static struct htc_reboot_params *reboot_params;

int pmic_reset_irq;
static void __iomem *msm_tmr0_base;

#ifdef CONFIG_MSM_DLOAD_MODE
static int in_panic;
static void *dload_mode_addr;

/* Download mode master kill-switch */
static int dload_set(const char *val, struct kernel_param *kp);
void set_ramdump_reason(const char *msg);
static int download_mode = 1;
module_param_call(download_mode, dload_set, param_get_int,
			&download_mode, 0644);


int check_in_panic(void)
{
	return in_panic;
}

static int panic_prep_restart(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	/* Prepare a buffer whose size is equal to htc_reboot_params->msg */
	char kernel_panic_msg[SZ_DIAG_ERR_MSG] = "Kernel Panic";
	in_panic = 1;

	/* ptr is a buffer declared in panic function. It's never be NULL.
	   Reserve one space for trailing zero.
	*/
	if (ptr)
		snprintf(kernel_panic_msg, SZ_DIAG_ERR_MSG-1, "KP: %s", (char *)ptr);
	set_ramdump_reason(kernel_panic_msg);

	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_prep_restart,
};

static void set_dload_mode(int on)
{
/* Disable QCT dload mode as HTC has our own dload mechanism. */
#if 0
	if (dload_mode_addr) {
		__raw_writel(on ? 0xE47B337D : 0, dload_mode_addr);
		__raw_writel(on ? 0xCE14091A : 0,
		       dload_mode_addr + sizeof(unsigned int));
		mb();
	}
#endif
}

static int dload_set(const char *val, struct kernel_param *kp)
{
	int ret;
	int old_val = download_mode;

	ret = param_set_int(val, kp);

	if (ret)
		return ret;

	/* If download_mode is not zero or one, ignore. */
	if (download_mode >> 1) {
		download_mode = old_val;
		return -EINVAL;
	}

	set_dload_mode(download_mode);

	return 0;
}
#else
#define set_dload_mode(x) do {} while (0)
#endif

void msm_set_restart_mode(int mode)
{
	restart_mode = mode;
}
EXPORT_SYMBOL(msm_set_restart_mode);
static atomic_t restart_counter = ATOMIC_INIT(0);
static int modem_cache_flush_done;

inline void notify_modem_cache_flush_done(void)
{
	modem_cache_flush_done = 1;
}

static inline unsigned get_restart_reason(void)
{
	return reboot_params->reboot_reason;
}
/*
   This function should not be called outside
   to ensure that others do not change restart reason.
   Use mode & cmd to set reason & msg in arch_reset().
*/
static inline void set_restart_reason(unsigned int reason)
{
	reboot_params->reboot_reason = reason;
}

/*
   This function should not be called outsite
   to ensure that others do no change restart reason.
   Use mode & cmd to set reason & msg in arch_reset().
*/
static inline void set_restart_msg(const char *msg)
{
	strncpy(reboot_params->msg, msg, sizeof(reboot_params->msg)-1);
}

static bool console_flushed;

static void msm_pm_flush_console(void)
{
	if (console_flushed)
		return;
	console_flushed = true;

	printk("\n");
	printk(KERN_EMERG "Restarting %s\n", linux_banner);
	if (!console_trylock()) {
		console_unlock();
		return;
	}

	mdelay(50);

	local_irq_disable();
	if (console_trylock())
		printk(KERN_EMERG "restart: Console was locked! Busting\n");
	else
		printk(KERN_EMERG "restart: Console was locked!\n");
	console_unlock();
}

/* This function expose others to restart message for entering ramdump mode. */
void set_ramdump_reason(const char *msg)
{
	/* only allow write msg before entering arch_rest */
	if (atomic_read(&restart_counter) != 0)
		return;

	set_restart_reason(RESTART_REASON_RAMDUMP);
	set_restart_msg(msg? msg: "");
}

void notify_modem_efs_sync(unsigned timeout)
{
#if defined(CONFIG_ARCH_MSM8960)
	smsm_change_state(SMSM_APPS_STATE, SMSM_APPS_REBOOT, SMSM_APPS_REBOOT);
	printk(KERN_INFO "%s: wait for modem efs_sync\n", __func__);
	while (timeout > 0 && !(smsm_get_state(SMSM_MODEM_STATE) & SMSM_SYSTEM_PWRDWN_USR)) {
		/* Kick watchdog.
		   Do not assume efs sync will be executed.
		   Assume watchdog timeout is default 4 seconds. */
		writel(1, msm_tmr0_base + WDT0_RST);
		mdelay(1000);
		timeout--;
	}
	if (timeout <= 0)
		printk(KERN_NOTICE "%s: modem efs_sync timeout.\n", __func__);
#else
#if defined(CONFIG_MSM_RMT_STORAGE_CLIENT)

	printk(KERN_INFO "from %s\r\n", __func__);
	/* efs sync timeout is 10 seconds.
	   set watchdog to 12 seconds. */
	writel(1, msm_tmr0_base + WDT0_RST);
	writel(0, msm_tmr0_base + WDT0_EN);
	writel(32768 * 12, msm_tmr0_base + WDT0_BARK_TIME);
	writel(32768 * 13, msm_tmr0_base + WDT0_BITE_TIME);
	writel(3, msm_tmr0_base + WDT0_EN);
	dsb();
	set_WDT_EN_footprint(3);
	wait_rmt_final_call_back(timeout);
	/* Rest watchdog timer to make sure we have enough time to power off */
	writel(1, msm_tmr0_base + WDT0_RST);
	printk(KERN_INFO "back %s\r\n", __func__);

#endif /* CONFIG_MSM_RMT_STORAGE_CLIENT */
#endif /* CONFIG_ARCH_MSM8960 */
}

static void __msm_power_off(int lower_pshold)
{
#if defined(CONFIG_MSM_RMT_STORAGE_CLIENT)
	set_restart_reason(RESTART_REASON_POWEROFF);

	/* final efs_sync */
	notify_modem_efs_sync(10);
#endif
	printk(KERN_CRIT "Powering off the SoC\n");
#ifdef CONFIG_MSM_DLOAD_MODE
	set_dload_mode(0);
#endif
	pm8xxx_reset_pwr_off(0);

	if (cpu_is_msm8x60())
		pm8901_reset_pwr_off(0);

	if (lower_pshold) {
		__raw_writel(0, PSHOLD_CTL_SU);
		mdelay(10000);
		printk(KERN_ERR "Powering off has failed\n");
	}
	return;
}

static void msm_power_off(void)
{
	/* MSM initiated power off, lower ps_hold */
	__msm_power_off(1);
}

static void cpu_power_off(void *data)
{
	int rc;

	pr_err("PMIC Initiated shutdown %s cpu=%d\n", __func__,
						smp_processor_id());
	if (smp_processor_id() == 0) {
		/*
		 * PMIC initiated power off, do not lower ps_hold, pmic will
		 * shut msm down
		 */
		__msm_power_off(0);

		pet_watchdog();
		pr_err("Calling scm to disable arbiter\n");
		/* call secure manager to disable arbiter and never return */
		rc = scm_call_atomic1(SCM_SVC_PWR,
						SCM_IO_DISABLE_PMIC_ARBITER, 1);

		pr_err("SCM returned even when asked to busy loop rc=%d\n", rc);
		pr_err("waiting on pmic to shut msm down\n");
	}

	preempt_disable();
	while (1)
		;
}

static irqreturn_t resout_irq_handler(int irq, void *dev_id)
{
	pr_warn("%s PMIC Initiated shutdown\n", __func__);
	oops_in_progress = 1;
	smp_call_function_many(cpu_online_mask, cpu_power_off, NULL, 0);
	if (smp_processor_id() == 0)
		cpu_power_off(NULL);
	preempt_disable();
	while (1)
		;
	return IRQ_HANDLED;
}

inline void soc_restart(char mode, const char *cmd)
{
	arm_pm_restart(mode, cmd);
}

void arch_reset(char mode, const char *cmd)
{
	/* arch_reset should only enter once*/
	if (atomic_add_return(1, &restart_counter) != 1)
		return;

#ifdef CONFIG_MSM_DLOAD_MODE

	/* This looks like a normal reboot at this point. */
	set_dload_mode(0);

	/* Write download mode flags if we're panic'ing */
	set_dload_mode(in_panic);

	/* Write download mode flags if restart_mode says so */
	if (restart_mode == RESTART_DLOAD)
		set_dload_mode(1);

	/* Kill download mode if master-kill switch is set */
	if (!download_mode)
		set_dload_mode(0);
#endif

	printk(KERN_NOTICE "Going down for restart now\n");
	printk(KERN_NOTICE "%s: mode %d\n", __func__, mode);
	if (cmd) {
		printk(KERN_NOTICE "%s: restart command `%s'.\n", __func__, cmd);
		/* XXX: modem will set msg itself.
			Dying msg should be passed to this function directly. */
		if (mode != RESTART_MODE_MODEM_CRASH)
			set_restart_msg(cmd);
	} else
		printk(KERN_NOTICE "%s: no command restart.\n", __func__);

#ifdef CONFIG_ARCH_MSM8960
	if (pm8xxx_reset_pwr_off(1)) {
		in_panic = 1;
	}
#else
	pm8xxx_reset_pwr_off(1);
#endif

	if (in_panic) {
		/* Reason & message are set in panic notifier, panic_prep_restart.
		   Kernel panic is a top priority.
		   Keep this condition to avoid the reason and message being modified. */
	} else if (!cmd) {
		set_restart_reason(RESTART_REASON_REBOOT);
	} else if (!strncmp(cmd, "bootloader", 10)) {
		set_restart_reason(RESTART_REASON_BOOTLOADER);
	} else if (!strncmp(cmd, "recovery", 8)) {
		set_restart_reason(RESTART_REASON_RECOVERY);
	} else if (!strcmp(cmd, "eraseflash")) {
		set_restart_reason(RESTART_REASON_ERASE_FLASH);
	} else if (!strncmp(cmd, "oem-", 4)) {
		unsigned long code;
		code = simple_strtoul(cmd + 4, 0, 16) & 0xff;

		/* oem-97, 98, 99 are RIL fatal */
		if ((code == 0x97) || (code == 0x98))
			code = 0x99;

		set_restart_reason(RESTART_REASON_OEM_BASE | code);
	} else if (!strcmp(cmd, "force-hard") ||
			(RESTART_MODE_LEGECY < mode && mode < RESTART_MODE_MAX)
		) {
		/* The only situation modem user triggers reset is NV restore after erasing EFS. */
		if (mode == RESTART_MODE_MODEM_USER_INVOKED)
			set_restart_reason(RESTART_REASON_REBOOT);
		else
			set_restart_reason(RESTART_REASON_RAMDUMP);
	} else {
		/* unknown command */
		set_restart_reason(RESTART_REASON_REBOOT);
	}

#if defined(CONFIG_MSM_RMT_STORAGE_CLIENT)
	/* if modem crashed, do not sync efs. */
	if (mode != RESTART_MODE_MODEM_CRASH &&
		mode != RESTART_MODE_MODEM_UNWEDGE_TIMEOUT &&
		mode != RESTART_MODE_MODEM_WATCHDOG_BITE &&
		mode != RESTART_MODE_MODEM_ERROR_FATAL &&
		mode != RESTART_MODE_APP_WATCHDOG_BARK
		) {
		/* final efs_sync */
		printk(KERN_INFO "from %s\r\n", __func__);

		/* efs sync timeout is 10 seconds.
		   set watchdog to 12 seconds. */
		writel(1, msm_tmr0_base + WDT0_RST);
		writel(0, msm_tmr0_base + WDT0_EN);
		writel(32768 * 12, msm_tmr0_base + WDT0_BARK_TIME);
		writel(32768 * 13, msm_tmr0_base + WDT0_BITE_TIME);
		writel(3, msm_tmr0_base + WDT0_EN);
		dsb();
		set_WDT_EN_footprint(3);

		/* radio team would like to be notified when MDM_DOG_BITE and MDM_FATAL */
		if (in_panic || (mode == RESTART_MODE_MDM_DOG_BITE) || (mode == RESTART_MODE_MDM_FATAL)) {
			rmt_storage_set_msm_client_status(0);
			smsm_change_state(SMSM_APPS_STATE, SMSM_APPS_REBOOT, SMSM_APPS_REBOOT);
		} else {
			wait_rmt_final_call_back(10);
		}
		printk(KERN_INFO "back %s\r\n", __func__);
	}
#endif

	switch (get_restart_reason()) {
	case RESTART_REASON_RIL_FATAL:
	case RESTART_REASON_RAMDUMP:
		send_q6_nmi(); /* NMI trigger to get valid Q6 ramdump */
		if (!in_panic && mode != RESTART_MODE_APP_WATCHDOG_BARK) {
			/* Suspend wdog until all stacks are printed */
			msm_watchdog_suspend(NULL);
			dump_stack();
			show_state_filter(TASK_UNINTERRUPTIBLE);
			print_workqueue();
			msm_watchdog_resume(NULL);
		}
		break;
	}

	/*	for kernel panic & ril fatal & MDM_DOG_BITE & MDM_FATAL,
		kenrel needs waiting modem flushing caches at most 10 seconds. 	*/
	if (in_panic || get_restart_reason() == RESTART_REASON_RIL_FATAL || (mode == RESTART_MODE_MDM_DOG_BITE) || (mode == RESTART_MODE_MDM_FATAL)) {
		int timeout = 10;
		printk(KERN_INFO "%s: wait for modem flushing caches.\n", __func__);
		while (timeout > 0 && !modem_cache_flush_done) {
			/* Kick watchdog.
			   Do not assume efs sync will be executed.
			   Assume watchdog timeout is default 4 seconds. */
			writel(1, msm_tmr0_base + WDT0_RST);

			mdelay(1000);
			timeout--;
		}
		if (timeout <= 0)
			printk(KERN_NOTICE "%s: modem flushes cache timeout.\n", __func__);
	}

#if defined(CONFIG_ARCH_MSM8X60_LTE)
	/* Added by HTC for forcing mdm9K to do the cache flush */
	if (mode == RESTART_MODE_MODEM_CRASH)
		charm_panic_wait_mdm_shutdown();
#endif	/* CONFIG_ARCH_MSM8X60_LTE */

	msm_pm_flush_console();

	__raw_writel(0, msm_tmr0_base + WDT0_EN);
	if (!(machine_is_msm8x60_fusion() || machine_is_msm8x60_fusn_ffa())) {
		mb();
		__raw_writel(0, PSHOLD_CTL_SU); /* Actually reset the chip */
		mdelay(5000);
		pr_notice("PS_HOLD didn't work, falling back to watchdog\n");
	}

	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	__raw_writel(5*0x31F3, msm_tmr0_base + WDT0_BARK_TIME);
	__raw_writel(0x31F3, msm_tmr0_base + WDT0_BITE_TIME);
	__raw_writel(1, msm_tmr0_base + WDT0_EN);

	mdelay(10000);
	printk(KERN_ERR "Restarting has failed\n");
}


static int __init msm_restart_init(void)
{
	int rc;
	arm_pm_restart = arch_reset;

#ifdef CONFIG_MSM_DLOAD_MODE
	atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);
	dload_mode_addr = MSM_IMEM_BASE + DLOAD_MODE_ADDR;

	/* Reset detection is switched on below.*/
	set_dload_mode(1);
#endif
	msm_tmr0_base = msm_timer_get_timer0_base();
	reboot_params = (void *)MSM_REBOOT_REASON_BASE;
	memset(reboot_params, 0x0, sizeof(struct htc_reboot_params));
	set_restart_reason(RESTART_REASON_RAMDUMP);
	set_restart_msg("Unknown");
	reboot_params->radio_flag = get_radio_flag();

	if (pmic_reset_irq != 0) {
		rc = request_any_context_irq(pmic_reset_irq,
					resout_irq_handler, IRQF_TRIGGER_HIGH,
					"restart_from_pmic", NULL);
		if (rc < 0)
			pr_err("pmic restart irq fail rc = %d\n", rc);
	} else {
		pr_warn("no pmic restart interrupt specified\n");
	}

	pm_power_off = msm_power_off;
	return 0;
}

late_initcall(msm_restart_init);
