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
#include <mach/htc_battery_common.h>
#include <linux/gpio.h>
#include <mach/perflock.h>
#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_orise.h"
#include <mach/debug_display.h>

/*#define HTC_USED_0_3_MIPI_INIT*/
/*#define MIPI_READ_DISPLAY_ID	1*/

/*
 *  Constant value define
 */

#define DEFAULT_BRIGHTNESS 83

static struct perf_lock orise_perf_lock;

static struct msm_panel_common_pdata *mipi_orise_pdata;

static struct dsi_buf orise_tx_buf;
static struct dsi_buf orise_rx_buf;

static unsigned char va1[4] = {0x00, 0xe0}; /* DTYPE_DCS_WRITE1 */
static unsigned char va2[4] = {0xc5, 0x61}; /* DTYPE_DCS_WRITE1 */

static unsigned char led_pwm1[] = {0x51, 0x00}; /* DTYPE_DCS_WRITE1, PWM*/
static unsigned char bkl_enable_cmds[] = {0x53, 0x24};/* DTYPE_DCS_WRITE1, bkl on and no dim */
static unsigned char bkl_disable_cmds[] = {0x53, 0x00};/* DTYPE_DCS_WRITE1, bkl off */

static unsigned char pwm_freq_sel_cmds1[] = {0x00, 0xB4}; /* address shift to pwm_freq_sel */
static unsigned char pwm_freq_sel_cmds2[] = {0xC6, 0x00}; /* CABC command with parameter 0 */

static unsigned char pwm_dbf_cmds1[] = {0x00, 0xB1}; /* address shift to PWM DBF */
static unsigned char pwm_dbf_cmds2[] = {0xC6, 0x04}; /* CABC command-- DBF: [2:1], force duty: [0] */

