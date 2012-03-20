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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/clk.h>
#include <mach/msm_iomap.h>

#include "msm_fb.h"
#include "mipi_dsi.h"

#include <mach/panel_id.h>
#include <mach/debug_display.h>

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
static struct dsi_clk_desc dsicore_clk;
static struct dsi_clk_desc dsi_pclk;
#endif
static int mipi_dsi_probe(struct platform_device *pdev);
static int mipi_dsi_remove(struct platform_device *pdev);

static int mipi_dsi_off(struct platform_device *pdev);
static int mipi_dsi_on(struct platform_device *pdev);

static struct clk *dsi_byte_div_clk;
static struct clk *dsi_esc_clk;
static struct clk *dsi_m_pclk;
static struct clk *dsi_s_pclk;
static struct clk *amp_pclk;

static char *mmss_cc_base;	/* mutimedia sub system clock control */
static char *mmss_sfpb_base;	/* mutimedia sub system sfpb */

static struct platform_device *pdev_list[MSM_FB_MAX_DEV_LIST];
static int pdev_list_cnt;
static struct mipi_dsi_platform_data *mipi_dsi_pdata;

static int vsync_gpio = -1;

static struct platform_driver mipi_dsi_driver = {
	.probe = mipi_dsi_probe,
	.remove = mipi_dsi_remove,
	.shutdown = NULL,
	.driver = {
		   .name = "mipi_dsi",
		   },
};

void mipi_orise_recover_chip(void);

struct device dsi_dev;

/* MIPI_DSI_MRPS, Maximum Return Packet Size */
char max_pktsize[2] = {MIPI_DSI_MRPS, 0x00}; /* LSB tx first, 16 bytes */

int mipi_status = 1;
struct mutex cmdlock;
int bl_level_prevset = 1;
struct dsi_cmd_desc *mipi_power_on_cmd = NULL;
struct dsi_cmd_desc *mipi_power_off_cmd = NULL;
int mipi_power_on_cmd_size = 0;
int mipi_power_off_cmd_size = 0;
char ptype[60] = "Panel Type = ";



#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
static void mipi_dsi_clk(struct dsi_clk_desc *clk, int clk_en)
{
	uint32	*cc, *ns, *md;
	int	pmxo_sel = 0;
	char	mnd_en = 1, root_en = 1;
	uint32	data, val;

	cc = (uint32 *)(mmss_cc_base + 0x004c);
	md = (uint32 *)(mmss_cc_base + 0x0050);
	ns = (uint32 *)(mmss_cc_base + 0x0054);

	if (clk_en) {
		if (clk->mnd_mode == 0) {
			data  = clk->pre_div_func << 14;
			data |= clk->src;
			MIPI_OUTP(ns, data);
			MIPI_OUTP(cc, ((pmxo_sel << 8)
						| (clk->mnd_mode << 6)
						| (root_en << 2) | clk_en));
		} else {
			val = clk->d * 2;
			data = (~val) & 0x0ff;
			data |= clk->m << 8;
			MIPI_OUTP(md, data);

			val = clk->n - clk->m;
			data = (~val) & 0x0ff;
			data <<= 24;
			data |= clk->src;
			MIPI_OUTP(ns, data);
			MIPI_OUTP(cc, ((pmxo_sel << 8)
					      | (clk->mnd_mode << 6)
					      | (mnd_en << 5)
					      | (root_en << 2) | clk_en));
		}

	} else
		MIPI_OUTP(cc, 0);

	wmb();
}

#else

#ifdef DSI_CLK_CALCULATE
static void mipi_dsi_clk(int on, struct dsi_clk_desc *clk)
{
	uint32 *cc, *ns, *md;
	uint32 data, val;

	cc = (uint32 *)(mmss_cc_base + 0x004c);
	md = (uint32 *)(mmss_cc_base + 0x0050);
	ns = (uint32 *)(mmss_cc_base + 0x0054);

	val = clk->d * 2;
	data = (~val) & 0x0ff;
	data |= clk->m << 8;
	MIPI_OUTP(md, data);

	val = clk->n - clk->m;
	data = (~val) & 0x0ff;
	data <<= 24;
	data |= clk->src;
	MIPI_OUTP(ns, data);

	/*
	 * mxo, bypass, mnd_en, root_en, clk_en
	 * */
	MIPI_OUTP(cc, 0x0145);
}
#else
/*
 * mipi_dsi_mxo_selected() -
 *
 *	determine if PXO or MXO should be used as clk_ref for the
 *	DSI PLL.
 *
 *	Note - this function can only be called after clk_enable()
 *	is called for the dsi_esc_clk, as that is what ensures that
 *	the MXO/PXO slection has been made for the dsi_esc_clk.  This
 *	function simply reads what the kernel's clock manager has
 *	configured for dsi_esc_clk.
 *
 *	Returns 1 if MXO should be used, 0 if PXO should be used
 *
 */
