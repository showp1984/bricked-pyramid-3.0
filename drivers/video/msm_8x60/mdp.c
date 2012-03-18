/* Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/clk.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include "mdp.h"
#include "msm_fb.h"
#ifdef CONFIG_MSM_MDP40
#include "mdp4.h"
#endif

uint32 mdp4_extn_disp;
static struct clk *mdp_clk;
static struct clk *mdp_pclk;
struct regulator *footswitch;

struct completion mdp_ppp_comp;
struct semaphore mdp_ppp_mutex;
struct semaphore mdp_pipe_ctrl_mutex;

unsigned long mdp_timer_duration = (HZ/20);

boolean mdp_ppp_waiting = FALSE;
uint32 mdp_tv_underflow_cnt;
uint32 mdp_lcdc_underflow_cnt;

boolean mdp_current_clk_on = FALSE;
boolean mdp_is_in_isr = FALSE;

/*
 * legacy mdp_in_processing is only for DMA2-MDDI
 * this applies to DMA2 block only
 */
uint32 mdp_in_processing = FALSE;

#ifdef CONFIG_MSM_MDP40
uint32 mdp_intr_mask = MDP4_ANY_INTR_MASK;
#else
uint32 mdp_intr_mask = MDP_ANY_INTR_MASK;
#endif

MDP_BLOCK_TYPE mdp_debug[MDP_MAX_BLOCK];

atomic_t mdp_block_power_cnt[MDP_MAX_BLOCK];

spinlock_t mdp_spin_lock;
spinlock_t mdp_done_lock;
struct workqueue_struct *mdp_dma_wq;	/*mdp dma wq */
struct workqueue_struct *mdp_vsync_wq;	/*mdp vsync wq */

static struct workqueue_struct *mdp_pipe_ctrl_wq; /* mdp mdp pipe ctrl wq */
static struct delayed_work mdp_pipe_ctrl_worker;

#ifdef CONFIG_MSM_MDP40
struct mdp_dma_data dma2_data;
struct mdp_dma_data dma_s_data;
struct mdp_dma_data dma_e_data;
ulong mdp4_display_intf;
#else
static struct mdp_dma_data dma2_data;
static struct mdp_dma_data dma_s_data;
static struct mdp_dma_data dma_e_data;
#endif
static struct mdp_dma_data dma3_data;

#ifdef MSM_FB_ENABLE_DBGFS
struct dentry *mdp_dir;
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static int mdp_suspend(struct platform_device *pdev, pm_message_t state);
#else
#define mdp_suspend NULL
#endif

struct timeval mdp_dma2_timeval;
struct timeval mdp_ppp_timeval;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend early_suspend;
#ifdef CONFIG_HTC_ONMODE_CHARGING
static struct early_suspend onchg_suspend;
#endif
#endif

int skip_hist_count;

#ifndef CONFIG_MSM_MDP22
DEFINE_MUTEX(mdp_lut_push_sem);
static int mdp_lut_i;
static int mdp_lut_hw_update(struct fb_cmap *cmap)
{
	int i;
	u16 *c[3];
	u16 r, g, b;

	c[0] = cmap->green;
	c[1] = cmap->blue;
	c[2] = cmap->red;

	for (i = 0; i < cmap->len; i++) {
		if (copy_from_user(&r, cmap->red++, sizeof(r)) ||
		    copy_from_user(&g, cmap->green++, sizeof(g)) ||
		    copy_from_user(&b, cmap->blue++, sizeof(b)))
			return -EFAULT;

#ifdef CONFIG_MSM_MDP40
		MDP_OUTP(MDP_BASE + 0x94800 +
#else
		MDP_OUTP(MDP_BASE + 0x93800 +
#endif
			(0x400*mdp_lut_i) + cmap->start*4 + i*4,
				((g & 0xff) |
				 ((b & 0xff) << 8) |
				 ((r & 0xff) << 16)));
	}

	return 0;
}

static int mdp_lut_push;
static int mdp_lut_push_i;
static int mdp_lut_update_nonlcdc(struct fb_info *info, struct fb_cmap *cmap)
{
	int ret;

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = mdp_lut_hw_update(cmap);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	if (ret)
		return ret;
	dsb();
	mutex_lock(&mdp_lut_push_sem);
	mdp_lut_push = 1;
	mdp_lut_push_i = mdp_lut_i;
	mutex_unlock(&mdp_lut_push_sem);

	mdp_lut_i = (mdp_lut_i + 1)%2;

	return 0;
}

static int mdp_lut_update_lcdc(struct fb_info *info, struct fb_cmap *cmap)
{
	int ret;

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = mdp_lut_hw_update(cmap);

	if (ret) {
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
		return ret;
	}

	MDP_OUTP(MDP_BASE + 0x90070, (mdp_lut_i << 10) | 0x17);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	mdp_lut_i = (mdp_lut_i + 1)%2;

	return 0;
}

static void mdp_lut_enable(void)
{
	mutex_lock(&mdp_lut_push_sem);
	if (mdp_lut_push) {
		mdp_lut_push = 0;
		dsb();
		MDP_OUTP(MDP_BASE + 0x90070,
				(mdp_lut_push_i << 10) | 0x17);
	}
	mutex_unlock(&mdp_lut_push_sem);
}

#define MDP_HIST_MAX_BIN 32

#ifdef CONFIG_MSM_MDP40
unsigned int mdp_hist_frame_cnt;
struct completion mdp_hist_comp;
boolean mdp_is_hist_start = FALSE;
#else
static unsigned int mdp_hist_frame_cnt;
static struct completion mdp_hist_comp;
static boolean mdp_is_hist_start = FALSE;
#endif
static DEFINE_MUTEX(mdp_hist_mutex);
static boolean mdp_is_hist_data = FALSE;

/*should hold mdp_hist_mutex before calling this function*/
int _mdp_histogram_ctrl(boolean en)
{
	unsigned long hist_base;
	unsigned long flag;

#ifdef CONFIG_MSM_MDP40
		hist_base = 0x95000;
#else
		hist_base = 0x94000;
#endif

	if (en == TRUE) {
		if (mdp_is_hist_start || mdp_is_hist_data) {
			pr_info("mdp histogram is start\n");
			return -EINVAL;
		}
		skip_hist_count = 0;
		mdp_enable_irq(MDP_HISTOGRAM_TERM);
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		mdp_hist_frame_cnt = 1;

		spin_lock_irqsave(&mdp_spin_lock, flag);
		if (mdp_is_hist_start == FALSE /*&& mdp_rev >= MDP_REV_40*/) {
			MDP_OUTP(MDP_BASE + hist_base + 0x10, 1);
			MDP_OUTP(MDP_BASE + hist_base + 0x1c, INTR_HIST_DONE);
		}
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
		MDP_OUTP(MDP_BASE + hist_base + 0x4, 1);
		MDP_OUTP(MDP_BASE + hist_base, 1);
		mdp_is_hist_data = TRUE;
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	} else {
		if (!mdp_is_hist_start && !mdp_is_hist_data)
			return -EINVAL;

		mdp_is_hist_data = FALSE;
		complete(&mdp_hist_comp);

			mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

			/* Write this register may cause system enter rampdump.
			Since this interrpt won't generate histogram interrupt.
			So we remove it temporary.
			*/
			//status = inpdw(MDP_BASE + hist_base + 0x1C);
			//status &= ~INTR_HIST_DONE;
			//MDP_OUTP(MDP_BASE + hist_base + 0x1C, 0);

			MDP_OUTP(MDP_BASE + hist_base + 0x18, INTR_HIST_DONE);
			mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF,
									FALSE);
		mdp_disable_irq(MDP_HISTOGRAM_TERM);
	}

	return 0;
}

