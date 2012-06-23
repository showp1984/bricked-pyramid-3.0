/*
 * arch/arm/mach-msm/msm_mpdecision.c
 *
 * cpu auto-hotplug/unplug based on system load for MSM dualcore cpus
 * single core while screen is off
 *
 * Copyright (c) 2012, Dennis Rassmann <showp1984@gmail.com>
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
#include <linux/delay.h>

#define MPDEC_TAG "[MPDEC]: "
#define MSM_MPDEC_STARTDELAY 70000
#define MSM_MPDEC_DELAY 500
#define MSM_MPDEC_PAUSE 10000

enum {
	MSM_MPDEC_DISABLED = 0,
	MSM_MPDEC_IDLE,
	MSM_MPDEC_DOWN,
	MSM_MPDEC_UP,
};

struct msm_mpdec_cpudata_t {
	struct mutex suspend_mutex;
	int online;
	int device_suspended;
	cputime64_t on_time;
};
static DEFINE_PER_CPU(struct msm_mpdec_cpudata_t, msm_mpdec_cpudata);

static struct delayed_work msm_mpdec_work;
static DEFINE_MUTEX(msm_cpu_lock);

static struct msm_mpdec_tuners {
	unsigned int startdelay;
	unsigned int delay;
	unsigned int pause;
	bool scroff_single_core;
} msm_mpdec_tuners_ins = {
	.startdelay = MSM_MPDEC_STARTDELAY,
	.delay = MSM_MPDEC_DELAY,
	.pause = MSM_MPDEC_PAUSE,
	.scroff_single_core = true,
};

static unsigned int NwNs_Threshold[4] = {35, 0, 0, 5};
static unsigned int TwTs_Threshold[4] = {250, 0, 0, 250};

extern unsigned int get_rq_info(void);
unsigned int state = MSM_MPDEC_IDLE;
bool was_paused = false;

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

	if (state == MSM_MPDEC_DISABLED)
		return MSM_MPDEC_DISABLED;

	current_time = ktime_to_ms(ktime_get());
	if (current_time <= msm_mpdec_tuners_ins.startdelay)
		return MSM_MPDEC_IDLE;

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
	unsigned int cpu = nr_cpu_ids;
	cputime64_t on_time = 0;

	if (per_cpu(msm_mpdec_cpudata, (CONFIG_NR_CPUS - 1)).device_suspended == true)
		goto out;

	if (!mutex_trylock(&msm_cpu_lock))
		goto out;

	/* if sth messed with the cpus, update the check vars so we can proceed */
	if (was_paused) {
		for_each_possible_cpu(cpu) {
			if (cpu_online(cpu))
				per_cpu(msm_mpdec_cpudata, cpu).online = true;
			else if (!cpu_online(cpu))
				per_cpu(msm_mpdec_cpudata, cpu).online = false;
		}
		was_paused = false;
	}

	state = mp_decision();
	switch (state) {
	case MSM_MPDEC_DISABLED:
	case MSM_MPDEC_IDLE:
		break;
	case MSM_MPDEC_DOWN:
		cpu = (CONFIG_NR_CPUS - 1);
		if (cpu < nr_cpu_ids) {
			if ((per_cpu(msm_mpdec_cpudata, cpu).online == true) && (cpu_online(cpu))) {
				cpu_down(cpu);
				per_cpu(msm_mpdec_cpudata, cpu).online = false;
				on_time = ktime_to_ms(ktime_get()) - per_cpu(msm_mpdec_cpudata, cpu).on_time;
				pr_info(MPDEC_TAG"CPU[%d] on->off | Mask=[%d%d] | time online: %llu\n",
						cpu, cpu_online(0), cpu_online(1), on_time);
			} else if (per_cpu(msm_mpdec_cpudata, cpu).online != cpu_online(cpu)) {
				pr_info(MPDEC_TAG"CPU[%d] was controlled outside of mpdecision! | pausing [%d]ms\n",
						cpu, msm_mpdec_tuners_ins.pause);
				msleep(msm_mpdec_tuners_ins.pause);
				was_paused = true;
			}
		}
		break;
	case MSM_MPDEC_UP:
		cpu = (CONFIG_NR_CPUS - 1);
		if (cpu < nr_cpu_ids) {
			if ((per_cpu(msm_mpdec_cpudata, cpu).online == false) && (!cpu_online(cpu))) {
				cpu_up(cpu);
				per_cpu(msm_mpdec_cpudata, cpu).online = true;
				per_cpu(msm_mpdec_cpudata, cpu).on_time = ktime_to_ms(ktime_get());
				pr_info(MPDEC_TAG"CPU[%d] off->on | Mask=[%d%d]\n",
						cpu, cpu_online(0), cpu_online(1));
			} else if (per_cpu(msm_mpdec_cpudata, cpu).online != cpu_online(cpu)) {
				pr_info(MPDEC_TAG"CPU[%d] was controlled outside of mpdecision! | pausing [%d]ms\n",
						cpu, msm_mpdec_tuners_ins.pause);
				msleep(msm_mpdec_tuners_ins.pause);
				was_paused = true;
			}
		}
		break;
	default:
		pr_err(MPDEC_TAG"%s: invalid mpdec hotplug state %d\n",
		       __func__, state);
	}
	mutex_unlock(&msm_cpu_lock);