static int mipi_dsi_mxo_selected(void)
{
	uint32_t *cc;
	uint32 data;

	cc = (uint32 *)(mmss_cc_base + 0x004c);
	data = MIPI_INP(cc);

	if (data & BIT(14))
		return 1;
	else
		return 0;
}

static void mipi_dsi_clk(int on)
{
	char	*cc, *ns, *md;

	cc = mmss_cc_base + 0x004c;
	md = mmss_cc_base + 0x0050;
	ns = mmss_cc_base + 0x0054;

	if (on) {
		if (mipi_dsi_mxo_selected())
			MIPI_OUTP(cc, 0x125);	/* mxo */
		else
			MIPI_OUTP(cc, 0x25);	/* pxo */

		MIPI_OUTP(md, 0x1fd);
		MIPI_OUTP(ns, 0xff000003);
	} else
		MIPI_OUTP(cc, 0);

	wmb();
}

#endif
#endif

static void mipi_dsi_sfpb_cfg(void)
{
	char *sfpb;
	int data;

	sfpb = mmss_sfpb_base + 0x058;

	data = MIPI_INP(sfpb);
	data |= 0x01800;
	MIPI_OUTP(sfpb, data);
	wmb();
}

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
static void mipi_dsi_pclk(struct dsi_clk_desc *clk, int clk_en)
{
	char	*cc, *ns, *md;
	char	mnd_en = 1, root_en = 1;
	uint32	data, val;

	cc = mmss_cc_base + 0x0130;
	md = mmss_cc_base + 0x0134;
	ns = mmss_cc_base + 0x0138;

	if (clk_en) {
		if (clk->mnd_mode == 0) {
			data  = clk->pre_div_func << 12;
			data |= clk->src;
			MIPI_OUTP(ns, data);
			MIPI_OUTP(cc, ((clk->mnd_mode << 6)
					      | (root_en << 2) | clk_en));
		} else {
			val = clk->d * 2;
			data = (~val) & 0x0ff;
			data |= clk->m << 8;
			MIPI_OUTP(md, data);

			val = clk->n - clk->m;
			data = (~val) & 0x0ff;
			data <<= 24;
			data |= clk->src;
			MIPI_OUTP(ns, data);

			MIPI_OUTP(cc, ((clk->mnd_mode << 6)
					      | (mnd_en << 5)
					      | (root_en << 2) | clk_en));
		}
	} else
		MIPI_OUTP(cc, 0);

	wmb();
}
#else
static void mipi_dsi_pclk(int on)
{
	char	*cc, *ns, *md;

	cc = mmss_cc_base + 0x0130;
	md = mmss_cc_base + 0x0134;
	ns = mmss_cc_base + 0x0138;

	if (on) {
		MIPI_OUTP(cc, 0x2a5);
		MIPI_OUTP(md, 0x1fb);
		MIPI_OUTP(ns, 0xfd0003);
	} else
		MIPI_OUTP(cc, 0);

	wmb();
}
#endif
static void mipi_dsi_ahb_en(void)
{
	char	*ahb;

	ahb = mmss_cc_base + 0x08;

	printk(KERN_INFO "%s: ahb=%x %x\n", __func__, (int) ahb, MIPI_INP(ahb));
}

static void mipi_dsi_calibration(void)
{
	uint32 data;

	MIPI_OUTP(MIPI_DSI_BASE + 0xf4, 0x0000ff11); /* cal_ctrl */
	MIPI_OUTP(MIPI_DSI_BASE + 0xf8, 0x00a105a1); /* cal_hw_ctrl */
	MIPI_OUTP(MIPI_DSI_BASE + 0xf0, 0x01); /* cal_hw_trigger */

	while (1) {
		data = MIPI_INP(MIPI_DSI_BASE + 0xfc); /* cal_status */
		if ((data & 0x10000000) == 0)
			break;

		udelay(10);
	}
}

struct dsiphy_pll_divider_config {
	u32 clk_rate;
	u32 fb_divider;
	u32 ref_divider_ratio;
	u32 bit_clk_divider;	/* oCLK1 */
	u32 byte_clk_divider;	/* oCLK2 */
	u32 dsi_clk_divider;	/* oCLK3 */
};

static const struct dsiphy_pll_divider_config divider_db[] = {
	{ 366000000, 366, 27, 2, 16, 16 }, /* test */
	{ 380000000, 380, 27, 2, 16, 16 }, /* test */
	{ 366000000, 732, 54, 2, 16,  8 },
	{ 380000000, 760, 54, 2, 16,  8 },
	{ 400000000, 400, 27, 2, 16,  8 },
	{ 200000000, 400, 27, 4, 32, 16 },
	{ 500000000, 250, 27, 1,  8,  8 },
	{ 482000000, 482, 27, 2, 16,  8 },
};

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
struct dsi_clk_mnd_table {
	uint8 lanes;
	uint8 bpp;
	uint8 dsiclk_div;
	uint8 dsiclk_m;
	uint8 dsiclk_n;
	uint8 dsiclk_d;
	uint8 pclk_m;
	uint8 pclk_n;
	uint8 pclk_d;
};
#define PREF_DIV_RATIO 27
static struct dsiphy_pll_divider_config pll_divider_config;