static unsigned char pwm_gate1[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 */ /* addr:0xc680, val:0xf4 */
static unsigned char pwm_gate2[] = {0xc6, 0xF4}; /* DTYPE_DCS_WRITE1 */

/* disable video mdoe */
static unsigned char no_video_mode1[] = {0x00, 0x93};
static unsigned char no_video_mode2[] = {0xB0, 0xB7};
/* disable TE wait VSYNC */
static unsigned char no_wait_te1[] = {0x00, 0xA0};
static unsigned char no_wait_te2[] = {0xC1, 0x00};

static char engmod_1[] = {0xFF, 0x96, 0x01, 0x01}; /* DTYPE_DCS_LWRITE */

static unsigned char engmod_2[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 */

static char engmod_3[] = {0xFF, 0x96, 0x01, 0xFF}; /* DTYPE_DCS_LWRITE */

static char sw_reset[] = {0x01, 0x00}; /* DTYPE_DCS_WRITE */
static char exit_sleep[] = {0x11, 0x00}; /* DTYPE_DCS_WRITE */
static char display_on[] = {0x29, 0x00}; /* DTYPE_DCS_WRITE */

static char enable_te[] = {0x35, 0x00}; /* DTYPE_DCS_WRITE1 */
/* 0x01, 0x68: 3/8 of QHD screen */
static char set_tear_line[] = {0x44, 0x01, 0x68};/* DTYPE_DCS_LWRITE */

static char enter_sleep[] = {0x10, 0x00}; /* DTYPE_DCS_WRITE */
static char display_off[] = {0x28, 0x00}; /* DTYPE_DCS_WRITE */

/* auo */
static char auo_orise_001[] = {
	0xFF, 0x96, 0x01, 0x01,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_orise_002[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 */
static char auo_orise_003[] = {
	0xFF, 0x96, 0x01, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_orise_004[] = {0x00, 0x81}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_005[] = {0xaa, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_006[] = {0x00, 0xa0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_007[] = {0xb3, 0x38}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_008[] = {0x00, 0xa1}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_009[] = {0xb3, 0x18}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_010[] = {0x00, 0xa2}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_011[] = {0xb3, 0x02}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_012[] = {0x00, 0xa3}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_013[] = {0xb3, 0x1c}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_014[] = {0x00, 0xa4}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_015[] = {0xb3, 0x03}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_016[] = {0x00, 0xa5}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_017[] = {0xb3, 0xc0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_c2_01[]  = {0x00, 0xa1}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_c2_02[]  = {0xc0, 0x02}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_018[] = {0x00, 0xb3}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_019[] = {0xc0, 0x10}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_020[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_021[] = {0xc2, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_022[] = {0x00, 0x81}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_023[] = {0xc2, 0x01}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_024[] = {0x00, 0x85}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_025[] = {0xc2, 0x01}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_026[] = {0x00, 0x86}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_027[] = {0xc2, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_028[] = {0x00, 0x87}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_029[] = {0xc2, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_030[] = {0x00, 0x90}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_031[] = {0xc2, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_032[] = {0x00, 0x91}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_033[] = {0xc2, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_034[] = {0x00, 0x92}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_035[] = {0xc2, 0x18}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_036[] = {0x00, 0x93}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_037[] = {0xc2, 0x02}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_c2_03[]  = {0x00, 0xc0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_c2_04[]  = {0xc2, 0xa0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_038[] = {0x00, 0xc3}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_039[] = {0xc2, 0x02}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_040[] = {0x00, 0xc4}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_041[] = {0xc2, 0x0d}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_042[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_043[] = {0xc5, 0x23}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_044[] = {0x00, 0x82}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_045[] = {0xc5, 0x07}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_046[] = {0x00, 0xa3}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_047[] = {0xc5, 0x55}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_047_c2[] = {0xc5, 0x44}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_048[] = {0x00, 0xa0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_049[] = {0xc5, 0xf9}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_050[] = {0x00, 0xa1}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_051[] = {0xc5, 0x5e}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_051_c3[] = {0xc5, 0x6e}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_052[] = {0x00, 0xa2}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_053[] = {0xc5, 0xbf}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_054[] = {0x00, 0xa6}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_055[] = {0xc5, 0x14}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_056[] = {0x00, 0x85}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_057[] = {0xc5, 0x00}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_058[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */
static char auo_orise_059[] = {
	0xd8, 0x78, 0x00, 0x78,
	0x00, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_orise_060[] = {0x00, 0xe0}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_061[] = {0xc5, 0x5b}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_061_c3[] = {0xc5, 0x3b}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_062[] = {0x00, 0x83}; /* DTYPE_DCS_WRITE1 */
static unsigned char auo_orise_063[] = {0xb3, 0x3f}; /* DTYPE_DCS_WRITE1 */

/* gamma 2.2 */
static unsigned char auo_gamma22_00[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */

static char auo_gamma22_01[] = {
	0xe1, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_02[] = {
	0xe2, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_03[] = {
	0xe3, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_04[] = {
	0xe4, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_05[] = {
	0xe5, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_06[] = {
	0xe6, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_07[] = {
	0xe7, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static char auo_gamma22_08[] = {
	0xe8, 0x00, 0x09, 0x0f,
	0x0d, 0x07, 0x13, 0x0c,
	0x0c, 0x01, 0x05, 0x0b,
	0x0e, 0x16, 0x27, 0x1a,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */

static unsigned char auo_gamma22_09[2] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */

/*auo gamma 2.5 */
static unsigned char auo_gamma25_00[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */

static unsigned char auo_gamma25_01[] = {
	0xe1, 0x00, 0x09, 0x10,
	0x10, 0x08, 0x10, 0x0D,
	0x0C, 0x01, 0x05, 0x13,
	0x0F, 0x16, 0x1D, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_02[] = {
	0xe2, 0x00, 0x09, 0x10,
	0x10, 0x08, 0x10, 0x0D,
	0x0C, 0x01, 0x05, 0x13,
	0x0F, 0x16, 0x1D, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_03[] = {
	0xe3, 0x00, 0x09, 0x10,
	0x0C, 0x05, 0x0E, 0x0D,
	0x0C, 0x01, 0x05, 0x11,
	0x0C, 0x15, 0x1D, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_04[] = {
	0xe4, 0x00, 0x09, 0x10,
	0x0C, 0x05, 0x0E, 0x0D,
	0x0C, 0x01, 0x05, 0x11,
	0x0C, 0x15, 0x1D, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_05[] = {
	0xe5, 0x00, 0x09, 0x10,
	0x10, 0x11, 0x0E, 0x0D,
	0x0C, 0x01, 0x05, 0x1E,
	0x14, 0x1E, 0x00, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_06[] = {
	0xE6, 0x00, 0x09, 0x10,
	0x10, 0x11, 0x0E, 0x0D,
	0x0C, 0x01, 0x05, 0x1E,
	0x14, 0x1E, 0x00, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_07[] = {
	0xe7, 0x00, 0x09, 0x10,
	0x10, 0x09, 0x10, 0x0d,
	0x0c, 0x01, 0x05, 0x11,
	0x0f, 0x16, 0x1d, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */
static unsigned char auo_gamma25_08[] = {
	0xe8, 0x00, 0x09, 0x10,
	0x10, 0x09, 0x10, 0x0d,
	0x0c, 0x01, 0x05, 0x11,
	0x0f, 0x16, 0x1d, 0x13,
	0x06, 0xff, 0xff, 0xff,
}; /* DTYPE_DCS_LWRITE */

static unsigned char auo_gamma25_09[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */

static unsigned char sony_orise_001[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 : address shift*/
static unsigned char sony_orise_002[] = {
	0xFF, 0x96, 0x01, 0x01,
}; /* DTYPE_DCS_LWRITE : 0x9600:0x96, 0x9601:0x01, 0x9602:0x01*/

static unsigned char sony_orise_003[] = {0x00, 0x80}; /* DTYPE_DCS_WRITE1 : address shift*/
static unsigned char sony_orise_004[] = {
	0xFF, 0x96, 0x01,
}; /* DTYPE_DCS_LWRITE : 0xFF80:0x96, 0xFF81:0x1 */

static unsigned char sony_gamma28_00[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_01[] = {
	0xe1, 0x07, 0x15, 0x1b,
	0x0e, 0x06, 0x0f, 0x0a,
	0x09, 0x05, 0x09, 0x0e,
	0x07, 0x0d, 0x0a, 0x05,
	0x00
}; /* DTYPE_DCS_LWRITE :0xE100:0x11, 0xE101:0x19, 0xE102: 0x1e, ..., 0xff are padding for 4 bytes*/

static unsigned char sony_gamma28_02[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_03[] = {
	0xe2, 0x07, 0x15, 0x1b,
	0x0d, 0x06, 0x0f, 0x0a,
	0x09, 0x05, 0x09, 0x0e,
	0x07, 0x0d, 0x0a, 0x05,
	0x00
}; /* DTYPE_DCS_LWRITE :0xE200:0x11, 0xE201:0x19, 0xE202: 0x1e, ..., 0xff are padding for 4 bytes*/

static unsigned char sony_gamma28_04[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_05[] = {
	0xe3, 0x19, 0x1e, 0x22,
	0x0c, 0x04, 0x0c, 0x0a,
	0x08, 0x06, 0x09, 0x0f,
	0x07, 0x0e, 0x0b, 0x06,
	0x00,
}; /* DTYPE_DCS_LWRITE :0xE200:0x11, 0xE201:0x19, 0xE202: 0x1e, ..., 0xff are padding for 4 bytes*/

static unsigned char sony_gamma28_06[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_07[] = {
	0xe4, 0x19, 0x1e, 0x22,
	0x0c, 0x04, 0x0c, 0x0a,
	0x08, 0x06, 0x09, 0x0f,
	0x07, 0x0e, 0x0b, 0x06,
	0x00
}; /* DTYPE_DCS_LWRITE :0xE200:0x11, 0xE201:0x19, 0xE202: 0x1e, ..., 0xff are padding for 4 bytes*/

static unsigned char sony_gamma28_08[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_09[] = {
	0xe5, 0x07, 0x12, 0x18,
	0x0f, 0x07, 0x0f, 0x0b,
	0x0a, 0x05, 0x09, 0x0e,
	0x04, 0x0b, 0x08, 0x04,
	0x00
}; /* DTYPE_DCS_LWRITE :0xE200:0x11, 0xE201:0x19, 0xE202: 0x1e, ..., 0xff are padding for 4 bytes*/

static unsigned char sony_gamma28_10[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 :address shift*/
static unsigned char sony_gamma28_11[] = {
	0xe6, 0x07, 0x12, 0x18,
	0x0f, 0x07, 0x0f, 0x0b,
	0x0a, 0x05, 0x09, 0x0e,
	0x04, 0x0b, 0x08, 0x04,
	0x00
}; /* DTYPE_DCS_LWRITE :0xE200:0x11, 0xE201:0x19, 0xE202: 0x1e, ..., 0xff are padding for 4 bytes*/
/* NOP */
static unsigned char sony_gamma28_12[] = {0x00, 0x00}; /* DTYPE_DCS_WRITE1 */

static struct dsi_cmd_desc orise_video_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 10,	sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120,	sizeof(display_on), display_on},
};

static struct dsi_cmd_desc orise_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(engmod_1), engmod_1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(engmod_2), engmod_2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(engmod_3), engmod_3},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_gate1), pwm_gate1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_gate2), pwm_gate2},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc auo_orise_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/

	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_001), auo_orise_001},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_002), auo_orise_002},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_003), auo_orise_003},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_004), auo_orise_004},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_005), auo_orise_005},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_006), auo_orise_006},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_007), auo_orise_007},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_008), auo_orise_008},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_009), auo_orise_009},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_010), auo_orise_010},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_011), auo_orise_011},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_012), auo_orise_012},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_013), auo_orise_013},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_014), auo_orise_014},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_015), auo_orise_015},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_016), auo_orise_016},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_017), auo_orise_017},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_018), auo_orise_018},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_019), auo_orise_019},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_020), auo_orise_020},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_021), auo_orise_021},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_022), auo_orise_022},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_023), auo_orise_023},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_024), auo_orise_024},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_025), auo_orise_025},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_026), auo_orise_026},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_027), auo_orise_027},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_028), auo_orise_028},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_029), auo_orise_029},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_030), auo_orise_030},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_031), auo_orise_031},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_032), auo_orise_032},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_033), auo_orise_033},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_034), auo_orise_034},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_035), auo_orise_035},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_036), auo_orise_036},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_037), auo_orise_037},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_038), auo_orise_038},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_039), auo_orise_039},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_040), auo_orise_040},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_041), auo_orise_041},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_042), auo_orise_042},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_043), auo_orise_043},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_044), auo_orise_044},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_045), auo_orise_045},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_046), auo_orise_046},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_047), auo_orise_047},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_048), auo_orise_048},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_049), auo_orise_049},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_050), auo_orise_050},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_051), auo_orise_051},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_052), auo_orise_052},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_053), auo_orise_053},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_054), auo_orise_054},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_055), auo_orise_055},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_056), auo_orise_056},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_057), auo_orise_057},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_058), auo_orise_058},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_059), auo_orise_059},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_060), auo_orise_060},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_061), auo_orise_061},

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma22_00), auo_gamma22_00},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_01), auo_gamma22_01},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_02), auo_gamma22_02},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_03), auo_gamma22_03},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_04), auo_gamma22_04},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_05), auo_gamma22_05},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_06), auo_gamma22_06},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_07), auo_gamma22_07},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma22_08), auo_gamma22_08},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma22_09), auo_gamma22_09},

	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc orise_c2_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(engmod_1), engmod_1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(engmod_2), engmod_2},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(engmod_3), engmod_3},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_gate1), pwm_gate1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_gate2), pwm_gate2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(va1), va1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(va2), va2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(va3), va3},*/
	{DTYPE_DCS_WRITE, 1, 0, 0, 100, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc sony_orise_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_orise_001), sony_orise_001},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_orise_002), sony_orise_002},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_orise_003), sony_orise_003},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_orise_004), sony_orise_004},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_00), sony_gamma28_00},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_01), sony_gamma28_01},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_02), sony_gamma28_02},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_03), sony_gamma28_03},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_04), sony_gamma28_04},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_05), sony_gamma28_05},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_06), sony_gamma28_06},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_07), sony_gamma28_07},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_08), sony_gamma28_08},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_09), sony_gamma28_09},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_10), sony_gamma28_10},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(sony_gamma28_11), sony_gamma28_11},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(sony_gamma28_12), sony_gamma28_12},

	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_freq_sel_cmds1), pwm_freq_sel_cmds1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_freq_sel_cmds2), pwm_freq_sel_cmds2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_dbf_cmds1), pwm_dbf_cmds1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(pwm_dbf_cmds2), pwm_dbf_cmds2},

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(set_tear_line), set_tear_line},

	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},

	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc auo_orise_c2_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/

	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_001), auo_orise_001},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_002), auo_orise_002},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_003), auo_orise_003},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_004), auo_orise_004},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_005), auo_orise_005},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_006), auo_orise_006},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_007), auo_orise_007},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_008), auo_orise_008},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_009), auo_orise_009},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_010), auo_orise_010},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_011), auo_orise_011},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_012), auo_orise_012},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_013), auo_orise_013},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_014), auo_orise_014},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_015), auo_orise_015},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_016), auo_orise_016},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_017), auo_orise_017},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_01), auo_orise_c2_01},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_02), auo_orise_c2_02},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_018), auo_orise_018},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_019), auo_orise_019},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_020), auo_orise_020},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_021), auo_orise_021},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_022), auo_orise_022},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_023), auo_orise_023},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_024), auo_orise_024},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_025), auo_orise_025},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_026), auo_orise_026},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_027), auo_orise_027},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_028), auo_orise_028},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_029), auo_orise_029},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_030), auo_orise_030},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_031), auo_orise_031},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_032), auo_orise_032},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_033), auo_orise_033},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_034), auo_orise_034},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_035), auo_orise_035},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_036), auo_orise_036},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_037), auo_orise_037},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_03), auo_orise_c2_03},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_04), auo_orise_c2_04},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_038), auo_orise_038},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_039), auo_orise_039},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_040), auo_orise_040},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_041), auo_orise_041},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_042), auo_orise_042},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_043), auo_orise_043},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_044), auo_orise_044},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_045), auo_orise_045},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_046), auo_orise_046},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_047_c2), auo_orise_047_c2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_048), auo_orise_048},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_049), auo_orise_049},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_050), auo_orise_050},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_051), auo_orise_051},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_052), auo_orise_052},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_053), auo_orise_053},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_054), auo_orise_054},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_055), auo_orise_055},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_056), auo_orise_056},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_057), auo_orise_057},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_058), auo_orise_058},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_059), auo_orise_059},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_060), auo_orise_060},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_061), auo_orise_061},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_062), auo_orise_062},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_063), auo_orise_063},

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma25_00), auo_gamma25_00},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_01), auo_gamma25_01},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_02), auo_gamma25_02},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_03), auo_gamma25_03},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_04), auo_gamma25_04},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_05), auo_gamma25_05},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_06), auo_gamma25_06},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_07), auo_gamma25_07},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_08), auo_gamma25_08},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma25_09), auo_gamma25_09},

	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc auo_orise_c3_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/

	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_001), auo_orise_001},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_002), auo_orise_002},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_003), auo_orise_003},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_004), auo_orise_004},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_005), auo_orise_005},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_006), auo_orise_006},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_007), auo_orise_007},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_008), auo_orise_008},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_009), auo_orise_009},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_010), auo_orise_010},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_011), auo_orise_011},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_012), auo_orise_012},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_013), auo_orise_013},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_014), auo_orise_014},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_015), auo_orise_015},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_016), auo_orise_016},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_017), auo_orise_017},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_01), auo_orise_c2_01},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_02), auo_orise_c2_02},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_018), auo_orise_018},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_019), auo_orise_019},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_020), auo_orise_020},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_021), auo_orise_021},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_022), auo_orise_022},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_023), auo_orise_023},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_024), auo_orise_024},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_025), auo_orise_025},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_026), auo_orise_026},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_027), auo_orise_027},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_028), auo_orise_028},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_029), auo_orise_029},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_030), auo_orise_030},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_031), auo_orise_031},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_032), auo_orise_032},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_033), auo_orise_033},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_034), auo_orise_034},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_035), auo_orise_035},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_036), auo_orise_036},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_037), auo_orise_037},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_03), auo_orise_c2_03},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_c2_04), auo_orise_c2_04},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_038), auo_orise_038},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_039), auo_orise_039},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_040), auo_orise_040},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_041), auo_orise_041},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_042), auo_orise_042},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_043), auo_orise_043},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_044), auo_orise_044},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_045), auo_orise_045},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_046), auo_orise_046},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_047_c2), auo_orise_047_c2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_048), auo_orise_048},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_049), auo_orise_049},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_050), auo_orise_050},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_051_c3), auo_orise_051_c3},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_052), auo_orise_052},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_053), auo_orise_053},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_054), auo_orise_054},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_055), auo_orise_055},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_056), auo_orise_056},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_057), auo_orise_057},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_058), auo_orise_058},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_orise_059), auo_orise_059},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_060), auo_orise_060},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_061_c3), auo_orise_061_c3},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_062), auo_orise_062},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_orise_063), auo_orise_063},

	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma25_00), auo_gamma25_00},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_01), auo_gamma25_01},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_02), auo_gamma25_02},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_03), auo_gamma25_03},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_04), auo_gamma25_04},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_05), auo_gamma25_05},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(auo_gamma25_06), auo_gamma25_06},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(auo_gamma25_09), auo_gamma25_09},

	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};

