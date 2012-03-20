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

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_novatek.h"
#include <mach/debug_display.h>

static struct msm_panel_info pinfo;

static struct mipi_dsi_phy_ctrl dsi_cmd_mode_phy_db = {
/* DSI_BIT_CLK at 482MHz, 2 lane, RGB888 */
		{0x03, 0x01, 0x01, 0x00},	/* regulator */
		/* timing   */
		{0x96, 0x1E, 0x1E, 0x00, 0x3C, 0x3C, 0x1E, 0x28,
		0x0b, 0x13, 0x04},
		{0x7f, 0x00, 0x00, 0x00},	/* phy ctrl */
		{0xee, 0x02, 0x86, 0x00},	/* strength */
		/* pll control */
		{0x41, 0x9c, 0xb9, 0xd6, 0x00, 0x50, 0x48, 0x63,
		0x01, 0x0f, 0x07,
		0x05, 0x14, 0x03, 0x03, 0x03, 0x54, 0x06, 0x10, 0x04, 0x03 },
};

static int __init mipi_cmd_novatek_blue_qhd_pt_init(void)
{
	int ret;

#ifdef CONFIG_FB_MSM_MIPI_PANEL_DETECT
	if (msm_fb_detect_client("mipi_cmd_novatek_qhd"))
		return 0;
	PR_DISP_INFO("panel: mipi_cmd_novatek_qhd\n");
#endif

	pinfo.xres = 540;
	pinfo.yres = 960;
	pinfo.type = MIPI_CMD_PANEL;
	pinfo.pdest = DISPLAY_1;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 24;
	pinfo.lcdc.h_back_porch = 64;
	pinfo.lcdc.h_front_porch = 96;
	pinfo.lcdc.h_pulse_width = 32;
	pinfo.lcdc.v_back_porch = 16;
	pinfo.lcdc.v_front_porch = 16;
	pinfo.lcdc.v_pulse_width = 4;
	pinfo.lcdc.border_clr = 0;	/* blk */
	pinfo.lcdc.underflow_clr = 0xff;	/* blue */
	pinfo.lcdc.hsync_skew = 0;
	pinfo.bl_max = 255;
	pinfo.bl_min = 1;
	pinfo.fb_num = 2;
	pinfo.clk_rate = 482000000;
	pinfo.lcd.vsync_enable = TRUE;
	pinfo.lcd.hw_vsync_mode = TRUE;
	pinfo.lcd.refx100 = 6096; /* adjust refx100 to prevent tearing */

	pinfo.mipi.mode = DSI_CMD_MODE;
	pinfo.mipi.dst_format = DSI_CMD_DST_FORMAT_RGB888;
	pinfo.mipi.vc = 0;
	pinfo.mipi.rgb_swap = DSI_RGB_SWAP_BGR;
	pinfo.mipi.data_lane0 = TRUE;
	pinfo.mipi.data_lane1 = TRUE;
	pinfo.mipi.t_clk_post = 0x0a;
	pinfo.mipi.t_clk_pre = 0x1e;
	pinfo.mipi.stream = 0;	/* dma_p */
	pinfo.mipi.mdp_trigger = DSI_CMD_TRIGGER_SW;
	pinfo.mipi.dma_trigger = DSI_CMD_TRIGGER_SW;
	pinfo.mipi.te_sel = 1; /* TE from vsycn gpio */
	pinfo.mipi.interleave_max = 1;
	pinfo.mipi.insert_dcs_cmd = TRUE;
	pinfo.mipi.wr_mem_continue = 0x3c;
	pinfo.mipi.wr_mem_start = 0x2c;
	pinfo.mipi.dsi_phy_db = &dsi_cmd_mode_phy_db;

	ret = mipi_novatek_device_register(&pinfo, MIPI_DSI_PRIM,
						MIPI_DSI_PANEL_WVGA_PT);
	if (ret)
		PR_DISP_ERR("%s: failed to register device!\n", __func__);

	return ret;
}

module_init(mipi_cmd_novatek_blue_qhd_pt_init);
