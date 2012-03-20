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
#include <mach/panel_id.h>
#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_himax.h"
#include <mach/debug_display.h>
#include <linux/gpio.h>
#if defined CONFIG_FB_MSM_SELF_REFRESH
#include <linux/wakelock.h>
#ifdef CONFIG_PERFLOCK
#include <mach/perflock.h>
#endif
#endif

/* -----------------------------------------------------------------------------
 *                             Constant value define
 * -----------------------------------------------------------------------------
 */


/* -----------------------------------------------------------------------------
 *                         External routine declaration
 * -----------------------------------------------------------------------------
 */

#define DEFAULT_BRIGHTNESS 83
static struct msm_panel_common_pdata *mipi_himax_pdata;

static struct dsi_buf himax_tx_buf;
static struct dsi_buf himax_rx_buf;

static char enter_sleep[2] = {0x10, 0x00}; /* DTYPE_DCS_WRITE */
static char exit_sleep[2] = {0x11, 0x00}; /* DTYPE_DCS_WRITE */
static char display_off[2] = {0x28, 0x00}; /* DTYPE_DCS_WRITE */

static char set_threelane[3] = {0xBA, 0x12, 0x83}; /* DTYPE_DCS_WRITE-1 */
static char max_pkt_size[2] = {0x03, 0x00};
static char himax_password[4] = {0xB9, 0xFF, 0x83, 0x92}; /* DTYPE_DCS_LWRITE */
static char display_mode_video[2] = {0xC2, 0x03}; /* DTYPE_DCS_WRITE-1 */
static char display_mode_cmd[2] = {0xC2, 0x00}; /* DTYPE_DCS_WRITE */
static char display_mode_cmd_CMI[2] = {0xC2, 0x08}; /* DTYPE_DCS_WRITE */
static char enable_te[2] = {0x35, 0x00};/* DTYPE_DCS_WRITE1 */
static char himax_cc[2] = {0xCC, 0x08}; /* DTYPE_DCS_WRITE-1 */
/* 36h's parameter: [1] 1 -> flip, 0 -> no flip */
static char himax_mx_normal[2] = {0x36, 0x00}; /* DTYPE_DCS_WRITE-1 */

static char bkl_enable_cmd[] = {0x53, 0x24};/* DTYPE_DCS_WRITE1 */ /* bkl on and no dim */
static char bkl_enable_dimming_cmd[] = {0x53, 0x2c};/* DTYPE_DCS_WRITE1 */ /* bkl on and dim */

static char himax_b2[13] = {0xB2, 0x0F, 0xC8, 0x04, 0x0C, 0x04, 0x74, 0x00,
							0xFF, 0x04, 0x0C, 0x04, 0x20}; /* DTYPE_DCS_LWRITE */ /*Set display related register */
static char himax_b4[21] = {0xB4, 0x00, 0x00, 0x05, 0x00, 0x9A, 0x05, 0x06,
							0x95, 0x00, 0x01, 0x06, 0x00, 0x08, 0x08, 0x00,
							0x1D, 0x08, 0x08, 0x08, 0x00}; /* DTYPE_DCS_LWRITE */ /* MPU/Command CYC */

static char himax_CMI_b2[13] = {0xB2, 0x0F, 0xC8, 0x05, 0x0F, 0x08, 0x84, 0x00,
							0xFF, 0x05, 0x0F, 0x04, 0x20}; /* DTYPE_DCS_LWRITE */ /*Set display related register */
static char himax_CMI_b4[21] = {0xB4, 0x00, 0x00, 0x05, 0x00, 0xA0, 0x05, 0x16,
							0x9D, 0x30, 0x03, 0x16, 0x00, 0x03, 0x03, 0x00,
							0x1B, 0x06, 0x07, 0x07, 0x00}; /* DTYPE_DCS_LWRITE */ /* MPU/Command CYC */

