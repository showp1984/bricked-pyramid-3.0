/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/fb.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/cacheflush.h>

#include "mdp.h"
#include "msm_fb.h"
#include "mdp4.h"
#include "mipi_dsi.h"
#include <mach/debug_display.h>

static struct mdp4_overlay_pipe *dsi_pipe;
static struct msm_fb_data_type *dsi_mfd;
static atomic_t busy_wait_cnt;

static int vsync_start_y_adjust = 4;

#define OVERLAY_BLT_EMBEDDED
/* #define OVDEBUG 1 */
/* #define BLTDEBUG 1 */
/* #define PADDING_PERFORMANCE */
#define PADDING_ENABLE	2
#define PADDING_DISABLE	3

static int writeback_offset;

static __u32 msm_fb_line_length(__u32 fb_index, __u32 xres, int bpp)
{
	/* The adreno GPU hardware requires that the pitch be aligned to
	   32 pixels for color buffers, so for the cases where the GPU
	   is writing directly to fb0, the framebuffer pitch
	   also needs to be 32 pixel aligned */

	if (fb_index == 0)
		return ALIGN(xres, 32) * bpp;
	else
		return xres * bpp;
}

void dsi_busy_check(void)
{
	if (dsi_mfd && dsi_pipe) {
		mdp4_dsi_cmd_dma_busy_wait(dsi_mfd, dsi_pipe);
		if (dsi_pipe->blt_addr)
			mdp4_dsi_blt_dmap_busy_wait(dsi_mfd);
	}
}

void dsi_mutex_lock(void)
{
	if (dsi_mfd)
		mutex_lock(&dsi_mfd->dma->ov_mutex);
}

void dsi_mutex_unlock(void)
{
	if (dsi_mfd)
		mutex_unlock(&dsi_mfd->dma->ov_mutex);
}

void mdp4_mipi_vsync_enable(struct msm_fb_data_type *mfd,
		struct mdp4_overlay_pipe *pipe, int which)
{
	uint32 start_y, data, tear_en;

	tear_en = (1 << which);

	if ((mfd->use_mdp_vsync) && (mfd->ibuf.vsync_enable) &&
		(mfd->panel_info.lcd.vsync_enable)) {

		if (vsync_start_y_adjust <= pipe->dst_y)
			start_y = pipe->dst_y - vsync_start_y_adjust;
		else
			start_y = (mfd->total_lcd_lines - 1) -
				(vsync_start_y_adjust - pipe->dst_y);
		if (which == 0)
			MDP_OUTP(MDP_BASE + 0x210, start_y);	/* primary */
		else
			MDP_OUTP(MDP_BASE + 0x214, start_y);	/* secondary */

		data = inpdw(MDP_BASE + 0x20c);
		data |= tear_en;
		MDP_OUTP(MDP_BASE + 0x20c, data);
	} else {
		data = inpdw(MDP_BASE + 0x20c);
		data &= ~tear_en;
		MDP_OUTP(MDP_BASE + 0x20c, data);
	}
}

/* maintain virtual address for framebuffer pin pon buffer */
struct {
	unsigned char *fbvir;
	unsigned char *fbphy;
	bool	is_3d;
} fb_addr[2] = { };