int mdp_histogram_ctrl(boolean en)
{
	int ret = 0;
	mutex_lock(&mdp_hist_mutex);
	ret = _mdp_histogram_ctrl(en);
	mutex_unlock(&mdp_hist_mutex);
	return ret;
}

int mdp_start_histogram(struct fb_info *info)
{
	unsigned long flag;
	int ret = 0;
	mutex_lock(&mdp_hist_mutex);
	if (mdp_is_hist_start == TRUE) {
		PR_DISP_INFO("%s histogram already started\n", __func__);
		ret = -EPERM;
		goto mdp_hist_start_err;
	}
	PR_DISP_INFO("%s", __func__);
	ret = _mdp_histogram_ctrl(TRUE);

	spin_lock_irqsave(&mdp_spin_lock, flag);
	mdp_is_hist_start = TRUE;
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

mdp_hist_start_err:
	mutex_unlock(&mdp_hist_mutex);
	return ret;

}
int mdp_stop_histogram(struct fb_info *info)
{
	unsigned long flag;
	int ret = 0;
	mutex_lock(&mdp_hist_mutex);
	if (!mdp_is_hist_start) {
		PR_DISP_INFO("%s histogram already stopped\n", __func__);
		ret = -EPERM;
		goto mdp_hist_stop_err;
	}
	PR_DISP_INFO("%s", __func__);
	spin_lock_irqsave(&mdp_spin_lock, flag);
	mdp_is_hist_start = FALSE;
	spin_unlock_irqrestore(&mdp_spin_lock, flag);
	/* disable the irq for histogram since we handled it
	   when the control reaches here */
	ret = _mdp_histogram_ctrl(FALSE);

mdp_hist_stop_err:
	mutex_unlock(&mdp_hist_mutex);
	return ret;
}

static int mdp_copy_hist_data(struct mdp_histogram *hist, struct msm_fb_data_type *mfd)
{
    char *mdp_hist_base;
    uint32 r_data_offset = 0x100, g_data_offset = 0x200;
    uint32 b_data_offset = 0x300;
    int ret = 0;

#ifdef CONFIG_MSM_MDP40
		mdp_hist_base = MDP_BASE + 0x95000;
#else
		mdp_hist_base = MDP_BASE + 0x94000;
#endif

	mutex_lock(&mfd->dma->ov_mutex);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	if (skip_hist_count != 1) {
		if (hist->r) {
		    ret = copy_to_user(hist->r, mdp_hist_base + r_data_offset,
				hist->bin_cnt * 4);
			if (ret)
				goto hist_err;
		}
		if (hist->g) {
			ret = copy_to_user(hist->g, mdp_hist_base + g_data_offset,
				hist->bin_cnt * 4);
			if (ret)
				goto hist_err;
		}
		if (hist->b) {
			ret = copy_to_user(hist->b, mdp_hist_base + b_data_offset,
				hist->bin_cnt * 4);
			if (ret)
			goto hist_err;
		}
	}

	skip_hist_count = 0;
	if (mdp_is_hist_start == TRUE) {
		MDP_OUTP(mdp_hist_base + 0x004,
		       mdp_hist_frame_cnt);
		MDP_OUTP(mdp_hist_base, 1);
	}
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	mutex_unlock(&mfd->dma->ov_mutex);
	return 0;

hist_err:
	PR_DISP_ERR("%s: invalid hist buffer\n", __func__);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	mutex_unlock(&mfd->dma->ov_mutex);
	return ret;
}

static int mdp_do_histogram(struct fb_info *info, struct mdp_histogram *hist, struct msm_fb_data_type *mfd)
{
	int ret = 0;

	if (!hist->frame_cnt || (hist->bin_cnt == 0) ||
				 (hist->bin_cnt > MDP_HIST_MAX_BIN)) {
		PR_DISP_ERR("%s fail frame_cnt:%d bin_cnt:%d \n",
			__func__, hist->frame_cnt, hist->bin_cnt);
		return -EINVAL;
	}

	mutex_lock(&mdp_hist_mutex);

	if (!mdp_is_hist_data) {
			PR_DISP_INFO("%s histogram data already stopped\n", __func__);
			ret = -EINVAL;
			goto error;
	}

	if (!mdp_is_hist_start) {
		PR_DISP_INFO("%s histogram already stopped\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	INIT_COMPLETION(mdp_hist_comp);
	mdp_hist_frame_cnt = hist->frame_cnt;
	mutex_unlock(&mdp_hist_mutex);

	ret = wait_for_completion_interruptible(&mdp_hist_comp);
	if(ret < 0)
		return ret;

	mutex_lock(&mdp_hist_mutex);
	if (mdp_is_hist_data)
		ret = mdp_copy_hist_data(hist, mfd);

	if (ret < 0)
		PR_DISP_ERR("%s mdp_copy_hist_data\n", __func__);
error:
	mutex_unlock(&mdp_hist_mutex);
	return ret;
}
#endif

/* Returns < 0 on error, 0 on timeout, or > 0 on successful wait */

int mdp_ppp_pipe_wait(void)
{
	int ret = 1;

	/* wait 5 seconds for the operation to complete before declaring
	the MDP hung */

	if (mdp_ppp_waiting == TRUE) {
		ret = wait_for_completion_interruptible_timeout(&mdp_ppp_comp,
								5 * HZ);

		if (!ret)
			printk(KERN_ERR "%s: Timed out waiting for the MDP.\n",
					__func__);
	}

	return ret;
}

static DEFINE_SPINLOCK(mdp_lock);
static int mdp_irq_mask;
static int mdp_irq_enabled;

/*
 * mdp_enable_irq: can not be called from isr
 */
void mdp_enable_irq(uint32 term)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mdp_lock, irq_flags);
	if (mdp_irq_mask & term) {
		printk(KERN_ERR "MDP IRQ term-0x%x is already set\n", term);
	} else {
		mdp_irq_mask |= term;
		if (mdp_irq_mask && !mdp_irq_enabled) {
			mdp_irq_enabled = 1;
			enable_irq(INT_MDP);
		}
	}
	spin_unlock_irqrestore(&mdp_lock, irq_flags);
}

/*
 * mdp_disable_irq: can not be called from isr
 */
void mdp_disable_irq(uint32 term)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&mdp_lock, irq_flags);
	if (!(mdp_irq_mask & term)) {
		printk(KERN_ERR "MDP IRQ term-0x%x is not set\n", term);
	} else {
		mdp_irq_mask &= ~term;
		if (!mdp_irq_mask && mdp_irq_enabled) {
			mdp_irq_enabled = 0;
			disable_irq(INT_MDP);
		}
	}
	spin_unlock_irqrestore(&mdp_lock, irq_flags);
}