static char himax_d8[21] = {0xD8, 0x00, 0x00, 0x05, 0x00, 0x9A, 0x05, 0x06,
							0x95, 0x00, 0x01, 0x06, 0x00, 0x08, 0x08, 0x00,
							0x1D, 0x08, 0x08, 0x08, 0x00}; /* DTYPE_DCS_LWRITE */ /* MPU/Command CYC */
static char himax_CMI_d8[21] = {0xD8, 0x00, 0x00, 0x04, 0x00, 0xA0, 0x04, 0x16,
							0x9D, 0x30, 0x03, 0x16, 0x00, 0x03, 0x03, 0x00,
							0x1B, 0x06, 0x07, 0x07, 0x00}; /* DTYPE_DCS_LWRITE */ /* MPU/Command CYC */

static char himax_d4[2] = {0xD4, 0x0C}; /* DTYPE_DCS_WRITE-1 */

static char himax_d5[22] = {0xD5, 0x00, 0x08, 0x08, 0x00, 0x44, 0x55, 0x66,
							0x77, 0xCC, 0xCC, 0xCC, 0xCC, 0x00, 0x77, 0x66,
							0x55, 0x44, 0xCC, 0xCC, 0xCC, 0xCC}; /* DTYPE_DCS_LWRITE */ /* Set Power */

static char himax_b5[3] = {0xB5, 0xA9, 0x18 };/* DTYPE_DCS_LWRITE */


static char himax_b1[14] = {0xB1, 0x7C, 0x00, 0x44, 0x76, 0x00, 0x12, 0x12,
							0x2A, 0x25, 0x1E, 0x1E, 0x42, 0x72}; /* DTYPE_DCS_LWRITE */ /* Set Power */

static char himax_CMI_b1[14] = {0xB1, 0x7C, 0x00, 0x44, 0x24, 0x00, 0x0D, 0x0D,
							0x12, 0x1F, 0x3F, 0x3F, 0x42, 0x72}; /* DTYPE_DCS_LWRITE */ /* Set Power */

static char himax_bd[4] = {0xBD, 0x00, 0x60, 0xD6}; /* DTYPE_DCS_LWRITE */
static char himax_bf[5] = {0xBF, 0x05, 0x60, 0x02, 0x00}; /* DTYPE_DCS_LWRITE */

static char himax_c6[3] = {0xC6, 0x00, 0x00}; /* DTYPE_DCS_LWRITE */
static char himax_CMI_c7[3] = {0xC7, 0x00, 0x40}; /* DTYPE_DCS_LWRITE */
/* Gamma */
static char himax_e0[35] = {0xE0, 0x00, 0x1D, 0x27, 0x3D, 0x3C, 0x3F, 0x38,
							0x4F, 0x07, 0x0E, 0x0E, 0x10, 0x17, 0x15, 0x15,
							0x16, 0x1F, 0x00, 0x1D, 0x27, 0x3D, 0x3C, 0x3F,
							0x38, 0x4F, 0x07, 0x0E, 0x0E, 0x10, 0x17, 0x15,
							0x15, 0x16, 0x1F};
static char himax_e1[35] = {0xE1, 0x25, 0x30, 0x33, 0x3B, 0x3A, 0x3F, 0x3B,
							0x50, 0x06, 0x0E, 0x0E, 0x10, 0x14, 0x11, 0x13,
							0x15, 0x1E, 0x25, 0x30, 0x33, 0x3B, 0x3A, 0x3F,
							0x3B, 0x50, 0x06, 0x0E, 0x0E, 0x10, 0x14, 0x11,
							0x13, 0x15, 0x1E};
static char himax_e2[35] = {0xE2, 0x2E, 0x34, 0x33, 0x3A, 0x39, 0x3F, 0x39,
							0x4E, 0x07, 0x0D, 0x0E, 0x10, 0x15, 0x11, 0x15,
							0x15, 0x1E, 0x2E, 0x34, 0x33, 0x3A, 0x39, 0x3F,
							0x39, 0x4E, 0x07, 0x0D, 0x0E, 0x10, 0x15, 0x11,
							0x15, 0x15, 0x1E};