void mdp4_overlay_move_padding(struct msm_fb_data_type *mfd, unsigned char *fbvir, bool is_3d)
{
	unsigned int i = 0;
	unsigned int linefor3d = 0;
	unsigned int lineforhalf = 0;
	unsigned int dst_off = 0;
	unsigned int src_off = 0;
	struct fb_info *fbi = mfd->fbi;
#ifdef PADDING_PERFORMANCE
	ktime_t tbegin;
	ktime_t tend;
	int64_t dt;

	tbegin = ktime_get();
#endif

	/* total bytes per line in 3D mode */
	linefor3d = fbi->fix.line_length*2;

	/* total byes per half line */
	lineforhalf = fbi->fix.line_length;

	/* there are 4 pixels shift between L and R frames which are 16 bytes */
	if (is_3d) {
		src_off = lineforhalf;
		dst_off = lineforhalf - 16;
	} else {
		src_off = lineforhalf - 16;
		dst_off = lineforhalf;
	}

	if (fbvir) {
		for (i = 0; i < fbi->var.yres/2; i++) {
			memmove(fbvir+dst_off, fbvir+src_off, fbi->var.xres*4);
			fbvir += linefor3d;
		}
	} else
		PR_DISP_INFO("%s(%d)fail to get virtual address of framebuffer\n", __func__, __LINE__);

#ifdef PADDING_PERFORMANCE
	tend = ktime_get();
	dt = ktime_to_ns(ktime_sub(tend, tbegin));
	PR_DISP_INFO("%s(%d) time %llu\n", __func__, __LINE__, dt);
#endif

}

void mdp4_overlay_handle_padding(struct msm_fb_data_type *mfd, bool is_3d)
{
	int i = 0;
	unsigned char *fbvir = NULL;
	struct fb_info *fbi = mfd->fbi;
	MDPIBUF *iBuf = &mfd->ibuf;
	bool found = false;

	/* get virtual address */
	for (i = 0; i < 2; i++) {
		if (fb_addr[i].fbphy == iBuf->buf) {
			fbvir = (unsigned char *)fb_addr[i].fbvir;
			found = true;
			break;
		} else if (fb_addr[i].fbphy == NULL || fb_addr[i].fbvir == NULL) {
			fb_addr[i].fbphy = (unsigned char *) iBuf->buf;
			fb_addr[i].fbvir = ioremap((unsigned long)fb_addr[i].fbphy, fbi->fix.smem_len/2);
			fbvir = (unsigned char *)fb_addr[i].fbvir;
			found = true;
			break;
		}
	}
	if (!found)
		return;

	if (is_3d) {
		mdp4_overlay_move_padding(mfd, fbvir, is_3d);
		fb_addr[i].is_3d = is_3d;
	} else {
		for (i = 0; i < 2; i++) {
			if (fb_addr[i].is_3d) {
				mdp4_overlay_move_padding(mfd, fb_addr[i].fbvir, is_3d);
				fb_addr[i].is_3d = false;
			}
		}
	}
}

