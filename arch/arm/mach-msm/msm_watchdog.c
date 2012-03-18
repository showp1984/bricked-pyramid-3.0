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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/pm.h>
#include <linux/mfd/pmic8058.h>
#include <linux/jiffies.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <mach/msm_iomap.h>
#include <asm/mach-types.h>
#include <mach/restart.h>
#include <mach/scm-io.h>
#include <mach/scm.h>
#include <mach/socinfo.h>
#include <mach/msm_watchdog.h>
#include <mach/board_htc.h>
#include "htc_watchdog_monitor.h"
#include "timer.h"

#define MODULE_NAME "msm_watchdog"

#define TCSR_WDT_CFG	0x30

#define WDT0_RST	0x38
#define WDT0_EN		0x40
#define WDT0_BARK_TIME	0x4C
#define WDT0_BITE_TIME	0x5C

#define WDT_HZ		32768

/* Periodically dump top 5 cpu usages. */
#define HTC_WATCHDOG_TOP_SCHED_DUMP 0

static void __iomem *msm_tmr0_base = 0;

static unsigned long delay_time;
static unsigned long bark_time;
static unsigned long long last_pet;

static atomic_t watchdog_bark_counter = ATOMIC_INIT(0);

/*
 * On the kernel command line specify
 * msm_watchdog.enable=1 to enable the watchdog
 * By default watchdog is turned on
 */
static int enable = 1;
module_param(enable, int, 0);

/*
 * If the watchdog is enabled at bootup (enable=1),
 * the runtime_disable sysfs node at
 * /sys/module/msm_watchdog/runtime_disable
 * can be used to deactivate the watchdog.
 * This is a one-time setting. The watchdog
 * cannot be re-enabled once it is disabled.
 */
static int runtime_disable;
static DEFINE_MUTEX(disable_lock);
static int wdog_enable_set(const char *val, struct kernel_param *kp);
module_param_call(runtime_disable, wdog_enable_set, param_get_int,
			&runtime_disable, 0644);

static int appsbark = 1;
module_param(appsbark, int, 0);

/*
 * Use /sys/module/msm_watchdog/parameters/print_all_stacks
 * to control whether stacks of all running
 * processes are printed when a wdog bark is received.
 */
static int print_all_stacks = 1;
module_param(print_all_stacks, int,  S_IRUGO | S_IWUSR);

static void pet_watchdog_work(struct work_struct *work);
static void init_watchdog_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(dogwork_struct, pet_watchdog_work);
static DECLARE_WORK(init_dogwork_struct, init_watchdog_work);

static unsigned int last_irqs[NR_IRQS];
void wtd_dump_irqs(unsigned int dump)
{
	int n;
	if (dump) {
		pr_err("\nWatchdog dump irqs:\n");
		pr_err("irqnr       total  since-last   status  name\n");
	}
	for (n = 1; n < NR_IRQS; n++) {
		struct irqaction *act = irq_desc[n].action;
		if (!act && !kstat_irqs(n))
			continue;
		if (dump) {
			pr_err("%5d: %10u %11u  %s\n", n,
				kstat_irqs(n),
				kstat_irqs(n) - last_irqs[n],
				(act && act->name) ? act->name : "???");
		}
		last_irqs[n] = kstat_irqs(n);
	}
}
EXPORT_SYMBOL(wtd_dump_irqs);

void set_WDT_EN_footprint(unsigned char WDT_ENABLE)
{
}

static int kernel_flag_boot_config(char *str)
{
	unsigned long kernelflag;

	if (!str)
		return -EINVAL;

	if (strict_strtoul(str, 16, &kernelflag))
		return -EINVAL;

	if (kernelflag & KERNEL_FLAG_WDOG_DISABLE)
		enable = 0;
	else if (kernelflag & KERNEL_FLAG_WDOG_FIQ)
		appsbark = 0;

	return 0;
}
early_param("kernelflag", kernel_flag_boot_config);

/* Remove static to allow call from suspend/resume function */
int msm_watchdog_suspend(struct device *dev)
{
	if (!enable || !msm_tmr0_base)
		return 0;

	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	__raw_writel(0, msm_tmr0_base + WDT0_EN);
	mb();
	set_WDT_EN_footprint(0);
	printk(KERN_DEBUG "msm_watchdog_suspend\n");
	return 0;
}
EXPORT_SYMBOL(msm_watchdog_suspend);