void mdp_disable_irq_nosync(uint32 term)
{
	spin_lock(&mdp_lock);
	if (!(mdp_irq_mask & term)) {
		printk(KERN_ERR "MDP IRQ term-0x%x is not set\n", term);
	} else {
		mdp_irq_mask &= ~term;
		if (!mdp_irq_mask && mdp_irq_enabled) {
			mdp_irq_enabled = 0;
			disable_irq_nosync(INT_MDP);
		}
	}
	spin_unlock(&mdp_lock);
}

void mdp_pipe_kickoff(uint32 term, struct msm_fb_data_type *mfd)
{
	/* complete all the writes before starting */
	wmb();

	/* kick off PPP engine */
	if (term == MDP_PPP_TERM) {
		if (mdp_debug[MDP_PPP_BLOCK])
			jiffies_to_timeval(jiffies, &mdp_ppp_timeval);

		/* let's turn on PPP block */
		mdp_pipe_ctrl(MDP_PPP_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

		mdp_enable_irq(term);
		INIT_COMPLETION(mdp_ppp_comp);
		mdp_ppp_waiting = TRUE;
		outpdw(MDP_BASE + 0x30, 0x1000);
		wait_for_completion_killable(&mdp_ppp_comp);
		mdp_disable_irq(term);

		if (mdp_debug[MDP_PPP_BLOCK]) {
			struct timeval now;

			jiffies_to_timeval(jiffies, &now);
			mdp_ppp_timeval.tv_usec =
			    now.tv_usec - mdp_ppp_timeval.tv_usec;
			MSM_FB_INFO("MDP-PPP: %d\n",
				    (int)mdp_ppp_timeval.tv_usec);
		}
	} else if (term == MDP_DMA2_TERM) {
		if (mdp_debug[MDP_DMA2_BLOCK]) {
			MSM_FB_INFO("MDP-DMA2: %d\n",
				    (int)mdp_dma2_timeval.tv_usec);
			jiffies_to_timeval(jiffies, &mdp_dma2_timeval);
		}
		/* DMA update timestamp */
		mdp_dma2_last_update_time = ktime_get_real();
		/* let's turn on DMA2 block */
#if 0
		mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
#endif
#ifdef CONFIG_MSM_MDP22
		outpdw(MDP_CMD_DEBUG_ACCESS_BASE + 0x0044, 0x0);/* start DMA */
#else
		mdp_lut_enable();

#ifdef CONFIG_MSM_MDP40
		outpdw(MDP_BASE + 0x000c, 0x0);	/* start DMA */
#else
		outpdw(MDP_BASE + 0x0044, 0x0);	/* start DMA */
#endif
#endif
#ifdef CONFIG_MSM_MDP40
	} else if (term == MDP_DMA_S_TERM) {
		mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0010, 0x0);	/* start DMA */
	} else if (term == MDP_DMA_E_TERM) {
		mdp_pipe_ctrl(MDP_DMA_E_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0014, 0x0);	/* start DMA */
	} else if (term == MDP_OVERLAY0_TERM) {
		mdp_pipe_ctrl(MDP_OVERLAY0_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		mdp_lut_enable();
		outpdw(MDP_BASE + 0x0004, 0);
	} else if (term == MDP_OVERLAY1_TERM) {
		mdp_pipe_ctrl(MDP_OVERLAY1_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		mdp_lut_enable();
		outpdw(MDP_BASE + 0x0008, 0);
	}
#else
	} else if (term == MDP_DMA_S_TERM) {
		mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x0048, 0x0);	/* start DMA */
	} else if (term == MDP_DMA_E_TERM) {
		mdp_pipe_ctrl(MDP_DMA_E_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		outpdw(MDP_BASE + 0x004C, 0x0);
	}
#endif
}

static struct platform_device *pdev_list[MSM_FB_MAX_DEV_LIST];
static int pdev_list_cnt;

static void mdp_pipe_ctrl_workqueue_handler(struct work_struct *work)
{
	mdp_pipe_ctrl(MDP_MASTER_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
}
void mdp_pipe_ctrl(MDP_BLOCK_TYPE block, MDP_BLOCK_POWER_STATE state,
		   boolean isr)
{
	boolean mdp_all_blocks_off = TRUE;
	int i;
	unsigned long flag;
	struct msm_fb_panel_data *pdata;

	/*
	 * It is assumed that if isr = TRUE then start = OFF
	 * if start = ON when isr = TRUE it could happen that the usercontext
	 * could turn off the clocks while the interrupt is updating the
	 * power to ON
	 */
	WARN_ON(isr == TRUE && state == MDP_BLOCK_POWER_ON);
	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (MDP_BLOCK_POWER_ON == state) {
		atomic_inc(&mdp_block_power_cnt[block]);

		if (MDP_DMA2_BLOCK == block)
			mdp_in_processing = TRUE;
	} else {
		atomic_dec(&mdp_block_power_cnt[block]);

		if (atomic_read(&mdp_block_power_cnt[block]) < 0) {
			/*
			* Master has to serve a request to power off MDP always
			* It also has a timer to power off.  So, in case of
			* timer expires first and DMA2 finishes later,
			* master has to power off two times
			* There shouldn't be multiple power-off request for
			* other blocks
			*/
			if (block != MDP_MASTER_BLOCK) {
				MSM_FB_INFO("mdp_block_power_cnt[block=%d] \
				multiple power-off request\n", block);
			}
			atomic_set(&mdp_block_power_cnt[block], 0);
		}

		if (MDP_DMA2_BLOCK == block)
			mdp_in_processing = FALSE;
	}
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	/*
	 * If it's in isr, we send our request to workqueue.
	 * Otherwise, processing happens in the current context
	 */
	if (isr) {
		/* checking all blocks power state */
		for (i = 0; i < MDP_MAX_BLOCK; i++) {
			if (atomic_read(&mdp_block_power_cnt[i]) > 0) {
				mdp_all_blocks_off = FALSE;
				break;
			}
		}

		if ((mdp_all_blocks_off) && (mdp_current_clk_on)) {
			/* send workqueue to turn off mdp power */
			queue_delayed_work(mdp_pipe_ctrl_wq,
					   &mdp_pipe_ctrl_worker,
					   mdp_timer_duration);
		}
	} else {
		down(&mdp_pipe_ctrl_mutex);
		/* checking all blocks power state */
		for (i = 0; i < MDP_MAX_BLOCK; i++) {
			if (atomic_read(&mdp_block_power_cnt[i]) > 0) {
				mdp_all_blocks_off = FALSE;
				break;
			}
		}

		/*
		 * find out whether a delayable work item is currently
		 * pending
		 */

		if (delayed_work_pending(&mdp_pipe_ctrl_worker)) {
			/*
			 * try to cancel the current work if it fails to
			 * stop (which means del_timer can't delete it
			 * from the list, it's about to expire and run),
			 * we have to let it run. queue_delayed_work won't
			 * accept the next job which is same as
			 * queue_delayed_work(mdp_timer_duration = 0)
			 */
			cancel_delayed_work(&mdp_pipe_ctrl_worker);
		}

		if ((mdp_all_blocks_off) && (mdp_current_clk_on)) {
			if (block == MDP_MASTER_BLOCK) {
				mdp_current_clk_on = FALSE;
				dsb();
				/* turn off MDP clks */
				mdp_vsync_clk_disable();
				for (i = 0; i < pdev_list_cnt; i++) {
					pdata = (struct msm_fb_panel_data *)
						pdev_list[i]->dev.platform_data;
					if (pdata && pdata->clk_func)
						pdata->clk_func(0);
				}
				if (mdp_clk != NULL) {
					clk_disable(mdp_clk);
					MSM_FB_DEBUG("MDP CLK OFF\n");
				}
				if (mdp_pclk != NULL) {
					clk_disable(mdp_pclk);
					MSM_FB_DEBUG("MDP PCLK OFF\n");
				}
			} else {
				/* send workqueue to turn off mdp power */
				queue_delayed_work(mdp_pipe_ctrl_wq,
						   &mdp_pipe_ctrl_worker,
						   mdp_timer_duration);
			}
		} else if ((!mdp_all_blocks_off) && (!mdp_current_clk_on)) {
			mdp_current_clk_on = TRUE;
			/* turn on MDP clks */
			for (i = 0; i < pdev_list_cnt; i++) {
				pdata = (struct msm_fb_panel_data *)
					pdev_list[i]->dev.platform_data;
				if (pdata && pdata->clk_func)
					pdata->clk_func(1);
			}
			if (mdp_clk != NULL) {
				clk_enable(mdp_clk);
				MSM_FB_DEBUG("MDP CLK ON\n");
			}
			if (mdp_pclk != NULL) {
				clk_enable(mdp_pclk);
				MSM_FB_DEBUG("MDP PCLK ON\n");
			}
			mdp_vsync_clk_enable();
		}
		up(&mdp_pipe_ctrl_mutex);
	}
}

#ifndef CONFIG_MSM_MDP40
irqreturn_t mdp_isr(int irq, void *ptr)
{
	uint32 mdp_interrupt = 0;
	struct mdp_dma_data *dma;

	mdp_is_in_isr = TRUE;
	do {
		mdp_interrupt = inp32(MDP_INTR_STATUS);
		outp32(MDP_INTR_CLEAR, mdp_interrupt);

		mdp_interrupt &= mdp_intr_mask;

		if (mdp_interrupt & TV_ENC_UNDERRUN) {
			mdp_interrupt &= ~(TV_ENC_UNDERRUN);
			mdp_tv_underflow_cnt++;
		}

		if (!mdp_interrupt)
			break;

		/* DMA3 TV-Out Start */
		if (mdp_interrupt & TV_OUT_DMA3_START) {
			/* let's disable TV out interrupt */
			mdp_intr_mask &= ~TV_OUT_DMA3_START;
			outp32(MDP_INTR_ENABLE, mdp_intr_mask);

			dma = &dma3_data;
			if (dma->waiting) {
				dma->waiting = FALSE;
				complete(&dma->comp);
			}
		}
#ifndef CONFIG_MSM_MDP22
		if (mdp_interrupt & MDP_HIST_DONE) {
			outp32(MDP_BASE + 0x94018, 0x3);
			outp32(MDP_INTR_CLEAR, MDP_HIST_DONE);
			complete(&mdp_hist_comp);
		}

		/* LCDC UnderFlow */
		if (mdp_interrupt & LCDC_UNDERFLOW) {
			mdp_lcdc_underflow_cnt++;
			/*when underflow happens HW resets all the histogram
			 registers that were set before so restore them back
			 to normal.*/
			MDP_OUTP(MDP_BASE + 0x94010, 1);
			MDP_OUTP(MDP_BASE + 0x9401c, 2);
			if (mdp_is_hist_start == TRUE) {
				MDP_OUTP(MDP_BASE + 0x94004,
						 mdp_hist_frame_cnt);
				MDP_OUTP(MDP_BASE + 0x94000, 1);
			}
		}
		/* LCDC Frame Start */
		if (mdp_interrupt & LCDC_FRAME_START) {
			/* let's disable LCDC interrupt */
			mdp_intr_mask &= ~LCDC_FRAME_START;
			outp32(MDP_INTR_ENABLE, mdp_intr_mask);

			dma = &dma2_data;
			if (dma->waiting) {
				dma->waiting = FALSE;
				complete(&dma->comp);
			}
		}

		/* DMA2 LCD-Out Complete */
		if (mdp_interrupt & MDP_DMA_S_DONE) {
			dma = &dma_s_data;
			dma->busy = FALSE;
			mdp_pipe_ctrl(MDP_DMA_S_BLOCK, MDP_BLOCK_POWER_OFF,
				      TRUE);
			complete(&dma->comp);
		}
		/* DMA_E LCD-Out Complete */
		if (mdp_interrupt & MDP_DMA_E_DONE) {
			dma = &dma_s_data;
			dma->busy = FALSE;
			mdp_pipe_ctrl(MDP_DMA_E_BLOCK, MDP_BLOCK_POWER_OFF,
				TRUE);
			complete(&dma->comp);
		}

#endif

		/* DMA2 LCD-Out Complete */
		if (mdp_interrupt & MDP_DMA_P_DONE) {
			struct timeval now;
			ktime_t now_k;

			now_k = ktime_get_real();
			mdp_dma2_last_update_time.tv.sec =
			    now_k.tv.sec - mdp_dma2_last_update_time.tv.sec;
			mdp_dma2_last_update_time.tv.nsec =
			    now_k.tv.nsec - mdp_dma2_last_update_time.tv.nsec;

			if (mdp_debug[MDP_DMA2_BLOCK]) {
				jiffies_to_timeval(jiffies, &now);
				mdp_dma2_timeval.tv_usec =
				    now.tv_usec - mdp_dma2_timeval.tv_usec;
			}

			dma = &dma2_data;
			dma->busy = FALSE;
			mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_OFF,
				      TRUE);
			complete(&dma->comp);
		}
		/* PPP Complete */
		if (mdp_interrupt & MDP_PPP_DONE) {
#ifdef	CONFIG_MSM_MDP31
			MDP_OUTP(MDP_BASE + 0x00100, 0xFFFF);
#endif
			mdp_pipe_ctrl(MDP_PPP_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);
			if (mdp_ppp_waiting) {
				mdp_ppp_waiting = FALSE;
				complete(&mdp_ppp_comp);
			}
		}
	} while (1);

	mdp_is_in_isr = FALSE;

	return IRQ_HANDLED;
}
#endif

static void mdp_drv_init(void)
{
	int i;

	for (i = 0; i < MDP_MAX_BLOCK; i++)
		mdp_debug[i] = 0;

	/* initialize spin lock and workqueue */
	spin_lock_init(&mdp_spin_lock);
	spin_lock_init(&mdp_done_lock);
	mdp_dma_wq = create_singlethread_workqueue("mdp_dma_wq");
	mdp_vsync_wq = create_singlethread_workqueue("mdp_vsync_wq");
	mdp_pipe_ctrl_wq = create_singlethread_workqueue("mdp_pipe_ctrl_wq");
	INIT_DELAYED_WORK(&mdp_pipe_ctrl_worker,
			  mdp_pipe_ctrl_workqueue_handler);

	/* initialize semaphore */
	init_completion(&mdp_ppp_comp);
	sema_init(&mdp_ppp_mutex, 1);
	sema_init(&mdp_pipe_ctrl_mutex, 1);

	dma2_data.busy = FALSE;
	dma2_data.dmap_busy = FALSE;
	dma2_data.waiting = FALSE;
	init_completion(&dma2_data.comp);
	init_completion(&dma2_data.dmap_comp);
	sema_init(&dma2_data.mutex, 1);
	mutex_init(&dma2_data.ov_mutex);

	dma3_data.busy = FALSE;
	dma3_data.waiting = FALSE;
	init_completion(&dma3_data.comp);
	sema_init(&dma3_data.mutex, 1);

	dma_s_data.busy = FALSE;
	dma_s_data.waiting = FALSE;
	init_completion(&dma_s_data.comp);
	sema_init(&dma_s_data.mutex, 1);

	dma_e_data.busy = FALSE;
	dma_e_data.waiting = FALSE;
	init_completion(&dma_e_data.comp);
	mutex_init(&dma_e_data.ov_mutex);

#ifndef CONFIG_MSM_MDP22
	init_completion(&mdp_hist_comp);
#endif

	/* initializing mdp power block counter to 0 */
	for (i = 0; i < MDP_MAX_BLOCK; i++)
		atomic_set(&mdp_block_power_cnt[i], 0);

#ifdef MSM_FB_ENABLE_DBGFS
	{
		struct dentry *root;
		char sub_name[] = "mdp";

		root = msm_fb_get_debugfs_root();
		if (root != NULL) {
			mdp_dir = debugfs_create_dir(sub_name, root);

			if (mdp_dir) {
				msm_fb_debugfs_file_create(mdp_dir,
					"dma2_update_time_in_usec",
					(u32 *) &mdp_dma2_update_time_in_usec);
				msm_fb_debugfs_file_create(mdp_dir,
					"vs_rdcnt_slow",
					(u32 *) &mdp_lcd_rd_cnt_offset_slow);
				msm_fb_debugfs_file_create(mdp_dir,
					"vs_rdcnt_fast",
					(u32 *) &mdp_lcd_rd_cnt_offset_fast);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_usec_diff_threshold",
					(u32 *) &mdp_usec_diff_threshold);
				msm_fb_debugfs_file_create(mdp_dir,
					"mdp_current_clk_on",
					(u32 *) &mdp_current_clk_on);
#ifdef CONFIG_FB_MSM_LCDC
				msm_fb_debugfs_file_create(mdp_dir,
					"lcdc_start_x",
					(u32 *) &first_pixel_start_x);
				msm_fb_debugfs_file_create(mdp_dir,
					"lcdc_start_y",
					(u32 *) &first_pixel_start_y);
#endif
			}
		}
	}
#endif
}