void mdp4_overlay_update_dsi_cmd(struct msm_fb_data_type *mfd)
{
	MDPIBUF *iBuf = &mfd->ibuf;
	struct fb_info *fbi;
	uint8 *src;
	int ptype;
	struct mdp4_overlay_pipe *pipe;
	int ret;
	int bpp;

	if (mfd->key != MFD_KEY)
		return;

	dsi_mfd = mfd;		/* keep it */

	/* MDP cmd block enable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	if (dsi_pipe == NULL) {
		ptype = mdp4_overlay_format2type(mfd->fb_imgType);
		if (ptype < 0)
			printk(KERN_INFO "%s: format2type failed\n", __func__);
		pipe = mdp4_overlay_pipe_alloc(ptype, MDP4_MIXER0, 0);
		if (pipe == NULL) {
			printk(KERN_INFO "%s: pipe_alloc failed\n", __func__);
			return;
		}
		pipe->pipe_used++;
		pipe->mixer_stage  = MDP4_MIXER_STAGE_BASE;
		pipe->mixer_num  = MDP4_MIXER0;
		pipe->src_format = mfd->fb_imgType;
		pipe->is_3d = 0;
		mdp4_overlay_panel_mode(pipe->mixer_num, MDP4_PANEL_DSI_CMD);
		ret = mdp4_overlay_format2pipe(pipe);
		if (ret < 0)
			printk(KERN_INFO "%s: format2type failed\n", __func__);

		dsi_pipe = pipe; /* keep it */

		fbi = mfd->fbi;
		bpp = fbi->var.bits_per_pixel / 8;
		src = (uint8 *) iBuf->buf;
		writeback_offset = mdp4_overlay_writeback_setup(
						fbi, pipe, src, bpp);
#ifdef OVERLAY_BLT_EMBEDDED
		pipe->blt_base = mfd->blt_base;
		PR_DISP_INFO("%s: blt_base=%08x\n", __func__,
			(uint32_t)pipe->blt_base);
#endif
		/*
		 * configure dsi stream id
		 * dma_p = 0, dma_s = 1
		 */
		MDP_OUTP(MDP_BASE + 0x000a0, 0x10);
		/* enable dsi trigger on dma_p */
		MDP_OUTP(MDP_BASE + 0x000a4, 0x01);

		MDP_OUTP(MDP_BASE + 0x0021c, 0x10);
	} else {
		pipe = dsi_pipe;
	}

	/* whole screen for base layer */
	src = (uint8 *) iBuf->buf;

	{
		struct fb_info *fbi;

		fbi = mfd->fbi;
		if (pipe->is_3d) {
			bpp = fbi->var.bits_per_pixel / 8;
			pipe->src_height = pipe->src_height_3d;
			pipe->src_width = pipe->src_width_3d;
			pipe->src_h = pipe->src_height_3d;
			pipe->src_w = pipe->src_width_3d;
			pipe->dst_h = pipe->src_height_3d;
			pipe->dst_w = pipe->src_width_3d;
			pipe->srcp0_ystride = msm_fb_line_length(0, pipe->src_width, bpp);
		} else {
			 /* 2D */
			pipe->src_height = fbi->var.yres;
			pipe->src_width = fbi->var.xres;
			pipe->src_h = fbi->var.yres;
			pipe->src_w = fbi->var.xres;
			pipe->dst_h = fbi->var.yres;
			pipe->dst_w = fbi->var.xres;
			pipe->srcp0_ystride = fbi->fix.line_length;
		}
		pipe->src_y = 0;
		pipe->src_x = 0;
		pipe->dst_y = 0;
		pipe->dst_x = 0;
		pipe->srcp0_addr = (uint32)src;
	}


	mdp4_overlay_rgb_setup(pipe);

	mdp4_mixer_stage_up(pipe);

	mdp4_overlayproc_cfg(pipe);

	mdp4_overlay_dmap_xy(pipe);

	mdp4_overlay_dmap_cfg(mfd, 0);

	mdp4_mipi_vsync_enable(mfd, pipe, 0);

	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	wmb();
}

void mdp4_mipi_read_ptr_intr(void)
{
	printk(KERN_INFO "%s: INTR\n", __func__);
}