static struct dsi_cmd_desc shp_orise_cmd_on_cmds[] = {
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset},*/
	{DTYPE_DCS_WRITE, 1, 0, 0, 150, sizeof(exit_sleep), exit_sleep},
	/*{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},*/
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(enable_te), enable_te},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode1), no_video_mode1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_video_mode2), no_video_mode2},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te1), no_wait_te1},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(no_wait_te2), no_wait_te2},
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm1), led_pwm1},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm2), led_pwm2},*/
	/*{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(led_pwm3), led_pwm3},*/
};


static struct dsi_cmd_desc orise_display_off_cmds[] = {
		{DTYPE_DCS_WRITE, 1, 0, 0, 0,
			sizeof(display_off), display_off},
		{DTYPE_DCS_WRITE, 1, 0, 0, 110,
			sizeof(enter_sleep), enter_sleep}
};

#ifdef MIPI_READ_DISPLAY_ID
static char manufacture_id[2] = {0x04, 0x00}; /* DTYPE_DCS_READ */

static struct dsi_cmd_desc orise_manufacture_id_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id), manufacture_id};


static uint32 mipi_orise_manufacture_id(void)
{
	struct dsi_buf *rp, *tp;
	struct dsi_cmd_desc *cmd;
	uint32 *lp;

	tp = &orise_tx_buf;
	rp = &orise_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &orise_manufacture_id_cmd;
	mipi_dsi_cmds_rx(tp, rp, cmd, 3);

{
	int i;
	char *cp;

	cp = (char *)rp->data;
	printk(KERN_INFO "rx-data: ");
	for (i = 0; i < rp->len; i++, cp++)
		printk(KERN_INFO "%x ", *cp);
	printk(KERN_INFO "\n");
}

	lp = (uint32 *)rp->data;

	printk(KERN_INFO "%s: manu_id=%x", __func__, *lp);

	return *lp;
}
#endif