static const struct dsi_clk_mnd_table mnd_table[] = {
	{ 1, 2, 8, 1, 1, 0, 1,  2, 0},
	{ 1, 3, 8, 1, 1, 0, 1,  3, 2},
	{ 2, 2, 4, 1, 1, 0, 1,  2, 0},
	{ 2, 3, 4, 1, 1, 0, 1,  3, 2},
	{ 3, 2, 1, 3, 8, 4, 3, 16, 7},
	{ 3, 3, 1, 3, 8, 4, 1,  8, 4},
	{ 4, 2, 2, 1, 1, 0, 1,  2, 0},
	{ 4, 3, 2, 1, 1, 0, 1,  3, 2},
};
#endif

int mipi_dsi_phy_pll_config(u32 clk_rate)
{
	struct dsiphy_pll_divider_config const *dividers = divider_db;
	u32 fb_divider, tmp;

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	dividers = &pll_divider_config;
#else
	/* find the requested divider_db entry */
	for (; dividers != divider_db + ARRAY_SIZE(divider_db); ++dividers) {
		if (dividers->clk_rate == clk_rate)
			break;
	}

	if (dividers == divider_db + ARRAY_SIZE(divider_db)) {
		PR_DISP_ERR("%s: request clk_rate, %u, not supported\n",
			__func__, clk_rate);
		return -EINVAL;
	}
#endif

	/* DSIPHY_PLL_CTRL_x:    1     2     3     8     9     10 */
	/* masks               0xff  0x07  0x3f  0x0f  0xff  0xff */

	/* DSIPHY_PLL_CTRL_1 */
#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	fb_divider = ((dividers->fb_divider) / 2) - 1;
#else
	fb_divider = dividers->fb_divider - 1;
#endif
	MIPI_OUTP(MIPI_DSI_BASE + 0x204, fb_divider & 0xff);

	/* DSIPHY_PLL_CTRL_2 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x208);
	tmp &= ~0x07;
	tmp |= (fb_divider >> 8) & 0x07;
	MIPI_OUTP(MIPI_DSI_BASE + 0x208, tmp);

	/* DSIPHY_PLL_CTRL_3 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x20c);
	tmp &= ~0x3f;
	tmp |= (dividers->ref_divider_ratio - 1) & 0x3f;
	MIPI_OUTP(MIPI_DSI_BASE + 0x20c, tmp);

	/* DSIPHY_PLL_CTRL_8 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x220);
	tmp &= ~0x0f;
	tmp |= (dividers->bit_clk_divider - 1) & 0x0f;
	MIPI_OUTP(MIPI_DSI_BASE + 0x220, tmp);

	/* DSIPHY_PLL_CTRL_9 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x224, (dividers->byte_clk_divider - 1));

	/* DSIPHY_PLL_CTRL_10 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x228, (dividers->dsi_clk_divider - 1));

	return 0;
}

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
int mipi_dsi_clk_div_config(uint8 bpp, uint8 lanes,
			    uint32 *expected_dsi_pclk)
{
	u32 fb_divider, rate, vco;
	u32 div_ratio = 0;
	struct dsi_clk_mnd_table const *mnd_entry = mnd_table;

	if (pll_divider_config.clk_rate == 0)
		pll_divider_config.clk_rate = 454000000;

	rate = pll_divider_config.clk_rate / 1000000; /* In Mhz */

	if (rate < 125) {
		vco = rate * 8;
		div_ratio = 8;
	} else if (rate < 250) {
		vco = rate * 4;
		div_ratio = 4;
	} else if (rate < 500) {
		vco = rate * 2;
		div_ratio = 2;
	} else {
		vco = rate * 1;
		div_ratio = 1;
	}

	/* find the mnd settings from mnd_table entry */
	for (; mnd_entry != mnd_table + ARRAY_SIZE(mnd_table); ++mnd_entry) {
		if (((mnd_entry->lanes) == lanes) &&
			((mnd_entry->bpp) == bpp))
			break;
	}

	if (mnd_entry == mnd_table + ARRAY_SIZE(mnd_table)) {
		PR_DISP_ERR("%s: requested Lanes, %u & BPP, %u, not supported\n",
			__func__, lanes, bpp);
		return -EINVAL;
	}
	fb_divider = ((vco * PREF_DIV_RATIO) / 27);
	pll_divider_config.fb_divider = fb_divider;
	pll_divider_config.ref_divider_ratio = PREF_DIV_RATIO;
	pll_divider_config.bit_clk_divider = div_ratio;
	pll_divider_config.byte_clk_divider =
			pll_divider_config.bit_clk_divider * 8;
	pll_divider_config.dsi_clk_divider =
			(mnd_entry->dsiclk_div) * div_ratio;

	if ((mnd_entry->dsiclk_d == 0)
		|| (mnd_entry->dsiclk_m == 1)) {
		dsicore_clk.mnd_mode = 0;
		dsicore_clk.src = 0x3;
		dsicore_clk.pre_div_func = (mnd_entry->dsiclk_n - 1);
	} else {
		dsicore_clk.mnd_mode = 2;
		dsicore_clk.src = 0x3;
		dsicore_clk.m = mnd_entry->dsiclk_m;
		dsicore_clk.n = mnd_entry->dsiclk_n;
		dsicore_clk.d = mnd_entry->dsiclk_d;
	}
	if ((mnd_entry->pclk_d == 0)
		|| (mnd_entry->pclk_m == 1)) {
		dsi_pclk.mnd_mode = 0;
		dsi_pclk.src = 0x3;
		dsi_pclk.pre_div_func = (mnd_entry->pclk_n - 1);
		*expected_dsi_pclk = ((vco * 1000000) /
					((pll_divider_config.dsi_clk_divider)
					* (mnd_entry->pclk_n)));
	} else {
		dsi_pclk.mnd_mode = 2;
		dsi_pclk.src = 0x3;
		dsi_pclk.m = mnd_entry->pclk_m;
		dsi_pclk.n = mnd_entry->pclk_n;
		dsi_pclk.d = mnd_entry->pclk_d;
		*expected_dsi_pclk = ((vco * 1000000 * dsi_pclk.m) /
					((pll_divider_config.dsi_clk_divider)
					* (mnd_entry->pclk_n)));
	}

	return 0;
}
#endif