void mdp4_dsi_cmd_3d(struct msm_fb_data_type *mfd, struct msmfb_overlay_3d *r3d)
{
	MDPIBUF *iBuf = &mfd->ibuf;
	struct fb_info *fbi;
	struct mdp4_overlay_pipe *pipe;
	int bpp;
	uint8 *src = NULL;

	if (r3d->is_3d == PADDING_ENABLE || r3d->is_3d == PADDING_DISABLE) {
		mfd->enable_uipadding = r3d->is_3d;

		if (r3d->is_3d == PADDING_DISABLE)
			mdp4_overlay_handle_padding(mfd, false);

		return;
	}

	if (dsi_pipe == NULL)
		return;

	PR_DISP_INFO("%s(%d) is3d %d pid=%d\n", __func__, __LINE__, r3d->is_3d, current->pid);

	dsi_pipe->is_3d = r3d->is_3d;
	dsi_pipe->src_height_3d = r3d->height;
	dsi_pipe->src_width_3d = r3d->width;

	src = (uint8 *) iBuf->buf;

	pipe = dsi_pipe;


#ifdef CONFIG_FB_MSM_MIPI_DSI
	if (mfd->panel_power_on)
		mdp4_dsi_cmd_dma_busy_wait(mfd, pipe);
	if (dsi_pipe->blt_addr)
		mdp4_dsi_blt_dmap_busy_wait(mfd);
#endif

	fbi = mfd->fbi;
	if (pipe->is_3d) {
		bpp = fbi->var.bits_per_pixel / 8;
		pipe->src_height = pipe->src_height_3d;
		pipe->src_width = pipe->src_width_3d;
		pipe->src_h = pipe->src_height_3d;
		pipe->src_w = pipe->src_width_3d;
		pipe->dst_h = pipe->src_height_3d;
		pipe->dst_w = pipe->src_width_3d;
		pipe->srcp0_ystride = msm_fb_line_length(0, pipe->src_width, bpp);
	} else {
		 /* 2D */
		pipe->src_height = fbi->var.yres;
		pipe->src_width = fbi->var.xres;
		pipe->src_h = fbi->var.yres;
		pipe->src_w = fbi->var.xres;
		pipe->dst_h = fbi->var.yres;
		pipe->dst_w = fbi->var.xres;
		pipe->srcp0_ystride = fbi->fix.line_length;
	}
	pipe->src_y = 0;
	pipe->src_x = 0;
	pipe->dst_y = 0;
	pipe->dst_x = 0;
	pipe->srcp0_addr = (uint32)src;
	PR_DISP_DEBUG("%s(%d) is3d %d srcx %d srcy %d srcw %d srch %d dstx %d dsty %d dstw %d dsth %d ystride %d\n", __func__, __LINE__, pipe->is_3d,
			pipe->src_x, pipe->src_y, pipe->src_w, pipe->src_h,
			pipe->dst_x, pipe->dst_y, pipe->dst_w, pipe->dst_h, pipe->srcp0_ystride);

	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	mdp4_overlay_rgb_setup(pipe);

	mdp4_mixer_stage_up(pipe);

	mdp4_overlayproc_cfg(pipe);

	mdp4_overlay_dmap_xy(pipe);

	mdp4_overlay_dmap_cfg(mfd, 0);

	mdp4_mipi_vsync_enable(mfd, pipe, 0);

	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	wmb();
}

#ifdef CONFIG_FB_MSM_OVERLAY_WRITEBACK
int mdp4_dsi_overlay_blt_start(struct msm_fb_data_type *mfd)
{
	unsigned long flag;

	if (dsi_pipe->blt_addr == 0) {
	PR_DISP_INFO("%s: blt_end=%d blt_addr=%x pid=%d\n",
	__func__, dsi_pipe->blt_end, (int)dsi_pipe->blt_addr, current->pid);
		mdp4_dsi_cmd_dma_busy_wait(mfd, dsi_pipe);
		spin_lock_irqsave(&mdp_spin_lock, flag);
		dsi_pipe->blt_end = 0;
		dsi_pipe->blt_cnt = 0;
		dsi_pipe->ov_cnt = 0;
		dsi_pipe->dmap_cnt = 0;
		dsi_pipe->blt_addr = dsi_pipe->blt_base;
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
		return 0;
	}

	return -EBUSY;
}

int mdp4_dsi_overlay_blt_stop(struct msm_fb_data_type *mfd)
{
	unsigned long flag;

	if ((dsi_pipe->blt_end == 0) && dsi_pipe->blt_addr) {
		mdp4_dsi_blt_dmap_busy_wait(dsi_mfd);
		PR_DISP_INFO("%s: blt_end=%d blt_addr=%x pid=%d\n",
			__func__, dsi_pipe->blt_end, (int)dsi_pipe->blt_addr, current->pid);
		spin_lock_irqsave(&mdp_spin_lock, flag);
		dsi_pipe->blt_end = 1;	/* mark as end */
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
		return 0;
	}

	return -EBUSY;
}
#endif
#ifdef CONFIG_FB_MSM_OVERLAY_WRITEBACK
int mdp4_dsi_overlay_blt_offset(struct msm_fb_data_type *mfd,
					struct msmfb_overlay_blt *req)
{
	req->offset = writeback_offset;
	req->width = dsi_pipe->src_width;
	req->height = dsi_pipe->src_height;
	req->bpp = dsi_pipe->bpp;