static char read_power[] = {0x0A, 0x00};
static struct dsi_cmd_desc power_cmd = {
	DTYPE_DCS_READ, 1, 0, 0, 0,
		sizeof(read_power), read_power
};

unsigned char mipi_orise_read_power(void)
{
	struct dsi_buf *tp, *rp;
	struct dsi_cmd_desc *cmd;

	mutex_lock(&cmdlock);
	/* mipi_dsi_cmd_bta_sw_trigger(); */

	tp = &orise_tx_buf;
	rp = &orise_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &power_cmd;
	mipi_dsi_cmds_rx(tp, rp, cmd, 3);

	mutex_unlock(&cmdlock);

	/* periodically check if S/W reset of DSI is necessary */
	if (1 == mipi_dsi_reset_read()) {
		mipi_dsi_reset_set(0);
		dsi_mutex_lock();
		dsi_busy_check();

		/*mipi_dsi_sw_reset();*/

		dsi_mutex_unlock();
	}

	return *(rp->data);
}

#if 0
static char read_status[] = {0x09, 0x00};
static struct dsi_cmd_desc chip_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5,
		sizeof(read_status), read_status
};
static unsigned char *mipi_orise_read_status(void)
{
	struct dsi_buf *tp, *rp;
	struct dsi_cmd_desc *cmd;

	tp = &orise_tx_buf;
	rp = &orise_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &chip_cmd;
	mipi_dsi_cmds_rx(tp, rp, cmd, 4);

	/* mipi_dsi_cmds_tx(orise_tx_buf, power_cmd, ARRAY_SIZE(power_cmd));*/

	{
		int i;
		char *cp;

		cp = (char *)rp->data;
		printk("chip status: ");
		for (i = 0; i < rp->len; i++, cp++)
			printk("%02X ", *cp);
		printk("\n");
	}

	/* lp = (uint32 *)rp->data; */
	return rp->data;
}
#endif