void mipi_dsi_phy_init(int panel_ndx, struct msm_panel_info const *panel_info)
{
	struct mipi_dsi_phy_ctrl *pd;
	int i, off;

	MIPI_OUTP(MIPI_DSI_BASE + 0x128, 0x0001);/* start phy sw reset */
	hr_msleep(1);
	MIPI_OUTP(MIPI_DSI_BASE + 0x128, 0x0000);/* end phy w reset */
	MIPI_OUTP(MIPI_DSI_BASE + 0x2cc, 0x0003);/* regulator_ctrl_0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x2d0, 0x0001);/* regulator_ctrl_1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x2d4, 0x0001);/* regulator_ctrl_2 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x2d8, 0x0000);/* regulator_ctrl_3 */
#ifdef DSI_POWER
	MIPI_OUTP(MIPI_DSI_BASE + 0x2dc, 0x0100);/* regulator_ctrl_4 */
#endif

	if (panel_info)
		pd = (panel_info->mipi).dsi_phy_db;
	else
		return;

	off = 0x02cc;	/* regulator ctrl 0 */
	for (i = 0; i < 4; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->regulator[i]);
		wmb();
		off += 4;
	}

	off = 0x0260;	/* phy timig ctrl 0 */
	for (i = 0; i < 11; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->timing[i]);
		wmb();
		off += 4;
	}

	off = 0x0290;	/* ctrl 0 */
	for (i = 0; i < 4; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->ctrl[i]);
		wmb();
		off += 4;
	}

	off = 0x02a0;	/* strength 0 */
	for (i = 0; i < 4; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->strength[i]);
		wmb();
		off += 4;
	}

	off = 0x0204;	/* pll ctrl 1, skip 0 */
	for (i = 1; i < 21; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->pll[i]);
		wmb();
		off += 4;
	}

	/* pll ctrl 0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x200, pd->pll[0]);
	wmb();
	MIPI_OUTP(MIPI_DSI_BASE + 0x200, (pd->pll[0] | 0x01));

	if (panel_info)
		mipi_dsi_phy_pll_config(panel_info->clk_rate);
}

atomic_t dsi_unset_cnt = ATOMIC_INIT(0);
struct completion dsi_unset_comp;

atomic_t need_soft_reset = ATOMIC_INIT(0);
int mipi_dsi_reset_read(void)
{
	return atomic_read(&need_soft_reset);
}

void mipi_dsi_reset_set(int reset)
{
	atomic_set(&need_soft_reset, !!reset);
}

static int mipi_dsi_off(struct platform_device *pdev)
{
	int ret = 0;
	uint32_t wait_time = 0;
	struct msm_fb_data_type *mfd;
	struct msm_panel_info *pinfo;

	mfd = platform_get_drvdata(pdev);
	pinfo = &mfd->panel_info;

	if (atomic_read(&dsi_unset_cnt)) {
		INIT_COMPLETION(dsi_unset_comp);
		wait_time = wait_for_completion_timeout(&dsi_unset_comp, HZ);
		if (!wait_time) {
			PR_DISP_WARN("%s: time out while waiting dsi_unset_comp\n",
				__func__);
		}
	}

	/* change to DSI_CMD_MODE since it needed to
	 * tx DCS dsiplay off comamnd to panel
	 */
	mipi_dsi_op_mode_config(DSI_CMD_MODE);

	if (pinfo->lcd.vsync_enable) {
		if (pinfo->lcd.hw_vsync_mode && vsync_gpio > 0)
			gpio_free(vsync_gpio);

#if 0
		mipi_dsi_set_tear_off();
#endif
	}

	PR_DISP_INFO("%s+ \n", __func__);

	ret = panel_next_off(pdev);

	dsi_mutex_lock();
	dsi_busy_check();

	/* disbale dsi engine */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0000, 0);

	if (mipi_dsi_pdata && mipi_dsi_pdata->dsi_power_save)
		mipi_dsi_pdata->dsi_power_save(0);