int mdp_get_gamma_curvy(struct gamma_curvy *gamma_tbl, struct gamma_curvy *gc, struct mdp_reg *color_enhancement_tbl)
{
	uint32_t *ref_y_gamma;
	uint32_t *ref_y_shade;
	uint32_t *ref_bl_lvl;
	uint32_t *ref_y_lvl;
	int i = 0;
	unsigned int addr, val;
	u16 *r, *g, *b;
	int mdp_lut_i = 0;
	struct fb_cmap cmap;

	ref_y_gamma = gamma_tbl->ref_y_gamma;
	ref_y_shade = gamma_tbl->ref_y_shade;
	ref_bl_lvl = gamma_tbl->ref_bl_lvl;
	ref_y_lvl = gamma_tbl->ref_y_lvl;


	/* size fo ref_Y_gamma should be the same as size of ref_Y_shade*/
	if (sizeof(gc->ref_y_gamma) / 4 != sizeof(gc->ref_y_shade) / 4)
		return -1;

	/* size fo ref_bl_lvl should be the same as size of ref_Y_lvl*/
	if (sizeof(gc->ref_bl_lvl) / 4 != sizeof(gc->ref_y_lvl) / 4)
		return -1;

	gc->gamma_len = sizeof(gc->ref_y_gamma) / 4;
	gc->bl_len = sizeof(gc->ref_bl_lvl) / 4;

	for (i = 0; i < gc->gamma_len; i++) {
		gc->ref_y_gamma[i] = ref_y_gamma[i];
		gc->ref_y_shade[i] = ref_y_shade[i];
	}

	for (i = 0; i < gc->bl_len; i++) {
		gc->ref_bl_lvl[i] = ref_bl_lvl[i];
		gc->ref_y_lvl[i] = ref_y_lvl[i];
	}
	/* get lut */
	cmap = gc->gc_tbl;
	r = cmap.red;
	g = cmap.green;
	b = cmap.blue;

	/* check if lut component is enabled */
	val = inpdw(MDP_BASE + 0x90070);
	val = val & (0x7);
	//if (0x7 == val) {
	if (color_enhancement_tbl) {
		for (i = 0; i < cmap.len; i++) {
			addr = 0x94800 + (0x400 * mdp_lut_i) + cmap.start * 4 + i * 4;

			val = inpdw(MDP_BASE + addr);
			*r++ = (color_enhancement_tbl[i].val & 0xff0000) >> 16;
			*b++ = (color_enhancement_tbl[i].val & 0xff00) >> 8;
			*g++ = color_enhancement_tbl[i].val & 0xff;
		}
	} else {
		for (i = 0; i < cmap.len; i++) {
			*r++ = i;
			*b++ = i;
			*g++ = i;
		}
	}

	gc->gc_tbl = cmap;
	return 0;
}