static struct dsi_cmd_desc sw_reset_cmd[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(sw_reset), sw_reset}
};

static struct dsi_cmd_desc display_on_cmd[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(bkl_enable_cmds), bkl_enable_cmds}
};

static struct dsi_cmd_desc orise_cmd_backlight_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(led_pwm1), led_pwm1},
};

void mipi_orise_recover_chip(void)
{
	mutex_lock(&cmdlock);

	if (!is_perf_lock_active(&orise_perf_lock))
		perf_lock(&orise_perf_lock);

	mipi_dsi_cmds_tx(&orise_tx_buf, sw_reset_cmd, ARRAY_SIZE(sw_reset_cmd));
	mipi_dsi_cmds_tx(&orise_tx_buf, sony_orise_cmd_on_cmds, ARRAY_SIZE(sony_orise_cmd_on_cmds));
	mipi_dsi_cmds_tx(&orise_tx_buf, orise_cmd_backlight_cmds, ARRAY_SIZE(orise_cmd_backlight_cmds));
	mipi_dsi_cmds_tx(&orise_tx_buf, display_on_cmd, ARRAY_SIZE(display_on_cmd));

	if (is_perf_lock_active(&orise_perf_lock))
		perf_unlock(&orise_perf_lock);

	mutex_unlock(&cmdlock);
}