#ifdef CONFIG_MSM_BUS_SCALING
	mdp_bus_scale_update_request(0);
#else
	if (mfd->ebi1_clk)
		clk_disable(mfd->ebi1_clk);
#endif

	mipi_dsi_disable_irq();
#if 0 /*remove continuous clock */
	wmb();
	clk_mode  =  MIPI_INP(MIPI_DSI_BASE + 0x00A8);
	MIPI_OUTP(MIPI_DSI_BASE + 0x00A8, clk_mode & ~(1<<28));
	wmb();
#endif

	/* DSIPHY_PLL_CTRL_5 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0214, 0x05f);

	/* DSIPHY_TPA_CTRL_1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0258, 0x08f);

	/* DSIPHY_TPA_CTRL_2 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x025c, 0x001);

	/* DSIPHY_REGULATOR_CTRL_0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x02cc, 0x02);
	/* DSIPHY_CTRL_0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0290, 0x00);

	/* DSIPHY_CTRL_1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0294, 0x7f);

	/* DSIPHY_PLL_CTRL_0, disbale dsi pll */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0200, 0x40);


	/* disbale dsi pll */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0200, 0);

	/* disbale dsi clk */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0118, 0);

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	mipi_dsi_pclk(&dsi_pclk, 0);
	mipi_dsi_clk(&dsicore_clk, 0);
#else
	mipi_dsi_pclk(0);
	mipi_dsi_clk(0);
#endif
	clk_disable(dsi_esc_clk);
	clk_disable(dsi_byte_div_clk);
	clk_disable(dsi_m_pclk);
	clk_disable(dsi_s_pclk);
	clk_disable(amp_pclk); /* clock for AHB-master to AXI */
	dsi_mutex_unlock();

	if (atomic_read(&need_soft_reset) == 1) {
		/* DSI soft reset */
		mipi_dsi_sw_reset();
		atomic_set(&need_soft_reset, 0);
	}
	PR_DISP_INFO("%s-\n", __func__);

	return ret;
}