static int mdp_probe(struct platform_device *pdev);
static int mdp_remove(struct platform_device *pdev);

static int mdp_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int mdp_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static struct dev_pm_ops mdp_dev_pm_ops = {
	.runtime_suspend = mdp_runtime_suspend,
	.runtime_resume = mdp_runtime_resume,
};


static struct platform_driver mdp_driver = {
	.probe = mdp_probe,
	.remove = mdp_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = mdp_suspend,
	.resume = NULL,
#endif
	.shutdown = NULL,
	.driver = {
		/*
		 * Driver name must match the device name added in
		 * platform.c.
		 */
		.name = "mdp",
		.pm = &mdp_dev_pm_ops,
	},
};

static int mdp_off(struct platform_device *pdev)
{
	int ret = 0;

	mdp_histogram_ctrl(FALSE);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = panel_next_off(pdev);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	return ret;
}

static int mdp_on(struct platform_device *pdev)
{
	int ret = 0;
#ifdef CONFIG_MSM_MDP40
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	if (is_mdp4_hw_reset()) {
		mdp4_hw_init();
		outpdw(MDP_BASE + 0x0038, mdp4_display_intf);
	}
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
#endif

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	ret = panel_next_on(pdev);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	return ret;
}

static int mdp_resource_initialized;
static struct msm_panel_common_pdata *mdp_pdata;

uint32 mdp_hw_revision;

/*
 * mdp_hw_revision:
 * 0 == V1
 * 1 == V2
 * 2 == V2.1
 *
 */