static struct dsi_cmd_desc orise_display_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 0,
		sizeof(display_on), display_on},
};

static struct dsi_cmd_desc orise_bkl_enable_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(bkl_enable_cmds), bkl_enable_cmds},
};

static struct dsi_cmd_desc orise_bkl_disable_cmds[] = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 0,
		sizeof(bkl_disable_cmds), bkl_disable_cmds},
};

void mipi_orise_panel_type_detect(void)
{
	if (panel_type == PANEL_ID_SHR_SHARP_OTM) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_SHR_SHARP_OTM\n", __func__);
		strcat(ptype, "PANEL_ID_SHR_SHARP_OTM");
		mipi_power_on_cmd = orise_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(orise_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_RIR_AUO_OTM) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_RIR_AUO_OTM\n", __func__);
		strcat(ptype, "PANEL_ID_RIR_AUO_OTM");
		mipi_power_on_cmd = auo_orise_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(auo_orise_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_SHR_SHARP_OTM_C2) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_SHR_SHARP_OTM_C2\n", __func__);
		strcat(ptype, "PANEL_ID_SHR_SHARP_OTM_C2");
		mipi_power_on_cmd = orise_c2_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(orise_c2_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_HOY_SONY_OTM) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_HOY_SONY_OTM\n", __func__);
		strcat(ptype, "PANEL_ID_HOY_SONY_OTM");
		mipi_power_on_cmd = sony_orise_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(sony_orise_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_RIR_AUO_OTM_C2) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_RIR_AUO_OTM_C2\n", __func__);
		strcat(ptype, "PANEL_ID_RIR_AUO_OTM_C2");
		mipi_power_on_cmd = auo_orise_c2_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(auo_orise_c2_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_RIR_AUO_OTM_C3) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_RIR_AUO_OTM_C3\n", __func__);
		strcat(ptype, "PANEL_ID_RIR_AUO_OTM_C3");
		mipi_power_on_cmd = auo_orise_c3_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(auo_orise_c3_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else if (panel_type == PANEL_ID_RIR_SHARP_OTM) {
		PR_DISP_INFO("%s: panel_type=PANEL_ID_RIR_SHARP_OTM\n", __func__);
		strcat(ptype, "PANEL_ID_RIR_SHARP_OTM");
		mipi_power_on_cmd = shp_orise_cmd_on_cmds;
		mipi_power_on_cmd_size = ARRAY_SIZE(shp_orise_cmd_on_cmds);
		mipi_power_off_cmd = orise_display_off_cmds;
		mipi_power_off_cmd_size = ARRAY_SIZE(orise_display_off_cmds);
	} else {
		PR_DISP_ERR("%s: panel_type=0x%x not support\n", __func__, panel_type);
		strcat(ptype, "PANEL_ID_NONE");
	}
	return;
}

/*
 *  Common Routine Implementation
 */
static int mipi_orise_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct msm_fb_panel_data *pdata = NULL;
	struct mipi_panel_info *mipi;
	static int init = 0;

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;

	pdata = (struct msm_fb_panel_data *)mfd->pdev->dev.platform_data;
	mipi = &mfd->panel_info.mipi;
	mutex_lock(&cmdlock);
	if (init == 0) {
		if (pdata && pdata->panel_type_detect)
			pdata->panel_type_detect();
		init = 1;
		goto end;
	} else {
		if (mipi->mode == DSI_VIDEO_MODE) {
			mipi_dsi_cmds_tx(&orise_tx_buf, orise_video_on_cmds,
				ARRAY_SIZE(orise_video_on_cmds));
		} else {
			if (panel_type != PANEL_ID_NONE) {
				PR_DISP_INFO("%s\n", ptype);
				if (!is_perf_lock_active(&orise_perf_lock))
					perf_lock(&orise_perf_lock);
				mipi_dsi_cmds_tx(&orise_tx_buf, mipi_power_on_cmd, mipi_power_on_cmd_size);
				if (is_perf_lock_active(&orise_perf_lock))
					perf_unlock(&orise_perf_lock);
#ifdef MIPI_READ_DISPLAY_ID /* mipi read command verify */
				/* clean up ack_err_status */
				mipi_dsi_cmd_bta_sw_trigger();
				mipi_novatek_manufacture_id();
#endif
			} else {
				printk(KERN_ERR "panel_type=0x%x not support at power on\n", panel_type);
				mutex_unlock(&cmdlock);
				return -EINVAL;
			}
		}
	}
end:
	mutex_unlock(&cmdlock);
	return 0;
}