static int mipi_dsi_on(struct platform_device *pdev)
{
	int ret = 0;
	u32 clk_rate;
	struct msm_fb_data_type *mfd;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct msm_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	u32 hbp, hfp, vbp, vfp, hspw, vspw, width, height;
	u32 ystride, bpp, data;
	static bool bfirsttime = true;
	u32 isr;
	bool sw_reset_status = false;

	isr = MIPI_INP(MIPI_DSI_BASE + 0x010c);
	PR_DISP_INFO("%s+, isr=0x%08x \n", __func__, isr);
	mfd = platform_get_drvdata(pdev);
	fbi = mfd->fbi;
	var = &fbi->var;
	pinfo = &mfd->panel_info;

	MIPI_OUTP(MIPI_DSI_BASE + 0x0000, 1);
	clk_rate = mfd->fbi->var.pixclock;
	clk_rate = min(clk_rate, mfd->panel_info.clk_max);

	if (clk_set_rate(dsi_byte_div_clk, 1) < 0)	/* divided by 1 */
		printk(KERN_ERR "%s: clk_set_rate failed\n",
			__func__);

	clk_enable(amp_pclk); /* clock for AHB-master to AXI */
	clk_enable(dsi_m_pclk);
	clk_enable(dsi_s_pclk);
	clk_enable(dsi_byte_div_clk);
	clk_enable(dsi_esc_clk);

	hbp = var->left_margin;
	hfp = var->right_margin;
	vbp = var->upper_margin;
	vfp = var->lower_margin;
	hspw = var->hsync_len;
	vspw = var->vsync_len;
	width = mfd->panel_info.xres;
	height = mfd->panel_info.yres;

	mipi_dsi_ahb_en();
	mipi_dsi_sfpb_cfg();
#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	mipi_dsi_clk(&dsicore_clk, 1);
	mipi_dsi_pclk(&dsi_pclk, 1);
#else
	mipi_dsi_clk(1);
	mipi_dsi_pclk(1);
#endif

	/* DSIPHY_PLL_CTRL_5 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0214, 0x050);

	/* DSIPHY_TPA_CTRL_1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0258, 0x00f);

	/* DSIPHY_TPA_CTRL_2 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x025c, 0x000);

	mipi_dsi_phy_init(0, &(mfd->panel_info));

	mipi_dsi_enable_irq();

	mipi  = &mfd->panel_info.mipi;

	if (mfd->panel_info.type == MIPI_VIDEO_PANEL) {
		MIPI_OUTP(MIPI_DSI_BASE + 0x20,
			((hspw + hbp + width) << 16 | (hspw + hbp)));
		MIPI_OUTP(MIPI_DSI_BASE + 0x24,
			((vspw + vbp + height) << 16 | (vspw + vbp)));
		MIPI_OUTP(MIPI_DSI_BASE + 0x28,
			(vspw + vbp + height + vfp - 1) << 16 |
				(hspw + hbp + width + hfp - 1));
		MIPI_OUTP(MIPI_DSI_BASE + 0x2c, (hspw << 16));
		MIPI_OUTP(MIPI_DSI_BASE + 0x30, 0);
		MIPI_OUTP(MIPI_DSI_BASE + 0x34, (vspw << 16));

	} else {		/* command mode */
		if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB888)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB666)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB565)
			bpp = 2;
		else
			bpp = 1;

		ystride = width * bpp + 1;

		/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
		data = (ystride << 16) | (mipi->vc << 8) | DTYPE_DCS_LWRITE;
		MIPI_OUTP(MIPI_DSI_BASE + 0x5c, data);
		MIPI_OUTP(MIPI_DSI_BASE + 0x54, data);

		/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
		data = height << 16 | width;
		MIPI_OUTP(MIPI_DSI_BASE + 0x60, data);
		MIPI_OUTP(MIPI_DSI_BASE + 0x58, data);
	}

	mipi  = &mfd->panel_info.mipi;
	mipi_dsi_host_init(mipi);

	/* workaround for orise driver ic to exit ULP mode */
	if (bfirsttime) {
		if ((panel_type == PANEL_ID_SHR_SHARP_OTM_C2) ||
			(panel_type == PANEL_ID_RIR_AUO_OTM_C2)   ||
			(panel_type == PANEL_ID_HOY_SONY_OTM)	||
			(panel_type == PANEL_ID_RIR_AUO_OTM_C3) ||
			(panel_type == PANEL_ID_RIR_AUO_OTM) ||
			(panel_type == PANEL_ID_RIR_SHARP_OTM) ||
			(panel_type == PANEL_ID_SHR_SHARP_OTM)) {
			MIPI_OUTP(MIPI_DSI_BASE + 0x00A8, MIPI_INP(MIPI_DSI_BASE + 0x00A8) | (1<<4)); /* enter */
			wmb();
			MIPI_OUTP(MIPI_DSI_BASE + 0x00A8, MIPI_INP(MIPI_DSI_BASE + 0x00A8) | (1<<12)); /* leave */
		}
		bfirsttime = false;
	}

#if 0 /*remove continuous clock */
	wmb();
	clk_mode  =  MIPI_INP(MIPI_DSI_BASE + 0x00A8);
	MIPI_OUTP(MIPI_DSI_BASE + 0x00A8, clk_mode | (1<<28));
	wmb();
#endif
	if (mipi_dsi_pdata && mipi_dsi_pdata->dsi_power_save)
		mipi_dsi_pdata->dsi_power_save(1);

	sw_reset_status = atomic_read(&need_soft_reset);

	ret = panel_next_on(pdev);

	/* errors happen while sending MIPI commands */
	if (panel_type == PANEL_ID_HOY_SONY_OTM) {
		if (!sw_reset_status && atomic_read(&need_soft_reset)) {
			mipi_dsi_sw_reset();
			atomic_set(&need_soft_reset, 0);
		}
	}

	mipi_dsi_op_mode_config(mipi->mode);

	if (mfd->panel_info.type == MIPI_CMD_PANEL) {
		if (pinfo->lcd.vsync_enable) {
			if (pinfo->lcd.hw_vsync_mode && vsync_gpio > 0) {
				if (gpio_request(vsync_gpio, "MDP_VSYNC") == 0)
					gpio_direction_input(vsync_gpio);
				else
					PR_DISP_ERR("%s: unable to request gpio=%d\n",
						__func__, vsync_gpio);
			}

		mipi_dsi_set_tear_on();
		}
	}

#ifdef CONFIG_MSM_BUS_SCALING
	mdp_bus_scale_update_request(2);
#else
	if (mfd->ebi1_clk)
		clk_enable(mfd->ebi1_clk);
#endif

	PR_DISP_INFO("%s-\n", __func__);

	return ret;
}

static int mipi_dsi_resource_initialized;