	return sizeof(*req);
}

void mdp4_dsi_overlay_blt(struct msm_fb_data_type *mfd,
					struct msmfb_overlay_blt *req)
{
	if (req->enable)
		mdp4_dsi_overlay_blt_start(mfd);
	else if (req->enable == 0)
		mdp4_dsi_overlay_blt_stop(mfd);
}
#else
int mdp4_dsi_overlay_blt_offset(struct msm_fb_data_type *mfd,
					struct msmfb_overlay_blt *req)
{
	return 0;
}
int mdp4_dsi_overlay_blt_start(struct msm_fb_data_type *mfd)
{
	return -EBUSY;
}
int mdp4_dsi_overlay_blt_stop(struct msm_fb_data_type *mfd)
{
	return -EBUSY;
}
#endif

void mdp4_blt_xy_update(struct mdp4_overlay_pipe *pipe)
{
	uint32 off, addr, addr2;
	int bpp;
	char *overlay_base;


	if (pipe->blt_addr == 0)
		return;


#ifdef BLT_RGB565
	bpp = 2; /* overlay ouput is RGB565 */
#else
	bpp = 3; /* overlay ouput is RGB888 */
#endif
	off = 0;
	if (pipe->dmap_cnt & 0x01)
		off = pipe->src_height * pipe->src_width * bpp;
	addr = pipe->blt_addr + off;

	/* dmap */
	MDP_OUTP(MDP_BASE + 0x90008, addr);

	off = 0;
	if (pipe->ov_cnt & 0x01)
		off = pipe->src_height * pipe->src_width * bpp;
	addr2 = pipe->blt_addr + off;
	/* overlay 0 */
	overlay_base = MDP_BASE + MDP4_OVERLAYPROC0_BASE;/* 0x10000 */
	outpdw(overlay_base + 0x000c, addr2);
	outpdw(overlay_base + 0x001c, addr2);
}


/*
 * mdp4_dmap_done_dsi: called from isr
 * DAM_P_DONE only used when blt enabled
 */
void mdp4_dma_p_done_dsi(struct mdp_dma_data *dma)
{
	int diff;

	spin_lock(&mdp_done_lock);
	mdp_disable_irq_nosync(MDP_DMA2_TERM);  /* disable intr */

	spin_lock(&mdp_spin_lock);
	dsi_pipe->dmap_cnt++;
	diff = dsi_pipe->ov_cnt - dsi_pipe->dmap_cnt;
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: ov_cnt=%d dmap_cnt=%d\n", __func__, dsi_pipe->ov_cnt, dsi_pipe->dmap_cnt);
#endif

	if (diff <= 0) {
		dma->dmap_busy = FALSE;
		dma->dmap_pid = __LINE__;
		complete(&dma->dmap_comp);
		if (dsi_pipe->blt_end) {
			dsi_pipe->blt_end = 0;
			dsi_pipe->blt_addr = 0;
			wmb();
#ifdef BLTDEBUG
			PR_DISP_INFO("%s(%d): END, ov_cnt=%d dmap_cnt=%d\n", __func__, __LINE__, dsi_pipe->ov_cnt, dsi_pipe->dmap_cnt);
#endif
			mdp_intr_mask &= ~INTR_DMA_P_DONE;
			outp32(MDP_INTR_ENABLE, mdp_intr_mask);

		}
		spin_unlock(&mdp_spin_lock);
		spin_unlock(&mdp_done_lock);
		mdp_pipe_ctrl(MDP_OVERLAY0_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);
		return;
	}

	dma->busy = FALSE;
	dma->busy_pid = __LINE__;
	spin_unlock(&mdp_spin_lock);
	complete(&dma->comp);
	if (atomic_read(&busy_wait_cnt))
		atomic_dec(&busy_wait_cnt);

	mdp4_blt_xy_update(dsi_pipe);
	mdp_enable_irq(MDP_DMA2_TERM);	/* enable intr */
	spin_unlock(&mdp_done_lock);

#ifdef BLTDEBUG
	printk(KERN_INFO "%s: kickoff dmap\n", __func__);
#endif
	/* kick off dmap */
	outpdw(MDP_BASE + 0x000c, 0x0);
	wmb();
	/* trigger dsi cmd engine */
	mipi_dsi_cmd_mdp_sw_trigger();
	mdp_pipe_ctrl(MDP_OVERLAY0_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);
}