static int mipi_orise_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
	mutex_lock(&cmdlock);

	if (panel_type != PANEL_ID_NONE) {
		PR_DISP_INFO("%s\n", ptype);
		if (!is_perf_lock_active(&orise_perf_lock))
			perf_lock(&orise_perf_lock);
		mipi_dsi_cmds_tx(&orise_tx_buf, mipi_power_off_cmd, mipi_power_off_cmd_size);
		if (is_perf_lock_active(&orise_perf_lock))
			perf_unlock(&orise_perf_lock);
	} else
		printk(KERN_ERR "panel_type=0x%x not support at power off\n", panel_type);

	mutex_unlock(&cmdlock);
	return 0;
}

static int mipi_dsi_set_backlight(struct msm_fb_data_type *mfd)
{
	struct mipi_panel_info *mipi;
	static int bl_level_old = 0;
	mutex_lock(&cmdlock);

	mipi  = &mfd->panel_info.mipi;
	PR_DISP_DEBUG("%s+:bl=%d status=%d\n", __func__, mfd->bl_level, mipi_status);
	if (mipi_status == 0)
		goto end;
	if (mipi_orise_pdata && mipi_orise_pdata->shrink_pwm)
		led_pwm1[1] = mipi_orise_pdata->shrink_pwm(mfd->bl_level);
	else
		led_pwm1[1] = (unsigned char)(mfd->bl_level);

	if (mfd->bl_level == 0 ||
			board_mfg_mode() == 4 ||
			(board_mfg_mode() == 5 && !(htc_battery_get_zcharge_mode()%2))) {
		led_pwm1[1] = 0;
	}

	if (mipi->mode == DSI_VIDEO_MODE) {
		mipi_dsi_cmd_mode_ctrl(1);	/* enable cmd mode */
		mipi_dsi_cmds_tx(&orise_tx_buf, orise_cmd_backlight_cmds,
			ARRAY_SIZE(orise_cmd_backlight_cmds));
		mipi_dsi_cmd_mode_ctrl(0);	/* disable cmd mode */
	} else {
		mipi_dsi_op_mode_config(DSI_CMD_MODE);
		mipi_dsi_cmds_tx(&orise_tx_buf, orise_cmd_backlight_cmds,
			ARRAY_SIZE(orise_cmd_backlight_cmds));
	}
	bl_level_prevset = bl_level_old = mfd->bl_level;
end:
	mutex_unlock(&cmdlock);
	return 0;
}