static int mipi_dsi_probe(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct fb_info *fbi;
	struct msm_panel_info *pinfo;
	struct platform_device *mdp_dev = NULL;
	struct msm_fb_panel_data *pdata = NULL;
#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	struct mipi_panel_info *mipi;
	uint8 lanes = 0, bpp;
	uint32 h_period, v_period, dsi_pclk_rate;
#endif

	int rc;
	resource_size_t size ;

	if ((pdev->id == 0) && (pdev->num_resources >= 0)) {
		mipi_dsi_pdata = pdev->dev.platform_data;

		size =  resource_size(&pdev->resource[0]);
		mipi_dsi_base =  ioremap(pdev->resource[0].start, size);

		MSM_FB_INFO("mipi_dsi base phy_addr = 0x%x virt = 0x%x\n",
				pdev->resource[0].start, (int) mipi_dsi_base);

		if (!mipi_dsi_base)
			return -ENOMEM;

		mmss_cc_base = MSM_MMSS_CLK_CTL_BASE;

		MSM_FB_INFO("msm_mmss_cc base = 0x%x\n",
				(int) mmss_cc_base);

		if (!mmss_cc_base)
			return -ENOMEM;

		mmss_sfpb_base =  ioremap(MMSS_SFPB_BASE_PHY, 0x100);
		MSM_FB_INFO("mmss_sfpb  base phy_addr = 0x%x virt = 0x%x\n",
				MMSS_SFPB_BASE_PHY, (int) mmss_sfpb_base);

		if (!mmss_sfpb_base)
			return -ENOMEM;

		rc = request_irq(DSI_IRQ, mipi_dsi_isr, IRQF_DISABLED,
						"MIPI_DSI", 0);
		if (rc) {
			printk(KERN_ERR "mipi_dsi_host request_irq() failed!\n");
			return rc;
		}

		disable_irq(DSI_IRQ);

		mipi_dsi_calibration();

		if (mipi_dsi_pdata) {
			vsync_gpio = mipi_dsi_pdata->vsync_gpio;
			PR_DISP_INFO("%s: vsync_gpio=%d\n", __func__, vsync_gpio);
		}

		mipi_dsi_resource_initialized = 1;

		return 0;
	}

	if (!mipi_dsi_resource_initialized)
		return -EPERM;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	if (pdev_list_cnt >= MSM_FB_MAX_DEV_LIST)
		return -ENOMEM;


	mdp_dev = platform_device_alloc("mdp", pdev->id);
	if (!mdp_dev)
		return -ENOMEM;

	/*
	 * link to the latest pdev
	 */
	mfd->pdev = mdp_dev;
	mfd->dest = DISPLAY_LCD;

	/*
	 * alloc panel device data
	 */
	if (platform_device_add_data
	    (mdp_dev, pdev->dev.platform_data,
	     sizeof(struct msm_fb_panel_data))) {
		printk(KERN_ERR "mipi_dsi_probe: platform_device_add_data failed!\n");
		platform_device_put(mdp_dev);
		return -ENOMEM;
	}
	/*
	 * data chain
	 */
	pdata = mdp_dev->dev.platform_data;
	pdata->on = mipi_dsi_on;
	pdata->off = mipi_dsi_off;
	pdata->next = pdev;

	/*
	 * get/set panel specific fb info
	 */
	mfd->panel_info = pdata->panel_info;
	pinfo = &mfd->panel_info;

	if (mfd->index == 0)
		mfd->fb_imgType = MSMFB_DEFAULT_TYPE;
	else
		mfd->fb_imgType = MDP_RGB_565;

	fbi = mfd->fbi;
	fbi->var.pixclock = mfd->panel_info.clk_rate;
	fbi->var.left_margin = mfd->panel_info.lcdc.h_back_porch;
	fbi->var.right_margin = mfd->panel_info.lcdc.h_front_porch;
	fbi->var.upper_margin = mfd->panel_info.lcdc.v_back_porch;
	fbi->var.lower_margin = mfd->panel_info.lcdc.v_front_porch;
	fbi->var.hsync_len = mfd->panel_info.lcdc.h_pulse_width;
	fbi->var.vsync_len = mfd->panel_info.lcdc.v_pulse_width;

#ifdef CONFIG_MSM_DSI_CLK_AUTO_CALCULATE
	 h_period = ((mfd->panel_info.lcdc.h_pulse_width)
			+ (mfd->panel_info.lcdc.h_back_porch)
			+ (mfd->panel_info.xres)
			+ (mfd->panel_info.lcdc.h_front_porch));

	v_period = ((mfd->panel_info.lcdc.v_pulse_width)
			+ (mfd->panel_info.lcdc.v_back_porch)
			+ (mfd->panel_info.yres)
			+ (mfd->panel_info.lcdc.v_front_porch));

	mipi  = &mfd->panel_info.mipi;

	if (mipi->data_lane3)
		lanes += 1;
	if (mipi->data_lane2)
		lanes += 1;
	if (mipi->data_lane1)
		lanes += 1;
	if (mipi->data_lane0)
		lanes += 1;

	if ((mipi->dst_format == DSI_CMD_DST_FORMAT_RGB888)
	    || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB888)
	    || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB666_LOOSE))
		bpp = 3;
	else if ((mipi->dst_format == DSI_CMD_DST_FORMAT_RGB565)
		 || (mipi->dst_format == DSI_VIDEO_DST_FORMAT_RGB565))
		bpp = 2;
	else
		bpp = 3;		/* Default format set to RGB888 */

	if (mfd->panel_info.type == MIPI_VIDEO_PANEL) {
		if (lanes > 0) {
			pll_divider_config.clk_rate =
			((h_period * v_period * (mipi->frame_rate) * bpp * 8)
			   / lanes);
		} else {
			PR_DISP_ERR("%s: forcing mipi_dsi lanes to 1\n", __func__);
			pll_divider_config.clk_rate =
				(h_period * v_period
					 * (mipi->frame_rate) * bpp * 8);
		}
	} else
		pll_divider_config.clk_rate = mfd->panel_info.clk_rate;


	rc = mipi_dsi_clk_div_config(bpp, lanes, &dsi_pclk_rate);
	PR_DISP_INFO("%s:bpp=>%d, lanes=>%d, dsi_pclk_rate=>%d", __func__, bpp, lanes, dsi_pclk_rate);
	if (rc)
		goto mipi_dsi_probe_err;

	if ((dsi_pclk_rate < 3300000) || (dsi_pclk_rate > 103300000))
		dsi_pclk_rate = 35000000;
	mipi->dsi_pclk_rate = dsi_pclk_rate;