static char pwm_freq[] = {0xC9, 0x0F, 0x04, 0x1E, 0x1E,
						  0x00, 0x00, 0x00, 0x10, 0x3E};/* 9.41kHz */

static char led_pwm1[] = {0x51, 0x00};
static char led_pwm1_full[] = {0x51, 0xFF};
static char led_pwm2[] = {0x53, 0x24};
static char led_pwm3[] = {0x55, 0x00}; /* set CABC mode , 0x00=100%, 0x01=UI mode, 0x02= still mode, 0x03= Moving mode*/

static struct dsi_cmd_desc himax_show_logo_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_cmd), display_mode_cmd},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_b2), himax_b2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1_full), led_pwm1_full},
};

static struct dsi_cmd_desc himax_video_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(set_threelane), set_threelane},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0,
		sizeof(max_pkt_size), max_pkt_size},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_mx_normal), himax_mx_normal},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b1), himax_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_b4), himax_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_bd), himax_bd},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_d8), himax_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e0), himax_e0},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e1), himax_e1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e2), himax_e2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_video_on_c2_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(set_threelane), set_threelane},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0,
		sizeof(max_pkt_size), max_pkt_size},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_mx_normal), himax_mx_normal},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b1), himax_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_b4), himax_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_bd), himax_bd},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_d8), himax_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_video_on_c3_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(set_threelane), set_threelane},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0,
		sizeof(max_pkt_size), max_pkt_size},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_CMI_video_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b5), himax_b5},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_bf), himax_bf},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b1), himax_CMI_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b4), himax_CMI_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(set_threelane), set_threelane},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_c7), himax_CMI_c7},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_d5), himax_d5},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_d8), himax_CMI_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_c6), himax_c6},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_CMI_video_on_c25_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_bf), himax_bf},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b1), himax_CMI_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b4), himax_CMI_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(set_threelane), set_threelane},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_c7), himax_CMI_c7},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_d5), himax_d5},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_d8), himax_CMI_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_c6), himax_c6},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_CMI_video_on_c3_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(set_threelane), set_threelane},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_video},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_c7), himax_CMI_c7},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_c6), himax_c6},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_cmd_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(set_threelane), set_threelane},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0,
		sizeof(max_pkt_size), max_pkt_size},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(display_mode_cmd), display_mode_cmd},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(himax_mx_normal), himax_mx_normal},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b1), himax_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b2), himax_b2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_b4), himax_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_d8), himax_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e0), himax_e0},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e1), himax_e1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_e2), himax_e2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};

static struct dsi_cmd_desc himax_CMI_cmd_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 150,
		sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_password), himax_password},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_b5), himax_b5},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b1), himax_CMI_b1},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b2), himax_CMI_b2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_b4), himax_CMI_b4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(set_threelane), set_threelane},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(display_mode_video), display_mode_cmd_CMI},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_c7), himax_CMI_c7},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_cc), himax_cc},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(himax_d4), himax_d4},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_d5), himax_d5},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(himax_CMI_d8), himax_CMI_d8},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 10,
		sizeof(pwm_freq), pwm_freq},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm1), led_pwm1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm2), led_pwm2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 10,
		sizeof(led_pwm3), led_pwm3},
};


static struct dsi_cmd_desc himax_display_off_cmd1[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 1,
		sizeof(display_off), display_off},
};

static struct dsi_cmd_desc himax_display_off_cmd2[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 130,
		sizeof(enter_sleep), enter_sleep}
};

static struct dsi_cmd_desc himax_cmd_backlight_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1,
		sizeof(led_pwm1), led_pwm1},
};

static struct dsi_cmd_desc himax_display_on_cmds[] = {
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_b2), himax_b2},
};

static struct dsi_cmd_desc himax_CMI_display_on_cmds[] = {
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1,
		sizeof(himax_CMI_b2), himax_CMI_b2},
};
static char manufacture_id[2] = {0x04, 0x00}; /* DTYPE_DCS_READ */