/* Remove static to allow call from suspend/resume function */
int msm_watchdog_resume(struct device *dev)
{
	if (!enable || !msm_tmr0_base)
		return 0;

	__raw_writel(1, msm_tmr0_base + WDT0_EN);
	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	set_WDT_EN_footprint(1);
	last_pet = sched_clock();
	printk(KERN_DEBUG "msm_watchdog_resume\n");
	mb();
	return 0;
}
EXPORT_SYMBOL(msm_watchdog_resume);

static void msm_watchdog_stop(void)
{
	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	__raw_writel(0, msm_tmr0_base + WDT0_EN);
	set_WDT_EN_footprint(0);
	printk(KERN_INFO "msm_watchdog_stop");
}

static int panic_wdog_handler(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	if (panic_timeout == 0) {
		__raw_writel(0, msm_tmr0_base + WDT0_EN);
		mb();
		set_WDT_EN_footprint(0);
	} else {
		__raw_writel(WDT_HZ * (panic_timeout + 4),
				msm_tmr0_base + WDT0_BARK_TIME);
		__raw_writel(WDT_HZ * (panic_timeout + 4),
				msm_tmr0_base + WDT0_BITE_TIME);
		__raw_writel(1, msm_tmr0_base + WDT0_RST);
	}
	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_wdog_handler,
};

static int wdog_enable_set(const char *val, struct kernel_param *kp)
{
	int ret = 0;
	int old_val = runtime_disable;

	mutex_lock(&disable_lock);

	if (!enable) {
		printk(KERN_INFO "MSM Watchdog is not active.\n");
		ret = -EINVAL;
		goto done;
	}

	ret = param_set_int(val, kp);

	if (ret)
		goto done;

	switch (runtime_disable) {

	case 1:
		if (!old_val) {
			__raw_writel(0, msm_tmr0_base + WDT0_EN);
			mb();
			free_irq(WDT0_ACCSCSSNBARK_INT, 0);
			enable = 0;
			atomic_notifier_chain_unregister(&panic_notifier_list,
			       &panic_blk);
			cancel_delayed_work(&dogwork_struct);
			/* may be suspended after the first write above */
			__raw_writel(0, msm_tmr0_base + WDT0_EN);
			printk(KERN_INFO "MSM Watchdog deactivated.\n");
		}
	break;

	default:
		runtime_disable = old_val;
		ret = -EINVAL;
	break;

	}

done:
	mutex_unlock(&disable_lock);
	return ret;
}

void pet_watchdog(void)
{
	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	last_pet = sched_clock();

#if HTC_WATCHDOG_TOP_SCHED_DUMP
	htc_watchdog_top_stat();
#endif
	htc_watchdog_pet_cpu_record();
}

static void pet_watchdog_work(struct work_struct *work)
{
	pet_watchdog();

	if (enable)
		schedule_delayed_work_on(0, &dogwork_struct, delay_time);

	wtd_dump_irqs(0);
}

static int msm_watchdog_remove(struct platform_device *pdev)
{
	if (enable) {
		__raw_writel(0, msm_tmr0_base + WDT0_EN);
		mb();
		set_WDT_EN_footprint(0);
		free_irq(WDT0_ACCSCSSNBARK_INT, 0);
		enable = 0;
		/* In case we got suspended mid-exit */
		__raw_writel(0, msm_tmr0_base + WDT0_EN);
	}
	printk(KERN_INFO "MSM Watchdog Exit - Deactivated\n");
	return 0;
}