void mdp_hw_version(void)
{
	char *cp;
	uint32 *hp;

	if (mdp_pdata == NULL)
		return;

	mdp_hw_revision = MDP4_REVISION_NONE;
	if (mdp_pdata->hw_revision_addr == 0)
		return;

	/* tlmmgpio2 shadow */
	cp = (char *)ioremap(mdp_pdata->hw_revision_addr, 0x16);

	if (cp == NULL)
		return;

	hp = (uint32 *)cp;	/* HW_REVISION_NUMBER */
	mdp_hw_revision = *hp;
	iounmap(cp);

	mdp_hw_revision >>= 28;	/* bit 31:28 */
	mdp_hw_revision &= 0x0f;

	printk(KERN_INFO "%s: mdp_hw_revision=%x\n",
				__func__, mdp_hw_revision);
}

#ifdef CONFIG_MSM_BUS_SCALING
static uint32_t mdp_bus_scale_handle;
int mdp_bus_scale_update_request(uint32_t index)
{
	if (!mdp_pdata || !mdp_pdata->mdp_bus_scale_table
	     || index > (mdp_pdata->mdp_bus_scale_table->num_usecases - 1)) {
		printk(KERN_ERR "%s invalid table or index\n", __func__);
		return -EINVAL;
	}
	if (mdp_bus_scale_handle < 1) {
		printk(KERN_ERR "%s invalid bus handle\n", __func__);
		return -EINVAL;
	}
	return msm_bus_scale_client_update_request(mdp_bus_scale_handle,
							index);
}
#endif
DEFINE_MUTEX(mdp_clk_lock);
int mdp_set_core_clk(uint16 perf_level)
{
	int ret = -EINVAL;
	if (mdp_clk && mdp_pdata
		 && mdp_pdata->mdp_core_clk_table) {
		if (perf_level > mdp_pdata->num_mdp_clk)
			printk(KERN_ERR "%s invalid perf level\n", __func__);
		else {
			mutex_lock(&mdp_clk_lock);
			if (mdp4_extn_disp)
				perf_level = 1;
			ret = clk_set_rate(mdp_clk,
				mdp_pdata->
				mdp_core_clk_table[mdp_pdata->num_mdp_clk
						 - perf_level]);
			mutex_unlock(&mdp_clk_lock);
			if (ret) {
				printk(KERN_ERR "%s unable to set mdp_core_clk rate\n",
					__func__);
			}
		}
	}
	return ret;
}

unsigned long mdp_get_core_clk(void)
{
	unsigned long clk_rate = 0;
	if (mdp_clk) {
		mutex_lock(&mdp_clk_lock);
		clk_rate = clk_get_rate(mdp_clk);
		mutex_unlock(&mdp_clk_lock);
	}

	return clk_rate;
}

unsigned long mdp_perf_level2clk_rate(uint32 perf_level)
{
	unsigned long clk_rate = 0;

	if (mdp_pdata && mdp_pdata->mdp_core_clk_table) {
		if (perf_level > mdp_pdata->num_mdp_clk) {
			printk(KERN_ERR "%s invalid perf level\n", __func__);
			clk_rate = mdp_get_core_clk();
		} else {
			if (mdp4_extn_disp)
				perf_level = 1;
			clk_rate = mdp_pdata->
				mdp_core_clk_table[mdp_pdata->num_mdp_clk - perf_level];
		}
	} else
		clk_rate = mdp_get_core_clk();

	return clk_rate;
}

static int mdp_irq_clk_setup(void)
{
	int ret;

#ifdef CONFIG_MSM_MDP40
	ret = request_irq(INT_MDP, mdp4_isr, IRQF_DISABLED, "MDP", 0);
#else
	ret = request_irq(INT_MDP, mdp_isr, IRQF_DISABLED, "MDP", 0);
#endif
	if (ret) {
		printk(KERN_ERR "mdp request_irq() failed!\n");
		return ret;
	}
	disable_irq(INT_MDP);

	footswitch = regulator_get(NULL, "fs_mdp");
	if (IS_ERR(footswitch))
		footswitch = NULL;
	else
		regulator_enable(footswitch);

	mdp_clk = clk_get(NULL, "mdp_clk");
	if (IS_ERR(mdp_clk)) {
		ret = PTR_ERR(mdp_clk);
		printk(KERN_ERR "can't get mdp_clk error:%d!\n", ret);
		free_irq(INT_MDP, 0);
		return ret;
	}

	mdp_pclk = clk_get(NULL, "mdp_pclk");
	if (IS_ERR(mdp_pclk))
		mdp_pclk = NULL;

#ifdef CONFIG_MSM_MDP40
	/*
	 * mdp_clk should greater than mdp_pclk always
	 */
	if (mdp_pdata && mdp_pdata->mdp_core_clk_rate) {
		mutex_lock(&mdp_clk_lock);
		clk_set_rate(mdp_clk, mdp_pdata->mdp_core_clk_rate);
		mutex_unlock(&mdp_clk_lock);
	}
	printk(KERN_INFO "mdp_clk: mdp_clk=%d\n", (int)clk_get_rate(mdp_clk));
#endif

	return 0;
}

struct mdp_reg mdp_init_color1[] = {
{0x93400, 0x0195, 0x0},
{0x93404, 0x0059, 0x0},
{0x93408, 0x0013, 0x0},
{0x9340C, 0x002D, 0x0},
{0x93410, 0x01C0, 0x0},
{0x93414, 0x0010, 0x0},
{0x93418, 0x002D, 0x0},
{0x9341C, 0x005B, 0x0},
{0x93420, 0x017B, 0x0},
{0x93600, 0x0000, 0x0},
{0x93604, 0x00FF, 0x0},
{0x93608, 0x0000, 0x0},
{0x9360C, 0x00FF, 0x0},
{0x93610, 0x0000, 0x0},
{0x93614, 0x00FF, 0x0},
{0x93680, 0x0000, 0x0},
{0x93684, 0x00FF, 0x0},
{0x93688, 0x0000, 0x0},
{0x9368C, 0x00FF, 0x0},
{0x93690, 0x0000, 0x0},
{0x93694, 0x00FF, 0x0},
{0x90070, 0xCD298008, 0x0},
};

struct mdp_reg mdp_init_color2[] = {
{0x93400, 0x024A, 0x0},
{0x93404, 0xFFC3, 0x0},
{0x93408, 0xFFF6, 0x0},
{0x9340C, 0xFFE0, 0x0},
{0x93410, 0x0229, 0x0},
{0x93414, 0xFFF2, 0x0},
{0x93418, 0xFFDF, 0x0},
{0x9341C, 0xFFC5, 0x0},
{0x93420, 0x025F, 0x0},
{0x93600, 0x0000, 0x0},
{0x93604, 0x00FF, 0x0},
{0x93608, 0x0000, 0x0},
{0x9360C, 0x00FF, 0x0},
{0x93610, 0x0000, 0x0},
{0x93614, 0x00FF, 0x0},
{0x93680, 0x0000, 0x0},
{0x93684, 0x00FF, 0x0},
{0x93688, 0x0000, 0x0},
{0x9368C, 0x00FF, 0x0},
{0x93690, 0x0000, 0x0},
{0x93694, 0x00FF, 0x0},
{0x90070, 0xCD298008, 0x0},
};