/*
 * mdp4_overlay0_done_dsi_cmd: called from isr
 */
void mdp4_overlay0_done_dsi_cmd(struct mdp_dma_data *dma)
{
	int diff;

	spin_lock(&mdp_done_lock);
	mdp_disable_irq_nosync(MDP_OVERLAY0_TERM);

	spin_lock(&mdp_spin_lock);
	rmb();
	if (dsi_pipe->blt_addr == 0) {
		dma->busy = FALSE;
		dma->busy_pid = __LINE__;
		complete(&dma->comp);
		if (atomic_read(&busy_wait_cnt))
			atomic_dec(&busy_wait_cnt);
		spin_unlock(&mdp_spin_lock);
		spin_unlock(&mdp_done_lock);
		mdp_pipe_ctrl(MDP_OVERLAY0_BLOCK, MDP_BLOCK_POWER_OFF, TRUE);
		return;
	}

	/* blt enabled */
	if (dsi_pipe->blt_end == 0)
		dsi_pipe->ov_cnt++;
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: ov_cnt=%d dmap_cnt=%d\n", __func__, dsi_pipe->ov_cnt, dsi_pipe->dmap_cnt);
#endif
	if (dsi_pipe->blt_cnt == 0) {
		/* first kickoff since blt enabled */
		mdp_intr_mask |= INTR_DMA_P_DONE;
		outp32(MDP_INTR_ENABLE, mdp_intr_mask);
	}
	dsi_pipe->blt_cnt++;

	diff = dsi_pipe->ov_cnt - dsi_pipe->dmap_cnt;
	if (diff >= 2) {
		PR_DISP_INFO("%s(%d) found diff > 2\n", __func__, __LINE__);
		spin_unlock(&mdp_spin_lock);
		spin_unlock(&mdp_done_lock);
		return;
	}

	dma->busy = FALSE;
	dma->busy_pid = __LINE__;
	dma->dmap_busy = TRUE;
	dma->dmap_pid = (current->pid << 16)+__LINE__;
	complete(&dma->comp);
			if (atomic_read(&busy_wait_cnt))
				atomic_dec(&busy_wait_cnt);

	spin_unlock(&mdp_spin_lock);

	mdp4_blt_xy_update(dsi_pipe);
	mdp_enable_irq(MDP_DMA2_TERM);	/* enable intr */
	wmb();	/* make sure registers updated */
	spin_unlock(&mdp_done_lock);
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: kickoff dmap\n", __func__);
#endif
	/* kick off dmap */
	outpdw(MDP_BASE + 0x000c, 0x0);
	wmb();
	/* trigger dsi cmd engine */
	mipi_dsi_cmd_mdp_sw_trigger();
}

void mdp4_dsi_cmd_overlay_restore(void)
{

#ifdef OVDEBUG
	printk(KERN_INFO "%s: start, pid=%d\n", __func__, current->pid);
#endif
	/* mutex holded by caller */
	if (dsi_mfd && dsi_pipe) {
		mdp4_dsi_cmd_dma_busy_wait(dsi_mfd, dsi_pipe);
		mdp4_overlay_update_dsi_cmd(dsi_mfd);
		/* FIXME: check blt_end flag is needed or not */
		if (dsi_pipe->blt_addr && dsi_pipe->blt_end == 0)
			mdp4_dsi_blt_dmap_busy_wait(dsi_mfd);
		mdp4_dsi_cmd_overlay_kickoff(dsi_mfd, dsi_pipe);
	}
}