out:
	if (state != MSM_MPDEC_DISABLED)
		schedule_delayed_work(&msm_mpdec_work,
				msecs_to_jiffies(msm_mpdec_tuners_ins.delay));
	return;
}

static void msm_mpdec_early_suspend(struct early_suspend *h)
{
	int cpu = 0;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(msm_mpdec_cpudata, cpu).suspend_mutex);
		if (((cpu >= (CONFIG_NR_CPUS - 1)) && (num_online_cpus() > 1)) && (msm_mpdec_tuners_ins.scroff_single_core)) {
			cpu_down(cpu);
			pr_info(MPDEC_TAG"Screen -> off. Suspended CPU%d | Mask=[%d%d]\n",
					cpu, cpu_online(0), cpu_online(1));
			per_cpu(msm_mpdec_cpudata, cpu).online = false;
		}
		per_cpu(msm_mpdec_cpudata, cpu).device_suspended = true;
		mutex_unlock(&per_cpu(msm_mpdec_cpudata, cpu).suspend_mutex);
	}
}

static void msm_mpdec_late_resume(struct early_suspend *h)
{
	int cpu = 0;
	for_each_possible_cpu(cpu) {
		mutex_lock(&per_cpu(msm_mpdec_cpudata, cpu).suspend_mutex);
		if ((cpu >= (CONFIG_NR_CPUS - 1)) && (num_online_cpus() < CONFIG_NR_CPUS)) {
			/* Always enable cpus when screen comes online.
			 * This boosts the wakeup process.
			 */
			cpu_up(cpu);
			per_cpu(msm_mpdec_cpudata, cpu).on_time = ktime_to_ms(ktime_get());
			per_cpu(msm_mpdec_cpudata, cpu).online = true;
			pr_info(MPDEC_TAG"Screen -> on. Hot plugged CPU%d | Mask=[%d%d]\n",
					cpu, cpu_online(0), cpu_online(1));
		}
		per_cpu(msm_mpdec_cpudata, cpu).device_suspended = false;
		mutex_unlock(&per_cpu(msm_mpdec_cpudata, cpu).suspend_mutex);
	}
}

static struct early_suspend msm_mpdec_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = msm_mpdec_early_suspend,
	.resume = msm_mpdec_late_resume,
};

/**************************** SYSFS START ****************************/
struct kobject *msm_mpdec_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)               \
{									\
	return sprintf(buf, "%u\n", msm_mpdec_tuners_ins.object);	\
}

show_one(startdelay, startdelay);
show_one(delay, delay);
show_one(pause, pause);
show_one(scroff_single_core, scroff_single_core);

static ssize_t show_enabled(struct kobject *a, struct attribute *b,
				   char *buf)
{
	unsigned int enabled;
	switch (state) {
	case MSM_MPDEC_DISABLED:
		enabled = 0;
		break;
	case MSM_MPDEC_IDLE:
	case MSM_MPDEC_DOWN:
	case MSM_MPDEC_UP:
		enabled = 1;
		break;
	default:
		enabled = 333;
	}
	return sprintf(buf, "%u\n", enabled);
}

static ssize_t show_nwns_threshold_up(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", NwNs_Threshold[0]);
}

static ssize_t show_nwns_threshold_down(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", NwNs_Threshold[3]);
}

static ssize_t show_twts_threshold_up(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", TwTs_Threshold[0]);
}

static ssize_t show_twts_threshold_down(struct kobject *kobj, struct attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", TwTs_Threshold[3]);
}

static ssize_t store_startdelay(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	msm_mpdec_tuners_ins.startdelay = input;

	return count;
}