static int
mdp_write_reg_mask(uint32_t reg, uint32_t val, uint32_t mask)
{
	uint32_t oldval, newval;

	oldval = inpdw(MDP_BASE + reg);

	oldval &= (~mask);
	val &= mask;
	newval = oldval | val;

	outpdw(MDP_BASE + reg, newval);

	return 0;

}

void mdp_color_enhancement(const struct mdp_reg *reg_seq, int size)
{
	int i;

	printk(KERN_INFO "%s\n", __func__);
	for (i = 0; i < size; i++) {
		if (reg_seq[i].mask == 0x0)
			outpdw(MDP_BASE + reg_seq[i].reg, reg_seq[i].val);
		else
			mdp_write_reg_mask(reg_seq[i].reg, reg_seq[i].val, reg_seq[i].mask);
	}

	return ;
}

#ifdef DEBUG_OVERLAY
int mdp4_overlay_debugfs_init(void);
#endif

static void mdp_do_update_timer(unsigned long data)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)data;
	if (mfd->mdp_pdata && atomic_read(&mfd->mdp_pdata->img_stick_on) == 1) {
		if (mfd->mdp_pdata->mdp_img_stick_wa)
				mfd->mdp_pdata->mdp_img_stick_wa(1);
		atomic_set(&mfd->mdp_pdata->img_stick_on, 0);
	}
}

static int mdp_probe(struct platform_device *pdev)
{
	struct platform_device *msm_fb_dev = NULL;
	struct msm_fb_data_type *mfd;
	struct msm_fb_panel_data *pdata = NULL;
	int rc;
	resource_size_t  size ;
#ifdef CONFIG_MSM_MDP40
	int intf, if_no;
#else
	unsigned long flag;
#endif

	if ((pdev->id == 0) && (pdev->num_resources > 0)) {
		mdp_pdata = pdev->dev.platform_data;

		size =  resource_size(&pdev->resource[0]);
		msm_mdp_base = ioremap(pdev->resource[0].start, size);

		MSM_FB_INFO("MDP HW Base phy_Address = 0x%x virt = 0x%x\n",
			(int)pdev->resource[0].start, (int)msm_mdp_base);

		if (unlikely(!msm_mdp_base))
			return -ENOMEM;

		rc = mdp_irq_clk_setup();

		if (rc)
			return rc;

		mdp_hw_version();

		/* initializing mdp hw */
#ifdef CONFIG_MSM_MDP40
		mdp4_hw_init();
		mdp4_fetch_cfg(clk_get_rate(mdp_clk));
#else
		mdp_hw_init();
#endif

#ifdef CONFIG_FB_MSM_OVERLAY
		mdp_hw_cursor_init();
#endif
		if (mdp_pdata->mdp_color_enhance)
			mdp_pdata->mdp_color_enhance();
		if (mdp_pdata->mdp_gamma)
			mdp_pdata->mdp_gamma();
		if (mdp_pdata->mdp_img_stick_wa)
			mdp_pdata->mdp_img_stick_wa(0);

		mdp_resource_initialized = 1;
		return 0;
	}

	if (!mdp_resource_initialized)
		return -EPERM;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	if (pdev_list_cnt >= MSM_FB_MAX_DEV_LIST)
		return -ENOMEM;

	msm_fb_dev = platform_device_alloc("msm_fb", pdev->id);
	if (!msm_fb_dev)
		return -ENOMEM;

	/* link to the latest pdev */
	mfd->pdev = msm_fb_dev;

	/* add panel data */
	if (platform_device_add_data
	    (msm_fb_dev, pdev->dev.platform_data,
	     sizeof(struct msm_fb_panel_data))) {
		printk(KERN_ERR "mdp_probe: platform_device_add_data failed!\n");
		rc = -ENOMEM;
		goto mdp_probe_err;
	}
	/* data chain */
	pdata = msm_fb_dev->dev.platform_data;
	pdata->on = mdp_on;
	pdata->off = mdp_off;
	pdata->next = pdev;

	switch (mfd->panel.type) {
	case EXT_MDDI_PANEL:
	case MDDI_PANEL:
	case EBI2_PANEL:
		INIT_WORK(&mfd->dma_update_worker,
			  mdp_lcd_update_workqueue_handler);
		INIT_WORK(&mfd->vsync_resync_worker,
			  mdp_vsync_resync_workqueue_handler);
		mfd->hw_refresh = FALSE;

		if (mfd->panel.type == EXT_MDDI_PANEL) {
			/* 15 fps -> 66 msec */
			mfd->refresh_timer_duration = (66 * HZ / 1000);
		} else {
			/* 24 fps -> 42 msec */
			mfd->refresh_timer_duration = (42 * HZ / 1000);
		}

#ifdef CONFIG_MSM_MDP22
		mfd->dma_fnc = mdp_dma2_update;
		mfd->dma = &dma2_data;
#else
		if (mfd->panel_info.pdest == DISPLAY_1) {
#if defined(CONFIG_FB_MSM_OVERLAY) && defined(CONFIG_FB_MSM_MDDI)
			mfd->dma_fnc = mdp4_mddi_overlay;
#else
			mfd->dma_fnc = mdp_dma2_update;
#endif
			mfd->dma = &dma2_data;
			mfd->lut_update = mdp_lut_update_nonlcdc;
			mfd->do_histogram = mdp_do_histogram;
		} else {
			mfd->dma_fnc = mdp_dma_s_update;
			mfd->dma = &dma_s_data;
		}
#endif
		if (mdp_pdata)
			mfd->vsync_gpio = mdp_pdata->gpio;
		else
			mfd->vsync_gpio = -1;

#ifdef CONFIG_MSM_MDP40
		if (mfd->panel.type == EBI2_PANEL)
			intf = EBI2_INTF;
		else
			intf = MDDI_INTF;

		if (mfd->panel_info.pdest == DISPLAY_1)
			if_no = PRIMARY_INTF_SEL;
		else
			if_no = SECONDARY_INTF_SEL;

		mdp4_display_intf_sel(if_no, intf);
#endif
		mdp_config_vsync(mfd);
		break;

#ifdef CONFIG_FB_MSM_MIPI_DSI
	case MIPI_VIDEO_PANEL:
		pdata->on = mdp4_dsi_video_on;
		pdata->off = mdp4_dsi_video_off;
		mfd->hw_refresh = TRUE;
		mfd->dma_fnc = mdp4_dsi_video_overlay;
		if (mfd->panel_info.pdest == DISPLAY_1) {
			if_no = PRIMARY_INTF_SEL;
			mfd->dma = &dma2_data;
		} else {
			if_no = EXTERNAL_INTF_SEL;
			mfd->dma = &dma_e_data;
		}
		mdp4_display_intf_sel(if_no, DSI_VIDEO_INTF);
		break;

	case MIPI_CMD_PANEL:
		mfd->dma_fnc = mdp4_dsi_cmd_overlay;
		if (mfd->panel_info.pdest == DISPLAY_1) {
			if_no = PRIMARY_INTF_SEL;
			mfd->dma = &dma2_data;
		} else {
			if_no = SECONDARY_INTF_SEL;
			mfd->dma = &dma_s_data;
		}
		mdp4_display_intf_sel(if_no, DSI_CMD_INTF);
		mfd->lut_update = mdp_lut_update_nonlcdc;
		mfd->do_histogram = mdp_do_histogram;
		mfd->get_gamma_curvy = mdp_get_gamma_curvy;
		mfd->lut_update = mdp_lut_update_nonlcdc;
		mdp_config_vsync(mfd);
		break;
#endif

#ifdef CONFIG_FB_MSM_DTV
	case DTV_PANEL:
		pdata->on = mdp4_dtv_on;
		pdata->off = mdp4_dtv_off;
		mfd->hw_refresh = TRUE;
		mfd->cursor_update = mdp_hw_cursor_update;
		mfd->dma_fnc = mdp4_dtv_overlay;
		mfd->dma = &dma_e_data;
		mdp4_display_intf_sel(EXTERNAL_INTF_SEL, DTV_INTF);
		break;
#endif
	case HDMI_PANEL:
	case LCDC_PANEL:
		pdata->on = mdp_lcdc_on;
		pdata->off = mdp_lcdc_off;
		mfd->hw_refresh = TRUE;
#if	defined(CONFIG_FB_MSM_OVERLAY) && defined(CONFIG_FB_MSM_MDP40)
		mfd->cursor_update = mdp_hw_cursor_sync_update;
#else
		mfd->cursor_update = mdp_hw_cursor_update;
#endif
#ifndef CONFIG_MSM_MDP22
		mfd->lut_update = mdp_lut_update_lcdc;
		mfd->do_histogram = mdp_do_histogram;
#endif
#ifdef CONFIG_FB_MSM_OVERLAY
		mfd->dma_fnc = mdp4_lcdc_overlay;
#else
		mfd->dma_fnc = mdp_lcdc_update;
#endif

#ifdef CONFIG_MSM_MDP40
		if (mfd->panel.type == HDMI_PANEL) {
			mfd->dma = &dma_e_data;
			mdp4_display_intf_sel(EXTERNAL_INTF_SEL, LCDC_RGB_INTF);
		} else {
			mfd->dma = &dma2_data;
			mdp4_display_intf_sel(PRIMARY_INTF_SEL, LCDC_RGB_INTF);
		}
#else
		mfd->dma = &dma2_data;
		spin_lock_irqsave(&mdp_spin_lock, flag);
		mdp_intr_mask &= ~MDP_DMA_P_DONE;
		outp32(MDP_INTR_ENABLE, mdp_intr_mask);
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
#endif
		break;

	case TV_PANEL:
#if defined(CONFIG_FB_MSM_OVERLAY) && defined(CONFIG_FB_MSM_TVOUT)
		pdata->on = mdp4_atv_on;
		pdata->off = mdp4_atv_off;
		mfd->dma_fnc = mdp4_atv_overlay;
		mfd->dma = &dma_e_data;
		mdp4_display_intf_sel(EXTERNAL_INTF_SEL, TV_INTF);
#else
		pdata->on = mdp_dma3_on;
		pdata->off = mdp_dma3_off;
		mfd->hw_refresh = TRUE;
		mfd->dma_fnc = mdp_dma3_update;
		mfd->dma = &dma3_data;
#endif
		break;

	default:
		printk(KERN_ERR "mdp_probe: unknown device type!\n");
		rc = -ENODEV;
		goto mdp_probe_err;
	}
#ifdef CONFIG_MSM_MDP40
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	mdp4_display_intf = inpdw(MDP_BASE + 0x0038);
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
#endif

#ifdef CONFIG_MSM_BUS_SCALING
	if (!mdp_bus_scale_handle && mdp_pdata &&
		mdp_pdata->mdp_bus_scale_table) {
		mdp_bus_scale_handle =
			msm_bus_scale_register_client(
					mdp_pdata->mdp_bus_scale_table);
		if (!mdp_bus_scale_handle) {
			printk(KERN_ERR "%s not able to get bus scale\n",
				__func__);
			return -ENOMEM;
		}
	}
#endif

	mfd->mdp_pdata = mdp_pdata;

	/* set driver data */
	platform_set_drvdata(msm_fb_dev, mfd);

	rc = platform_device_add(msm_fb_dev);
	if (rc)
		goto mdp_probe_err;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	pdev_list[pdev_list_cnt++] = pdev;
	mdp4_extn_disp = 0;
	if (mdp_pdata && mdp_pdata->mdp_img_stick_wa)
		setup_timer(&mfd->frame_update_timer, mdp_do_update_timer, (unsigned long)mfd);

	init_completion(&mdp_hist_comp);
#ifdef DEBUG_OVERLAY
	mdp4_overlay_debugfs_init();
#endif

	return 0;

mdp_probe_err:
	platform_device_put(msm_fb_dev);
#ifdef CONFIG_MSM_BUS_SCALING
	if (mdp_pdata && mdp_pdata->mdp_bus_scale_table &&
		mdp_bus_scale_handle > 0)
		msm_bus_scale_unregister_client(mdp_bus_scale_handle);
#endif
	return rc;
}