static struct dsi_cmd_desc himax_manufacture_id_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id), manufacture_id};

#if 0
static char chip_id[2] = {0xB9, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc himax_chip_id_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(chip_id), chip_id};
#endif
static uint32 mipi_himax_manufacture_id(void)
{
	struct dsi_buf *rp, *tp;
	struct dsi_cmd_desc *cmd;
	uint32 *lp;
	int i;
	char *cp;

	tp = &himax_tx_buf;
	rp = &himax_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &himax_manufacture_id_cmd;
	mipi_dsi_cmds_rx(tp, rp, cmd, 3);


	cp = (char *)rp->data;
	PR_DISP_DEBUG("rx-data: ");
	for (i = 0; i < rp->len; i++, cp++)
		PR_DISP_DEBUG("%x ", *cp);
	PR_DISP_DEBUG("\n");

	lp = (uint32 *)rp->data;

	PR_DISP_DEBUG("%s: manu_id=%x", __func__, *lp);

	return *lp;
}

#if defined CONFIG_FB_MSM_SELF_REFRESH
#define DSI_VIDEO_BASE	0xE0000
static char display_mode_video_cmd_1[2] = {0xC2, 0x0B}; /* DTYPE_DCS_WRITE */
static char display_mode_video_cmd_2[2] = {0xC2, 0x00}; /* DTYPE_DCS_WRITE */
static char himax_vsync_start[2] = {0x00, 0x00}; /* DTYPE_DCS_WRITE */
static char himax_hsync_start[2] = {0x00, 0x00}; /* DTYPE_DCS_WRITE */

static struct dsi_cmd_desc video_to_cmd[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 33,
		sizeof(display_mode_video_cmd_1), display_mode_video_cmd_1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 33,
		sizeof(display_mode_video_cmd_2), display_mode_video_cmd_2},
};

static struct dsi_cmd_desc cmd_to_video[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(display_mode_video), display_mode_video},
};
static struct dsi_cmd_desc vsync_hsync_cmds[] = {
	{DTYPE_VSYNC_START, 1, 0, 0, 0,
		sizeof(himax_vsync_start), himax_vsync_start},
	{DTYPE_HSYNC_START, 1, 0, 0, 0,
		sizeof(himax_hsync_start), himax_hsync_start},
	{DTYPE_VSYNC_START, 1, 0, 0, 0,
		sizeof(himax_vsync_start), himax_vsync_start},
	{DTYPE_HSYNC_START, 1, 0, 0, 0,
		sizeof(himax_hsync_start), himax_hsync_start},
};

static void disable_video_mode_clk(void)
{
	/* Turn off video mode operation */
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 0);
	MIPI_OUTP(MIPI_DSI_BASE + 0x0000, 0x175);
}

static void enable_video_mode_clk(void)
{
	/* Turn on video mode operation */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0000, 0x173);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 1);
}

void mipi_dsi_sw_reset(void);
static void mipi_himax_panel_recover(void)
{
	mipi_dsi_sw_reset();
	if (panel_type == PANEL_ID_VIG_CHIMEI_HX) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_cmds,
			ARRAY_SIZE(himax_CMI_video_on_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_display_on_cmds,
			ARRAY_SIZE(himax_CMI_display_on_cmds));
	} else if (panel_type == PANEL_ID_VIG_CHIMEI_HX_C25) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX_C25\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_c25_cmds,
			ARRAY_SIZE(himax_CMI_video_on_c25_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_display_on_cmds,
			ARRAY_SIZE(himax_CMI_display_on_cmds));
	} else if (panel_type == PANEL_ID_VIG_CHIMEI_HX_C3) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX_C3\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_c3_cmds,
			ARRAY_SIZE(himax_CMI_video_on_c3_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_display_on_cmds,
			ARRAY_SIZE(himax_CMI_display_on_cmds));
	} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C3) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C3\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c3_cmds,
			ARRAY_SIZE(himax_video_on_c3_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_on_cmds,
			ARRAY_SIZE(himax_display_on_cmds));
	} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C25) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C25\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c2_cmds,
			ARRAY_SIZE(himax_video_on_c2_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_on_cmds,
			ARRAY_SIZE(himax_display_on_cmds));
	} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C2) {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C2\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c2_cmds,
			ARRAY_SIZE(himax_video_on_c2_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_on_cmds,
			ARRAY_SIZE(himax_display_on_cmds));
	} else {
		PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX\n");
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_cmds,
			ARRAY_SIZE(himax_video_on_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_on_cmds,
			ARRAY_SIZE(himax_display_on_cmds));
	}

	if (mipi_himax_pdata && mipi_himax_pdata->shrink_pwm)
		led_pwm1[1] = mipi_himax_pdata->shrink_pwm(bl_level_prevset);
	else
		led_pwm1[1] = bl_level_prevset;
	mipi_dsi_cmds_tx(&himax_tx_buf, himax_cmd_backlight_cmds,
			ARRAY_SIZE(himax_cmd_backlight_cmds));
}