#endif

#ifdef DSI_CLK
	clk_rate = mfd->panel_info.clk_max;
	if (clk_set_max_rate(mipi_dsi_clk, clk_rate) < 0)
		printk(KERN_ERR "%s: clk_set_max_rate failed\n", __func__);
	mfd->panel_info.clk_rate = mfd->panel_info.clk_min;
#endif

	/*
	 * set driver data
	 */
	platform_set_drvdata(mdp_dev, mfd);

	/*
	 * register in mdp driver
	 */
	rc = platform_device_add(mdp_dev);
	if (rc)
		goto mipi_dsi_probe_err;

	pdev_list[pdev_list_cnt++] = pdev;

#ifndef CONFIG_MSM_BUS_SCALING
	if (IS_ERR(mfd->ebi1_clk)) {
		rc = PTR_ERR(mfd->ebi1_clk);
		goto mipi_dsi_probe_err;
	}
	clk_set_rate(mfd->ebi1_clk, 122000000);
#endif

	mfd->esd_fixup = mipi_dsi_pdata->esd_fixup;
	mipi_dsi_read_status_reg();

	return 0;

mipi_dsi_probe_err:
	platform_device_put(mdp_dev);
	return rc;
}

static int mipi_dsi_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);
#ifndef CONFIG_MSM_BUS_SCALING
	clk_put(mfd->ebi1_clk);
#endif

	iounmap(msm_pmdh_base);

	return 0;
}

static int mipi_dsi_register_driver(void)
{
	return platform_driver_register(&mipi_dsi_driver);
}

static int __init mipi_dsi_driver_init(void)
{
	int ret;

	amp_pclk = clk_get(NULL, "amp_pclk");
	if (IS_ERR(amp_pclk)) {
		printk(KERN_ERR "can't find amp_pclk\n");
		return PTR_ERR(amp_pclk);
	}

	dsi_m_pclk = clk_get(NULL, "dsi_m_pclk");
	if (IS_ERR(dsi_m_pclk)) {
		printk(KERN_ERR "can't find dsi_m_pclk\n");
		return PTR_ERR(dsi_m_pclk);
	}

	dsi_s_pclk = clk_get(NULL, "dsi_s_pclk");
	if (IS_ERR(dsi_s_pclk)) {
		printk(KERN_ERR "can't find dsi_s_pclk\n");
		return PTR_ERR(dsi_s_pclk);
	}

	dsi_byte_div_clk = clk_get(NULL, "dsi_byte_div_clk");
	if (IS_ERR(dsi_byte_div_clk)) {
		printk(KERN_ERR "can't find dsi_byte_div_clk\n");
		return PTR_ERR(dsi_byte_div_clk);
	}


	dsi_esc_clk = clk_get(NULL, "dsi_esc_clk");
	if (IS_ERR(dsi_esc_clk)) {
		printk(KERN_ERR "can't find dsi_byte_div_clk\n");
		return PTR_ERR(dsi_byte_div_clk);
	}

	ret = mipi_dsi_register_driver();

	device_initialize(&dsi_dev);

	if (ret) {
		clk_disable(amp_pclk);
		clk_put(amp_pclk);
		clk_disable(dsi_m_pclk);
		clk_put(dsi_m_pclk);
		clk_disable(dsi_s_pclk);
		clk_put(dsi_s_pclk);
		clk_disable(dsi_byte_div_clk);
		clk_put(dsi_byte_div_clk);
		clk_disable(dsi_esc_clk);
		clk_put(dsi_esc_clk);
		printk(KERN_ERR "mipi_dsi_register_driver() failed!\n");
		return ret;
	}

	init_completion(&dsi_unset_comp);

	mipi_dsi_init();

	return ret;
}

module_init(mipi_dsi_driver_init);