static ssize_t store_delay(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	msm_mpdec_tuners_ins.delay = input;

	return count;
}

static ssize_t store_pause(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	msm_mpdec_tuners_ins.pause = input;

	return count;
}

static ssize_t store_scroff_single_core(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	switch (buf[0]) {
	case '0':
		msm_mpdec_tuners_ins.scroff_single_core = input;
		break;
	case '1':
		msm_mpdec_tuners_ins.scroff_single_core = input;
		break;
	default:
		ret = -EINVAL;
	}
	return count;
}

static ssize_t store_enabled(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int cpu, input, enabled;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	switch (state) {
	case MSM_MPDEC_DISABLED:
		enabled = 0;
		break;
	case MSM_MPDEC_IDLE:
	case MSM_MPDEC_DOWN:
	case MSM_MPDEC_UP:
		enabled = 1;
		break;
	default:
		enabled = 333;
	}

	if (buf[0] == enabled)
		return -EINVAL;

	switch (buf[0]) {
	case '0':
		state = MSM_MPDEC_DISABLED;
		cpu = (CONFIG_NR_CPUS - 1);
		per_cpu(msm_mpdec_cpudata, cpu).on_time = ktime_to_ms(ktime_get());
		per_cpu(msm_mpdec_cpudata, cpu).online = true;
		cpu_up(cpu);
		pr_info(MPDEC_TAG"nap time... Hot plugged CPU[%d] | Mask=[%d%d]\n",
				 cpu, cpu_online(0), cpu_online(1));
		break;
	case '1':
		state = MSM_MPDEC_IDLE;
		was_paused = true;
		schedule_delayed_work(&msm_mpdec_work, 0);
		pr_info(MPDEC_TAG"firing up mpdecision...\n");
		break;
	default:
		ret = -EINVAL;
	}
	return count;
}

static ssize_t store_nwns_threshold_up(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	NwNs_Threshold[0] = input;

	return count;
}

static ssize_t store_nwns_threshold_down(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	NwNs_Threshold[3] = input;

	return count;
}

static ssize_t store_twts_threshold_up(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	TwTs_Threshold[0] = input;

	return count;
}

static ssize_t store_twts_threshold_down(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	TwTs_Threshold[3] = input;

	return count;
}

define_one_global_rw(startdelay);
define_one_global_rw(delay);
define_one_global_rw(pause);
define_one_global_rw(scroff_single_core);
define_one_global_rw(enabled);
define_one_global_rw(nwns_threshold_up);
define_one_global_rw(nwns_threshold_down);
define_one_global_rw(twts_threshold_up);
define_one_global_rw(twts_threshold_down);

static struct attribute *msm_mpdec_attributes[] = {
	&startdelay.attr,
	&delay.attr,
	&pause.attr,
	&scroff_single_core.attr,
	&enabled.attr,
	&nwns_threshold_up.attr,
	&nwns_threshold_down.attr,
	&twts_threshold_up.attr,
	&twts_threshold_down.attr,
	NULL
};


static struct attribute_group msm_mpdec_attr_group = {
	.attrs = msm_mpdec_attributes,
	.name = "conf",
};
/**************************** SYSFS END ****************************/

static int __init msm_mpdec(void)
{
	int cpu, rc, err = 0;

	for_each_possible_cpu(cpu) {
		mutex_init(&(per_cpu(msm_mpdec_cpudata, cpu).suspend_mutex));
		per_cpu(msm_mpdec_cpudata, cpu).device_suspended = false;
		per_cpu(msm_mpdec_cpudata, cpu).online = true;
	}

	INIT_DELAYED_WORK(&msm_mpdec_work, msm_mpdec_work_thread);
	if (state != MSM_MPDEC_DISABLED)
		schedule_delayed_work(&msm_mpdec_work, 0);

	register_early_suspend(&msm_mpdec_early_suspend_handler);

	msm_mpdec_kobject = kobject_create_and_add("msm_mpdecision", kernel_kobj);
	if (msm_mpdec_kobject) {
		rc = sysfs_create_group(msm_mpdec_kobject,
							&msm_mpdec_attr_group);
		if (rc) {
			pr_warn(MPDEC_TAG"sysfs: ERROR, could not create sysfs group");
		}
	} else
		pr_warn(MPDEC_TAG"sysfs: ERROR, could not create sysfs kobj");

	pr_info(MPDEC_TAG"%s init complete.", __func__);

	return err;
}

late_initcall(msm_mpdec);