static irqreturn_t wdog_bark_handler(int irq, void *dev_id)
{
	unsigned long nanosec_rem;
	unsigned long long t = sched_clock();
	struct task_struct *tsk;

	/* this should only enter once*/
	if (atomic_add_return(1, &watchdog_bark_counter) != 1)
		return IRQ_HANDLED;

	nanosec_rem = do_div(t, 1000000000);
	printk(KERN_INFO "Watchdog bark! Now = %lu.%06lu\n", (unsigned long) t,
		nanosec_rem / 1000);

	nanosec_rem = do_div(last_pet, 1000000000);
	printk(KERN_INFO "Watchdog last pet at %lu.%06lu\n", (unsigned long)
		last_pet, nanosec_rem / 1000);

	if (print_all_stacks) {

		/* Suspend wdog until all stacks are printed */
		msm_watchdog_suspend(NULL);

		/* Dump top cpu loading processes */
		htc_watchdog_top_stat();

		/* Dump PC, LR, and registers. */
		print_modules();
		__show_regs(get_irq_regs());

		wtd_dump_irqs(1);

		dump_stack();

		printk(KERN_INFO "Stack trace dump:\n");

		for_each_process(tsk) {
			printk(KERN_INFO "\nPID: %d, Name: %s\n",
				tsk->pid, tsk->comm);
			show_stack(tsk, NULL);
		}

		/* HTC changes: show blocked processes to debug hang problems */
		printk(KERN_INFO "\n### Show Blocked State ###\n");
		show_state_filter(TASK_UNINTERRUPTIBLE);
		print_workqueue();

		msm_watchdog_resume(NULL);
	}

	arm_pm_restart(RESTART_MODE_APP_WATCHDOG_BARK, "Apps-watchdog-bark-received!");
	return IRQ_HANDLED;
}

#define SCM_SET_REGSAVE_CMD 0x2

static void configure_bark_dump(void)
{
	int ret;
	struct {
		unsigned addr;
		int len;
	} cmd_buf;

	if (!appsbark) {
		cmd_buf.addr = MSM_TZ_DOG_BARK_REG_SAVE_PHYS;
		cmd_buf.len  = MSM_TZ_DOG_BARK_REG_SAVE_SIZE;

		ret = scm_call(SCM_SVC_UTIL, SCM_SET_REGSAVE_CMD,
			       &cmd_buf, sizeof(cmd_buf), NULL, 0);
		if (ret)
			pr_err("Setting register save address failed.\n"
			       "Registers won't be dumped on a dog "
			       "bite\n");
		else
			pr_info("%s: regsave address = 0x%X\n",
					__func__, cmd_buf.addr);
	} else
		pr_info("%s: dogbark processed by apps side\n", __func__);
}

static void init_watchdog_work(struct work_struct *work)
{
	u64 timeout = (bark_time * WDT_HZ)/1000;
	__raw_writel(timeout, msm_tmr0_base + WDT0_BARK_TIME);
	__raw_writel(timeout + 3*WDT_HZ, msm_tmr0_base + WDT0_BITE_TIME);

	schedule_delayed_work_on(0, &dogwork_struct, delay_time);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &panic_blk);

	__raw_writel(1, msm_tmr0_base + WDT0_EN);
	__raw_writel(1, msm_tmr0_base + WDT0_RST);
	last_pet = sched_clock();

	htc_watchdog_monitor_init();

	printk(KERN_INFO "MSM Watchdog Initialized\n");

	return;
}

static int msm_watchdog_probe(struct platform_device *pdev)
{
	struct msm_watchdog_pdata *pdata = pdev->dev.platform_data;
	int ret;

	msm_tmr0_base = msm_timer_get_timer0_base();

	if (!enable || !pdata || !pdata->pet_time || !pdata->bark_time) {
		/*Turn off watchdog enabled by hboot*/
		msm_watchdog_stop();
		printk(KERN_INFO "MSM Watchdog Not Initialized\n");
		return -ENODEV;
	}

	if (!pdata->has_secure)
		appsbark = 1;

	bark_time = pdata->bark_time;

	/* Must request irq before sending scm command */
	ret = request_irq(WDT0_ACCSCSSNBARK_INT, wdog_bark_handler, 0,
			  "apps_wdog_bark", NULL);
	if (ret)
		return -EINVAL;

	configure_bark_dump();

	delay_time = msecs_to_jiffies(pdata->pet_time);
	schedule_work_on(0, &init_dogwork_struct);

	return 0;
}

static struct platform_driver msm_watchdog_driver = {
	.probe = msm_watchdog_probe,
	.remove = msm_watchdog_remove,
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
	},
};

static int init_watchdog(void)
{
	return platform_driver_register(&msm_watchdog_driver);
}

late_initcall(init_watchdog);
MODULE_DESCRIPTION("MSM Watchdog Driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