void mdp4_dsi_blt_dmap_busy_wait(struct msm_fb_data_type *mfd)
{
	unsigned long flag;
	int need_wait = 0;
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: start pid=%d\n", __func__, current->pid);
#endif
	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (mfd->dma->dmap_busy == TRUE) {
		INIT_COMPLETION(mfd->dma->dmap_comp);
		need_wait++;
	}
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (need_wait) {
		/* wait until DMA finishes the current job */
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: pending pid=%d\n", __func__, current->pid);
#endif
		wait_for_completion(&mfd->dma->dmap_comp);
	}
#ifdef BLTDEBUG
	printk(KERN_INFO "%s: done pid=%d\n", __func__, current->pid);
#endif
}

void mdp4_dsi_cmd_dma_busy_wait(struct msm_fb_data_type *mfd,
				struct mdp4_overlay_pipe *pipe)
{
	unsigned long flag;
	bool need_wait = false;
	long timeout;
	int log_count = 0;

	if (pipe == NULL) /* first time since boot up */
		return;

#ifdef OVDEBUG
	printk(KERN_INFO "%s: start pid=%d\n", __func__, current->pid);
#endif
	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (mfd->dma->busy == TRUE) {
		if (!atomic_read(&busy_wait_cnt))
			INIT_COMPLETION(mfd->dma->comp);
		else
			PR_DISP_INFO("%s(%d)Re-entry dma busy wait, cnt:%d\n", __func__, __LINE__, atomic_read(&busy_wait_cnt));
		need_wait = true;
		atomic_inc(&busy_wait_cnt);
	}
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (need_wait) {
		/* wait until DMA finishes the current job */
#if 1
		timeout = wait_for_completion_timeout(&mfd->dma->comp, HZ/5);

		while (!timeout) {
			rmb();
			if (mfd->dma->busy == FALSE) {
				PR_DISP_INFO("%s(%d)timeout but dma not busy now, cnt:%d\n", __func__, __LINE__, atomic_read(&busy_wait_cnt));
				atomic_dec(&busy_wait_cnt);
				break;
			} else {
				if (log_count++ < 100) {
					PR_DISP_INFO("%s(%d)timeout but dma still busy\n", __func__, __LINE__);
					PR_DISP_INFO("###busy_wait_cnt:%d blt_end:%d blt_cnt:%d ov_cnt:%d dmap_cnt:%d blt_addr:%lu\n",
						atomic_read(&busy_wait_cnt), dsi_pipe->blt_end, dsi_pipe->blt_cnt, dsi_pipe->ov_cnt,
						dsi_pipe->dmap_cnt, dsi_pipe->blt_addr);
				}
				INIT_COMPLETION(mfd->dma->comp);
				timeout = wait_for_completion_timeout(&mfd->dma->comp, HZ/5);
			}
		}
#else
		wait_for_completion(&mfd->dma->comp);
#endif

#ifdef OVDEBUG
		printk(KERN_INFO "%s: pending pid=%d\n", __func__, current->pid);
#endif
		/* wait until DMA finishes the current job */
		mfd->dma_update_flag = 0;
	}
#ifdef OVDEBUG
	printk(KERN_INFO "%s: done pid=%d\n", __func__, current->pid);
#endif
}

void mdp4_dsi_cmd_kickoff_video(struct msm_fb_data_type *mfd,
				struct mdp4_overlay_pipe *pipe)
{
#ifdef OVDEBUG
	printk(KERN_INFO "%s: pid=%d\n", __func__, current->pid);
#endif

	if (dsi_pipe->blt_addr && dsi_pipe->blt_cnt == 0)
		mdp4_overlay_update_dsi_cmd(mfd);