static struct wake_lock himax_idle_wake_lock;
#ifdef CONFIG_PERFLOCK
static struct perf_lock himax_perf_lock;
#endif
static DECLARE_WAIT_QUEUE_HEAD(himax_vsync_wait);
static unsigned int vsync_irq;
static int wait_vsync = 0;
static int himax_vsync_gpio = 0;
static void himax_self_refresh_switch(int on)
{
	int vsync_timeout;
	mutex_lock(&cmdlock);

	wake_lock(&himax_idle_wake_lock);
#ifdef CONFIG_PERFLOCK
	if (!is_perf_lock_active(&himax_perf_lock))
		perf_lock(&himax_perf_lock);
#endif
	if (on) {
		mipi_set_tx_power_mode(0);
		mipi_dsi_cmds_tx(&himax_tx_buf, video_to_cmd,
			ARRAY_SIZE(video_to_cmd));
		mipi_set_tx_power_mode(1);
		disable_video_mode_clk();
	} else {
		mipi_set_tx_power_mode(0);
		enable_irq(vsync_irq);
		mipi_dsi_cmds_tx(&himax_tx_buf, cmd_to_video,
			ARRAY_SIZE(cmd_to_video));
		wait_vsync = 1;
		udelay(300);
		vsync_timeout = wait_event_timeout(himax_vsync_wait, himax_vsync_gpio ||
					gpio_get_value(28), HZ/2);
		if (vsync_timeout == 0)
			PR_DISP_DEBUG("Lost vsync!\n");
		disable_irq(vsync_irq);
		wait_vsync = 0;
		himax_vsync_gpio = 0;
		udelay(300);
		mipi_dsi_cmds_tx(&himax_tx_buf, vsync_hsync_cmds,
			ARRAY_SIZE(vsync_hsync_cmds));
		enable_video_mode_clk();
		if (vsync_timeout == 0)
			mipi_himax_panel_recover();
	}
#ifdef CONFIG_PERFLOCK
	if (is_perf_lock_active(&himax_perf_lock))
		perf_unlock(&himax_perf_lock);
#endif
	wake_unlock(&himax_idle_wake_lock);

	PR_DISP_DEBUG("[SR] %d\n", on);
	mutex_unlock(&cmdlock);
}

static irqreturn_t himax_vsync_interrupt(int irq, void *data)
{
	if (wait_vsync) {
		PR_DISP_DEBUG("WV\n");
		himax_vsync_gpio = 1;
		wake_up(&himax_vsync_wait);
	}

	return IRQ_HANDLED;
}

