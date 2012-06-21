/*
 * arch/arm/mach-msm/msm_mpdecision.c
 *
 * cpu auto-hotplug/unplug based on system load for MSM dualcore cpus
 * single core while screen is off
 *
 * Copyright (c) 2012, Dennis Rassmann.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/earlysuspend.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <asm-generic/cputime.h>
#include <linux/hrtimer.h>

#define MPDEC_TAG "[MPDEC]: "
#define MSM_MPDEC_DELAY 400

enum {
	MSM_MPDEC_DISABLED = 0,
	MSM_MPDEC_IDLE,
	MSM_MPDEC_DOWN,
	MSM_MPDEC_UP,
};

struct msm_mpdec_suspend_t {
	struct mutex suspend_mutex;
	int device_suspended;
};
static DEFINE_PER_CPU(struct msm_mpdec_suspend_t, msm_mpdec_suspend);

static struct delayed_work msm_mpdec_work;
static DEFINE_SPINLOCK(msm_cpu_lock);

bool scroff_single_core = true;

static unsigned int NwNs_Threshold[4] = {20, 0, 0, 5};
static unsigned int TwTs_Threshold[4] = {250, 0, 0, 250};
extern unsigned int get_rq_info(void);

static int mp_decision(void)
{
	static bool first_call = true;
	int new_state = MSM_MPDEC_IDLE;
	int nr_cpu_online;
	int index;
	unsigned int rq_depth;
	static cputime64_t total_time = 0;
	static cputime64_t last_time;
	cputime64_t current_time;
	cputime64_t this_time = 0;

	current_time = ktime_to_ms(ktime_get());
	if (first_call) {
		first_call = false;
	} else {
		this_time = current_time - last_time;
	}
	total_time += this_time;

	rq_depth = get_rq_info();
	nr_cpu_online = num_online_cpus();

	if (nr_cpu_online) {
		index = (nr_cpu_online - 1) * 2;
		if ((nr_cpu_online < 2) && (rq_depth >= NwNs_Threshold[index])) {
			if (total_time >= TwTs_Threshold[index]) {
				new_state = MSM_MPDEC_UP;
			}
		} else if (rq_depth <= NwNs_Threshold[index+1]) {
			if (total_time >= TwTs_Threshold[index+1] ) {
				new_state = MSM_MPDEC_DOWN;
			}
		} else {
			new_state = MSM_MPDEC_IDLE;
			total_time = 0;
		}
	} else {
		total_time = 0;
	}

	if (new_state != MSM_MPDEC_IDLE) {
		total_time = 0;
	}

	last_time = ktime_to_ms(ktime_get());

	return new_state;
}

static void msm_mpdec_work_thread(struct work_struct *work)
{
	int ret = 0;
	unsigned int cpu = nr_cpu_ids;
	unsigned long flags = 0;

	if (per_cpu(msm_mpdec_suspend, (CONFIG_NR_CPUS - 1)).device_suspended == true)
		goto out;

	spin_lock_irqsave(&msm_cpu_lock, flags);

	ret = mp_decision();
	switch (ret) {
	case MSM_MPDEC_DISABLED:
	case MSM_MPDEC_IDLE:
		break;
	case MSM_MPDEC_DOWN:
		cpu = (CONFIG_NR_CPUS - 1);
		if ((cpu < nr_cpu_ids) && (cpu_online(cpu))) {
			cpu_down(cpu);
			pr_info(MPDEC_TAG"CPU[%d] 1->0 | Mask=[%d%d]\n",
					cpu, cpu_online(0), cpu_online(1));
		}
		break;
	case MSM_MPDEC_UP:
		cpu = (CONFIG_NR_CPUS - 1);
		if ((cpu < nr_cpu_ids) && (!cpu_online(cpu))) {
			cpu_up(cpu);
			pr_info(MPDEC_TAG"CPU[%d] 0->1 | Mask=[%d%d]\n",
					cpu, cpu_online(0), cpu_online(1));
		}
		break;
	default:
		pr_err(MPDEC_TAG"%s: invalid mpdec hotplug state %d\n",
		       __func__, ret);
	}

	spin_unlock_irqrestore(&msm_cpu_lock, flags);

out:
	schedule_delayed_work(&msm_mpdec_work,
			msecs_to_jiffies(MSM_MPDEC_DELAY));
	return;
}

static void msm_mpdec_early_suspend(struct early_suspend *h)
{
	int cpu = 0;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(msm_mpdec_suspend, cpu).suspend_mutex);
		if (((cpu >= (CONFIG_NR_CPUS - 1)) && (num_online_cpus() > 1)) && (scroff_single_core)) {
			pr_info(MPDEC_TAG"Screen -> off. Suspending CPU%d", cpu);
			cpu_down(cpu);
			per_cpu(msm_mpdec_suspend, cpu).device_suspended = true;
		}
		mutex_unlock(&per_cpu(msm_mpdec_suspend, cpu).suspend_mutex);
	}
}

static void msm_mpdec_late_resume(struct early_suspend *h)
{
	int cpu = 0;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(msm_mpdec_suspend, cpu).suspend_mutex);
		if ((cpu >= (CONFIG_NR_CPUS - 1)) && (num_online_cpus() < CONFIG_NR_CPUS)) {
			/* Always enable cpus when screen comes online.
			 * This boosts the wakeup process.
			 */
			pr_info(MPDEC_TAG"Screen -> on. Hot plugging CPU%d", cpu);
			cpu_up(cpu);
			per_cpu(msm_mpdec_suspend, cpu).device_suspended = false;
		}
		mutex_unlock(&per_cpu(msm_mpdec_suspend, cpu).suspend_mutex);
	}
}

static struct early_suspend msm_mpdec_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.suspend = msm_mpdec_early_suspend,
	.resume = msm_mpdec_late_resume,
};

static int __init msm_mpdec(void)
{
	int cpu, err = 0;
	for_each_possible_cpu(cpu) {
		mutex_init(&(per_cpu(msm_mpdec_suspend, cpu).suspend_mutex));
		per_cpu(msm_mpdec_suspend, cpu).device_suspended = false;
	}

	INIT_DELAYED_WORK(&msm_mpdec_work, msm_mpdec_work_thread);
	schedule_delayed_work(&msm_mpdec_work, 0);

	register_early_suspend(&msm_mpdec_early_suspend_handler);

	pr_info(MPDEC_TAG"%s init complete.", __func__);

	return err;
}

late_initcall(msm_mpdec);