	if (dsi_pipe->blt_addr)
		mdp4_dsi_blt_dmap_busy_wait(dsi_mfd);
	mdp4_dsi_cmd_overlay_kickoff(mfd, pipe);
}

void mdp4_dsi_cmd_kickoff_ui(struct msm_fb_data_type *mfd,
				struct mdp4_overlay_pipe *pipe)
{
#ifdef OVDEBUG
	printk(KERN_INFO "%s: pid=%d\n", __func__, current->pid);
#endif
	mdp4_dsi_cmd_overlay_kickoff(mfd, pipe);
}


void mdp4_dsi_cmd_overlay_kickoff(struct msm_fb_data_type *mfd,
				struct mdp4_overlay_pipe *pipe)
{
	unsigned long flag;

	spin_lock_irqsave(&mdp_spin_lock, flag);
	mdp_enable_irq(MDP_OVERLAY0_TERM);
	mfd->dma->busy = TRUE;
	mfd->dma->busy_pid = (current->pid << 16)+__LINE__;
	if (dsi_pipe->blt_addr) {
		mfd->dma->dmap_busy = TRUE;
		mfd->dma->dmap_pid = (current->pid << 16)+__LINE__;
	}
	wmb();	/* make sure all registers updated */
	spin_unlock_irqrestore(&mdp_spin_lock, flag);
	/* start OVERLAY pipe */
	mdp_pipe_kickoff(MDP_OVERLAY0_TERM, mfd);
	wmb();

	if (pipe->blt_addr == 0) {
		/* trigger dsi cmd engine */
		mipi_dsi_cmd_mdp_sw_trigger();
	}

}

void mdp4_dsi_cmd_overlay(struct msm_fb_data_type *mfd)
{
	if (atomic_read(&ov_unset)) {
		PR_DISP_INFO("%s(%d)found ov unset is called, skip frame update\n", __func__, __LINE__);
		return;
	}
	if (dsi_pipe && dsi_pipe->is_3d) {
		atomic_set(&ov_play, 1);
		if (mfd && mfd->enable_uipadding == PADDING_ENABLE)
			mdp4_overlay_handle_padding(mfd, true);
	}

	if (mfd->esd_fixup) {
		mutex_lock(&mfd->dma->ov_mutex);
		if (mfd && mfd->panel_power_on && dsi_pipe)
			mdp4_dsi_cmd_dma_busy_wait(mfd, dsi_pipe);
		if (dsi_pipe && dsi_pipe->blt_addr)
			mdp4_dsi_blt_dmap_busy_wait(mfd);
		mutex_unlock(&mfd->dma->ov_mutex);
		mfd->esd_fixup((uint32_t)mfd);
	}

	mutex_lock(&mfd->dma->ov_mutex);

	if (mfd && mfd->panel_power_on) {
		mdp4_dsi_cmd_dma_busy_wait(mfd, dsi_pipe);

	if (dsi_pipe && dsi_pipe->blt_addr)
		mdp4_dsi_blt_dmap_busy_wait(mfd);
	mdp4_overlay_update_dsi_cmd(mfd);


	mdp4_dsi_cmd_kickoff_ui(mfd, dsi_pipe);
	mdp4_stat.kickoff_dsi++;

	/* signal if pan function is waiting for the update completion */
		if (mfd->pan_waiting) {
			mfd->pan_waiting = FALSE;
			complete(&mfd->pan_comp);
		}
	}
	/* mask to work-around to fix camera preview issue */
	mdp4_overlay_resource_release();
	mutex_unlock(&mfd->dma->ov_mutex);

	if (dsi_pipe && dsi_pipe->is_3d) {
		atomic_set(&ov_play, 0);
		if (atomic_read(&ov_unset)) {
			PR_DISP_INFO("%s(%d) ov play finished completion ov_comp\n", __func__, __LINE__);
			complete(&ov_comp);
		}
	}
}