static int setup_vsync(struct platform_device *pdev, int init)
{
	int ret;
	int gpio = 28;

	PR_DISP_INFO("%s+++\n", __func__);

	if (!init) {
		ret = 0;
		goto uninit;
	}
	ret = gpio_request(gpio, "vsync");
	if (ret)
		goto err_request_gpio_failed;

	ret = gpio_direction_input(gpio);
	if (ret)
		goto err_gpio_direction_input_failed;

	ret = vsync_irq = gpio_to_irq(gpio);
	if (ret < 0)
		goto err_get_irq_num_failed;

	ret = request_irq(vsync_irq, himax_vsync_interrupt, IRQF_TRIGGER_RISING,
			  "vsync", pdev);
	if (ret)
		goto err_request_irq_failed;
	PR_DISP_INFO("vsync on gpio %d now %d\n",
	       gpio, gpio_get_value(gpio));
	disable_irq(vsync_irq);

	PR_DISP_INFO("%s---\n", __func__);
	return 0;

uninit:
	free_irq(gpio_to_irq(gpio), pdev);
err_request_irq_failed:
err_get_irq_num_failed:
err_gpio_direction_input_failed:
	gpio_free(gpio);
err_request_gpio_failed:
	return ret;
}
#endif

/* -----------------------------------------------------------------------------
//                         Common Routine Implementation
// -----------------------------------------------------------------------------
*/
static int mipi_himax_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mipi_panel_info *mipi;
	static int turn_on_logo = 1;

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mipi  = &mfd->panel_info.mipi;

	mutex_lock(&cmdlock);
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (!is_perf_lock_active(&himax_perf_lock))
		perf_lock(&himax_perf_lock);
#endif
#endif
	if (mipi->mode == DSI_VIDEO_MODE) {
		PR_DISP_DEBUG("DSI_VIDEO_MODE.%s", __func__);
		if (panel_type == PANEL_ID_VIG_CHIMEI_HX) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_cmds,
				ARRAY_SIZE(himax_CMI_video_on_cmds));
		} else if (panel_type == PANEL_ID_VIG_CHIMEI_HX_C25) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX_C25\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_c25_cmds,
				ARRAY_SIZE(himax_CMI_video_on_c25_cmds));
		} else if (panel_type == PANEL_ID_VIG_CHIMEI_HX_C3) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_CHIMEI_HX_C3\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_video_on_c3_cmds,
				ARRAY_SIZE(himax_CMI_video_on_c3_cmds));
		} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C3) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C3\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c3_cmds,
				ARRAY_SIZE(himax_video_on_c3_cmds));
		} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C25) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C25\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c2_cmds,
				ARRAY_SIZE(himax_video_on_c2_cmds));
		} else if (panel_type == PANEL_ID_VIG_SHARP_HX_C2) {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX_C2\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_c2_cmds,
				ARRAY_SIZE(himax_video_on_c2_cmds));
		} else {
			PR_DISP_DEBUG("Panel type = PANEL_ID_VIG_SHARP_HX\n");
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_video_on_cmds,
				ARRAY_SIZE(himax_video_on_cmds));
		}
		if (turn_on_logo && board_mfg_mode() == 0) {
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_show_logo_cmds,
			ARRAY_SIZE(himax_show_logo_cmds));
			turn_on_logo = 0;
		}
	} else {
		PR_DISP_DEBUG("DSI_CMD_MODE.%s", __func__);
		if (panel_type == PANEL_ID_VIG_CHIMEI_HX) {
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_cmd_on_cmds,
				ARRAY_SIZE(himax_CMI_cmd_on_cmds));
		} else {
			mipi_dsi_cmds_tx(&himax_tx_buf, himax_cmd_on_cmds,
				ARRAY_SIZE(himax_cmd_on_cmds));
		}

		mipi_dsi_cmd_bta_sw_trigger();
		mipi_himax_manufacture_id();
	}
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (is_perf_lock_active(&himax_perf_lock))
		perf_unlock(&himax_perf_lock);
#endif
#endif
	mutex_unlock(&cmdlock);
	return 0;
}

static int mipi_himax_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);
	PR_DISP_DEBUG("%s\n", __func__);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
	mutex_lock(&cmdlock);
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (!is_perf_lock_active(&himax_perf_lock))
		perf_lock(&himax_perf_lock);
