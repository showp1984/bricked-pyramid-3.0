/* arch/arm/mach-msm/htc_watchdog_monitor.c
 *
 * Copyright (C) 2011 HTC Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel_stat.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include "htc_watchdog_monitor.h"

#define MAX_PID 32768
#define NUM_BUSY_THREAD_CHECK 5

#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

static unsigned int *prev_proc_stat;
static int *curr_proc_delta;
static struct task_struct **task_ptr_array;
static struct cpu_usage_stat  old_cpu_stat;
static spinlock_t lock;

/* Sync fs/proc/stat.c to caculate all cpu statistics */
static void get_all_cpu_stat(struct cpu_usage_stat *cpu_stat)
{
	int i;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;

	if (!cpu_stat)
		return;

	user = nice = system = idle = iowait =
		irq = softirq = steal = cputime64_zero;
	guest = guest_nice = cputime64_zero;

	for_each_possible_cpu(i) {
		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, kstat_cpu(i).cpustat.idle);
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = cputime64_add(iowait, kstat_cpu(i).cpustat.iowait);
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);
		steal = cputime64_add(steal, kstat_cpu(i).cpustat.steal);
		guest = cputime64_add(guest, kstat_cpu(i).cpustat.guest);
		guest_nice = cputime64_add(guest_nice,
			kstat_cpu(i).cpustat.guest_nice);
	}
	cpu_stat->user = user;
	cpu_stat->nice = nice;
	cpu_stat->system = system;
	cpu_stat->softirq = softirq;
	cpu_stat->irq = irq;
	cpu_stat->idle = idle;
	cpu_stat->iowait = iowait;
	cpu_stat->steal = steal;
	cpu_stat->guest = guest;
	cpu_stat->guest_nice = guest_nice;
}

void htc_watchdog_monitor_init(void)
{
	spin_lock_init(&lock);
	prev_proc_stat = vmalloc(sizeof(int) * MAX_PID);
	curr_proc_delta = vmalloc(sizeof(int) * MAX_PID);
	task_ptr_array = vmalloc(sizeof(int) * MAX_PID);
	if (prev_proc_stat)
		memset(prev_proc_stat, 0, sizeof(int) * MAX_PID);
	if (curr_proc_delta)
		memset(curr_proc_delta, 0, sizeof(int) * MAX_PID);
	if (task_ptr_array)
		memset(task_ptr_array, 0, sizeof(int) * MAX_PID);

	get_all_cpu_stat(&old_cpu_stat);
}

static int findBiggestInRange(int *array, int max_limit_idx)
{
	int largest_idx = 0, i;

	for (i = 0; i < MAX_PID; i++) {
		if (array[i] > array[largest_idx] &&
		(max_limit_idx == -1 || array[i] < array[max_limit_idx]))
			largest_idx = i;
	}

	return largest_idx;
}

/* sorting from large to small */
static void sorting(int *source, int *output)
{
	int i;
	for (i = 0; i < NUM_BUSY_THREAD_CHECK; i++) {
		if (i == 0)
			output[i] = findBiggestInRange(source, -1);
		else
			output[i] = findBiggestInRange(source, output[i-1]);
	}
}

/*
 *  Record the proc cpu statistic when petting watchdog
 */
void htc_watchdog_pet_cpu_record(void)
{
	struct task_struct *p;
	ulong flags;
	struct task_cputime cputime;

	if (prev_proc_stat == NULL)
		return;

	spin_lock_irqsave(&lock, flags);
	get_all_cpu_stat(&old_cpu_stat);

	/* calculate the cpu time of each process */
	for_each_process(p) {
		if (p->pid < MAX_PID) {
			thread_group_cputime(p, &cputime);
			prev_proc_stat[p->pid] = cputime.stime + cputime.utime;
		}
	}
	spin_unlock_irqrestore(&lock, flags);
}

/*
 *  When watchdog bark, print the cpu statistic
 */
void htc_watchdog_top_stat(void)
{
	struct task_struct *p;
	int top_loading[NUM_BUSY_THREAD_CHECK], i;
	unsigned long user_time, system_time, io_time;
	unsigned long irq_time, idle_time, delta_time;
	ulong flags;
	struct task_cputime cputime;
	struct cpu_usage_stat new_cpu_stat;

	if (task_ptr_array == NULL ||
			curr_proc_delta == NULL ||
			prev_proc_stat == NULL)
		return;

	memset(curr_proc_delta, 0, sizeof(int) * MAX_PID);
	memset(task_ptr_array, 0, sizeof(int) * MAX_PID);

	printk(KERN_ERR"\n\n[%s] Start to dump:\n", __func__);

	spin_lock_irqsave(&lock, flags);
	get_all_cpu_stat(&new_cpu_stat);

	/* calculate the cpu time of each process */
	for_each_process(p) {
		thread_group_cputime(p, &cputime);

		if (p->pid < MAX_PID) {
			curr_proc_delta[p->pid] =
				(cputime.utime + cputime.stime)
				- (prev_proc_stat[p->pid]);
			task_ptr_array[p->pid] = p;
		}
	}

	/* sorting to get the top cpu consumers */
	sorting(curr_proc_delta, top_loading);

	/* calculate the total delta time */
	user_time = (unsigned long)((new_cpu_stat.user + new_cpu_stat.nice)
			- (old_cpu_stat.user + old_cpu_stat.nice));
	system_time = (unsigned long)(new_cpu_stat.system - old_cpu_stat.system);
	io_time = (unsigned long)(new_cpu_stat.iowait - old_cpu_stat.iowait);
	irq_time = (unsigned long)((new_cpu_stat.irq + new_cpu_stat.softirq)
			- (old_cpu_stat.irq + old_cpu_stat.softirq));
	idle_time = (unsigned long)
	((new_cpu_stat.idle + new_cpu_stat.steal + new_cpu_stat.guest)
	- (old_cpu_stat.idle + old_cpu_stat.steal + old_cpu_stat.guest));
	delta_time = user_time + system_time + io_time + irq_time + idle_time;

	/* print most time consuming processes */
	printk(KERN_ERR"CPU\t\tPID\t\tName\n");
	for (i = 0; i < NUM_BUSY_THREAD_CHECK; i++) {
		printk(KERN_ERR "%lu%%\t\t%d\t\t%s\n",
			curr_proc_delta[top_loading[i]] * 100 / delta_time,
			top_loading[i],
			task_ptr_array[top_loading[i]]->comm);
	}
	spin_unlock_irqrestore(&lock, flags);
	printk(KERN_ERR "\n");
}