#ifdef CONFIG_PM
static void mdp_suspend_sub(void)
{
	/* cancel pipe ctrl worker */
	cancel_delayed_work(&mdp_pipe_ctrl_worker);

	/* for workder can't be cancelled... */
	flush_workqueue(mdp_pipe_ctrl_wq);

	/* let's wait for PPP completion */
	while (atomic_read(&mdp_block_power_cnt[MDP_PPP_BLOCK]) > 0)
		cpu_relax();

	/* try to power down */
	mdp_pipe_ctrl(MDP_MASTER_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
}
#endif

#if defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static int mdp_suspend(struct platform_device *pdev, pm_message_t state)
{
	mdp_suspend_sub();
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mdp_early_suspend(struct early_suspend *h)
{
	mdp_suspend_sub();
}
#endif

static int mdp_remove(struct platform_device *pdev)
{
	if (footswitch != NULL)
		regulator_put(footswitch);
	iounmap(msm_mdp_base);
	pm_runtime_disable(&pdev->dev);
#ifdef CONFIG_MSM_BUS_SCALING
	if (mdp_pdata && mdp_pdata->mdp_bus_scale_table &&
		mdp_bus_scale_handle > 0)
		msm_bus_scale_unregister_client(mdp_bus_scale_handle);
#endif
	return 0;
}

static int mdp_register_driver(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	early_suspend.suspend = mdp_early_suspend;
	register_early_suspend(&early_suspend);
#ifdef CONFIG_HTC_ONMODE_CHARGING
	onchg_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	onchg_suspend.suspend = mdp_early_suspend;
	register_onchg_suspend(&onchg_suspend);
#endif
#endif

	return platform_driver_register(&mdp_driver);
}

static int __init mdp_driver_init(void)
{
	int ret;

	mdp_drv_init();

	ret = mdp_register_driver();
	if (ret) {
		printk(KERN_ERR "mdp_register_driver() failed!\n");
		return ret;
	}

#if defined(CONFIG_DEBUG_FS)
	mdp_debugfs_init();
#endif

	return 0;

}

module_init(mdp_driver_init);