#endif
#endif
	mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_off_cmd1,
		ARRAY_SIZE(himax_display_off_cmd1));
	mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_off_cmd2,
		ARRAY_SIZE(himax_display_off_cmd2));
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (is_perf_lock_active(&himax_perf_lock))
		perf_unlock(&himax_perf_lock);
#endif
#endif
	mutex_unlock(&cmdlock);

	return 0;
}

static struct dsi_cmd_desc himax_bkl_enable_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(bkl_enable_cmd), bkl_enable_cmd},
};

static struct dsi_cmd_desc himax_bkl_enable_dimming_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(bkl_enable_dimming_cmd), bkl_enable_dimming_cmd},
};

static int mipi_dsi_set_backlight(struct msm_fb_data_type *mfd)
{
	struct mipi_panel_info *mipi;

	mutex_lock(&cmdlock);
	mipi  = &mfd->panel_info.mipi;
	PR_DISP_DEBUG("bl=%d s=%d\n", mfd->bl_level, mipi_status);
	if (mipi_status == 0)
		goto end;
	if (mipi_himax_pdata && mipi_himax_pdata->shrink_pwm)
		led_pwm1[1] = mipi_himax_pdata->shrink_pwm(mfd->bl_level);
	else
		led_pwm1[1] = (unsigned char)(mfd->bl_level);

	if (mfd->bl_level == 0 || board_mfg_mode() == 4 || board_mfg_mode() == 5)
		led_pwm1[1] = 0;

	if (mipi->mode == DSI_VIDEO_MODE) {
		if (mfd->bl_level == 0)
                        mipi_dsi_cmds_tx(&himax_tx_buf, himax_bkl_enable_cmds,
                                ARRAY_SIZE(himax_bkl_enable_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_cmd_backlight_cmds,
			ARRAY_SIZE(himax_cmd_backlight_cmds));
		if (bl_level_prevset == 0 && mfd->bl_level != 0)
                        mipi_dsi_cmds_tx(&himax_tx_buf, himax_bkl_enable_dimming_cmds,
                                ARRAY_SIZE(himax_bkl_enable_dimming_cmds));
	} else {
		mipi_dsi_op_mode_config(DSI_CMD_MODE);
                if (mfd->bl_level == 0)
                        mipi_dsi_cmds_tx(&himax_tx_buf, himax_bkl_enable_cmds,
                                ARRAY_SIZE(himax_bkl_enable_cmds));
		mipi_dsi_cmds_tx(&himax_tx_buf, himax_cmd_backlight_cmds,
			ARRAY_SIZE(himax_cmd_backlight_cmds));
		if (bl_level_prevset == 0 && mfd->bl_level != 0)
                        mipi_dsi_cmds_tx(&himax_tx_buf, himax_bkl_enable_dimming_cmds,
                                ARRAY_SIZE(himax_bkl_enable_dimming_cmds));
	}
	bl_level_prevset = mfd->bl_level;
end:
	mutex_unlock(&cmdlock);
	return 0;
}

static void mipi_himax_set_backlight(struct msm_fb_data_type *mfd)
{
	int bl_level;

	bl_level = mfd->bl_level;

	if (!mipi_dsi_controller_on())
			return;

	mipi_dsi_set_backlight(mfd);
}

static void mipi_himax_display_on(struct msm_fb_data_type *mfd)
{
	PR_DISP_DEBUG("%s+\n", __func__);

	mutex_lock(&cmdlock);
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (!is_perf_lock_active(&himax_perf_lock))
		perf_lock(&himax_perf_lock);
#endif
#endif
	mipi_dsi_cmds_tx(&himax_tx_buf, himax_display_on_cmds,
		ARRAY_SIZE(himax_display_on_cmds));
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (is_perf_lock_active(&himax_perf_lock))
		perf_unlock(&himax_perf_lock);
#endif
#endif
	mutex_unlock(&cmdlock);
}

static void mipi_himax_cmi_display_on(struct msm_fb_data_type *mfd)
{
	PR_DISP_DEBUG("%s+\n", __func__);

	mutex_lock(&cmdlock);
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (!is_perf_lock_active(&himax_perf_lock))
		perf_lock(&himax_perf_lock);
#endif
#endif
	mipi_dsi_cmds_tx(&himax_tx_buf, himax_CMI_display_on_cmds,
		ARRAY_SIZE(himax_CMI_display_on_cmds));
#if defined CONFIG_FB_MSM_SELF_REFRESH
#ifdef CONFIG_PERFLOCK
	if (is_perf_lock_active(&himax_perf_lock))
		perf_unlock(&himax_perf_lock);
#endif
#endif
	mutex_unlock(&cmdlock);
}

static void mipi_himax_bkl_switch(struct msm_fb_data_type *mfd, bool on)
{
	unsigned int val = 0;

	if (on) {
		mipi_status = 1;
		val = mfd->bl_level;
		if (val == 0) {
			if (bl_level_prevset != 1) {
				val = bl_level_prevset;
				mfd->bl_level = val;
			} else {
				val = DEFAULT_BRIGHTNESS;
				mfd->bl_level = val;
			}
		}
		mipi_dsi_set_backlight(mfd);
	} else {
		mipi_status = 0;
		mfd->bl_level = 0;
		mipi_dsi_set_backlight(mfd);
	}
}

static int mipi_himax_lcd_probe(struct platform_device *pdev)
{
#if defined CONFIG_FB_MSM_SELF_REFRESH
	int ret;
#endif
	if (pdev->id == 0) {
		mipi_himax_pdata = pdev->dev.platform_data;
		mutex_init(&cmdlock);
#if defined CONFIG_FB_MSM_SELF_REFRESH
		ret = setup_vsync(pdev, 1);
		if (ret) {
			dev_err(&pdev->dev, "mipi_himax_setup_vsync failed\n");
			return ret;
		}
		wake_lock_init(&himax_idle_wake_lock, WAKE_LOCK_IDLE, "himax_idle_lock");
#ifdef CONFIG_PERFLOCK
		perf_lock_init(&himax_perf_lock, PERF_LOCK_HIGHEST, "himax");
#endif
#endif
		return 0;
	}

	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mipi_himax_lcd_probe,
	.driver = {
		.name   = "mipi_himax",
	},
};

static struct msm_fb_panel_data himax_panel_data = {
	.on		= mipi_himax_lcd_on,
	.off		= mipi_himax_lcd_off,
	.set_backlight  = mipi_himax_set_backlight,
	.bklswitch	= mipi_himax_bkl_switch,
#if defined CONFIG_FB_MSM_SELF_REFRESH
	.self_refresh_switch = himax_self_refresh_switch,
#endif
};

static int ch_used[3];

int mipi_himax_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_himax", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	himax_panel_data.panel_info = *pinfo;
	if (panel_type == PANEL_ID_VIG_SHARP_HX_C3 ||
		panel_type == PANEL_ID_VIG_SHARP_HX_C25 ||
		panel_type == PANEL_ID_VIG_SHARP_HX_C2 ||
		panel_type == PANEL_ID_VIG_SHARP_HX) {
		himax_panel_data.display_on = mipi_himax_display_on;
	} else if (panel_type == PANEL_ID_VIG_CHIMEI_HX ||
		panel_type == PANEL_ID_VIG_CHIMEI_HX_C25 ||
		panel_type == PANEL_ID_VIG_CHIMEI_HX_C3) {
		himax_panel_data.display_on = mipi_himax_cmi_display_on;
	}

	ret = platform_device_add_data(pdev, &himax_panel_data,
		sizeof(himax_panel_data));
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}
	ret = platform_device_add(pdev);
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}
	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

static int __init mipi_himax_lcd_init(void)
{
	mipi_dsi_buf_alloc(&himax_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&himax_rx_buf, DSI_BUF_SIZE);

	return platform_driver_register(&this_driver);
}

module_init(mipi_himax_lcd_init);