static void mipi_orise_set_backlight(struct msm_fb_data_type *mfd)
{
	int bl_level;

	bl_level = mfd->bl_level;
	PR_DISP_DEBUG("%s+ bl_level=%d\n", __func__, mfd->bl_level);

	mipi_dsi_set_backlight(mfd);

}

static void mipi_orise_display_on(struct msm_fb_data_type *mfd)
{
	PR_DISP_DEBUG("%s+\n", __func__);
	mutex_lock(&cmdlock);
	mipi_dsi_op_mode_config(DSI_CMD_MODE);
	mipi_dsi_cmds_tx(&orise_tx_buf, orise_display_on_cmds,
		ARRAY_SIZE(orise_display_on_cmds));
	mutex_unlock(&cmdlock);
}

static void mipi_orise_bkl_switch(struct msm_fb_data_type *mfd, bool on)
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

static void mipi_orise_bkl_ctrl(bool on)
{
	mutex_lock(&cmdlock);
	if (on) {
		mipi_dsi_op_mode_config(DSI_CMD_MODE);
		mipi_dsi_cmds_tx(&orise_tx_buf, orise_bkl_enable_cmds,
			ARRAY_SIZE(orise_bkl_enable_cmds));
	} else {
		mipi_dsi_op_mode_config(DSI_CMD_MODE);
		mipi_dsi_cmds_tx(&orise_tx_buf, orise_bkl_disable_cmds,
			ARRAY_SIZE(orise_bkl_disable_cmds));
	}
	mutex_unlock(&cmdlock);
}

static int mipi_orise_lcd_probe(struct platform_device *pdev)
{
	if (pdev->id == 0) {
		mipi_orise_pdata = pdev->dev.platform_data;
		mutex_init(&cmdlock);
		perf_lock_init(&orise_perf_lock, PERF_LOCK_HIGHEST, "orise");
		return 0;
	}

	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mipi_orise_lcd_probe,
	.driver = {
		.name   = "mipi_orise",
	},
};

static struct msm_fb_panel_data orise_panel_data = {
	.on		= mipi_orise_lcd_on,
	.off		= mipi_orise_lcd_off,
	.set_backlight  = mipi_orise_set_backlight,
	.display_on  = mipi_orise_display_on,
	.bklswitch	= mipi_orise_bkl_switch,
	.bklctrl	= mipi_orise_bkl_ctrl,
	.panel_type_detect = mipi_orise_panel_type_detect,
};

static int ch_used[3];

int mipi_orise_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_orise", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	orise_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &orise_panel_data,
		sizeof(orise_panel_data));
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

static int __init mipi_orise_lcd_init(void)
{
	mipi_dsi_buf_alloc(&orise_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&orise_rx_buf, DSI_BUF_SIZE);
	return platform_driver_register(&this_driver);
}

module_init(mipi_orise_lcd_init);
