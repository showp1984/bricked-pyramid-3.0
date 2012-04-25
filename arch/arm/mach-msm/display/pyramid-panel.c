/* linux/arch/arm/mach-msm/board-mecha-panel.c
 *
 * Copyright (c) 2011 HTC.
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

#include <asm/io.h>
#include <asm/mach-types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <mach/msm_fb.h>
#include <mach/msm_iomap.h>
#include <mach/panel_id.h>
#include <mach/msm_bus_board.h>
#include <mach/debug_display.h>

#include "../devices.h"
#include "../board-pyramid.h"
#include "../devices-msm8x60.h"
#include "../../../../drivers/video/msm_8x60/mdp_hw.h"
#if defined (CONFIG_FB_MSM_MDP_ABL)
#include <linux/fb.h>
#endif

void mdp_color_enhancement(const struct mdp_reg *reg_seq, int size);

static struct regulator *l1_3v;
static struct regulator *lvs1_1v8;
static struct regulator *l4_1v8;

/*
TODO:
1. move regulator initialization to pyramid_panel_init()
*/
static void pyramid_panel_power(int on)
{
	static int init;
	int ret;
	int rc;

	PR_DISP_INFO("%s(%d): init=%d system_rev:%d\n", __func__, on, init, system_rev);

	/* If panel is already on (or off), do nothing. */
	if (!init) {
		l1_3v = regulator_get(NULL, "8901_l1");
		if (IS_ERR(l1_3v)) {
			PR_DISP_ERR("%s: unable to get 8901_l1\n", __func__);
			goto fail;
		}
		if (system_rev >= 1) {
			l4_1v8 = regulator_get(NULL, "8901_l4");
			if (IS_ERR(l4_1v8)) {
				PR_DISP_ERR("%s: unable to get 8901_l4\n", __func__);
				goto fail;
			}
		} else {
			lvs1_1v8 = regulator_get(NULL, "8901_lvs1");
			if (IS_ERR(lvs1_1v8)) {
				PR_DISP_ERR("%s: unable to get 8901_lvs1\n", __func__);
				goto fail;
			}
		}

		ret = regulator_set_voltage(l1_3v, 3100000, 3100000);
		if (ret) {
			PR_DISP_ERR("%s: error setting l1_3v voltage\n", __func__);
			goto fail;
		}

		if (system_rev >= 1) {
			ret = regulator_set_voltage(l4_1v8, 1800000, 1800000);
			if (ret) {
				PR_DISP_ERR("%s: error setting l4_1v8 voltage\n", __func__);
				goto fail;
			}
		}

		/* LCM Reset */
		rc = gpio_request(GPIO_LCM_RST_N,
				"LCM_RST_N");
		if (rc) {
			printk(KERN_ERR "%s:LCM gpio %d request"
					"failed\n", __func__,
					GPIO_LCM_RST_N);
			return;
		}

		init = 1;
	}

	if (!l1_3v || IS_ERR(l1_3v)) {
		PR_DISP_ERR("%s: l1_3v is not initialized\n", __func__);
		return;
	}

	if (system_rev >= 1) {
		if (!l4_1v8 || IS_ERR(l4_1v8)) {
			PR_DISP_ERR("%s: l4_1v8 is not initialized\n", __func__);
			return;
		}
	} else {
		if (!lvs1_1v8 || IS_ERR(lvs1_1v8)) {
			PR_DISP_ERR("%s: lvs1_1v8 is not initialized\n", __func__);
			return;
		}
	}
	if (on) {
		if (regulator_enable(l1_3v)) {
			PR_DISP_ERR("%s: Unable to enable the regulator:"
					" l1_3v\n", __func__);
			return;
		}
		hr_msleep(5);

		if (system_rev >= 1) {
			if (regulator_enable(l4_1v8)) {
				PR_DISP_ERR("%s: Unable to enable the regulator:"
						" l4_1v8\n", __func__);
				return;
			}
		} else {

			if (regulator_enable(lvs1_1v8)) {
				PR_DISP_ERR("%s: Unable to enable the regulator:"
						" lvs1_1v8\n", __func__);
				return;
			}
		}

		if (init == 1) {
			init = 2;

			return;
		} else {
			hr_msleep(10);
			gpio_set_value(GPIO_LCM_RST_N, 1);
			hr_msleep(1);
			gpio_set_value(GPIO_LCM_RST_N, 0);
			hr_msleep(1);
			gpio_set_value(GPIO_LCM_RST_N, 1);
			hr_msleep(20);
		}
	} else {
		gpio_set_value(GPIO_LCM_RST_N, 0);
		hr_msleep(5);
		if (system_rev >= 1) {
			if (regulator_disable(l4_1v8)) {
				PR_DISP_ERR("%s: Unable to enable the regulator:"
						" l4_1v8\n", __func__);
				return;
			}
		} else {
			if (regulator_disable(lvs1_1v8)) {
				PR_DISP_ERR("%s: Unable to enable the regulator:"
						" lvs1_1v8\n", __func__);
				return;
			}
		}
		hr_msleep(5);
		if (regulator_disable(l1_3v)) {
			PR_DISP_ERR("%s: Unable to enable the regulator:"
					" l1_3v\n", __func__);
			return;
		}
	}
	return;

fail:
	if (l1_3v)
		regulator_put(l1_3v);
	if (system_rev >= 1) {
		if (l4_1v8)
			regulator_put(l4_1v8);
	} else {
		if (lvs1_1v8)
			regulator_put(lvs1_1v8);
	}
}

/*
TODO:
1. remove unused LCDC stuff.
*/
static uint32_t lcd_panel_gpios[] = {
	GPIO_CFG(0,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_pclk */
	GPIO_CFG(1,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_hsync*/
	GPIO_CFG(2,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_vsync*/
	GPIO_CFG(3,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_den */
	GPIO_CFG(4,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red7 */
	GPIO_CFG(5,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red6 */
	GPIO_CFG(6,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red5 */
	GPIO_CFG(7,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red4 */
	GPIO_CFG(8,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red3 */
	GPIO_CFG(9,  1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red2 */
	GPIO_CFG(10, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red1 */
	GPIO_CFG(11, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_red0 */
	GPIO_CFG(12, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn7 */
	GPIO_CFG(13, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn6 */
	GPIO_CFG(14, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn5 */
	GPIO_CFG(15, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn4 */
	GPIO_CFG(16, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn3 */
	GPIO_CFG(17, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn2 */
	GPIO_CFG(18, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn1 */
	GPIO_CFG(19, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_grn0 */
	GPIO_CFG(20, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu7 */
	GPIO_CFG(21, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu6 */
	GPIO_CFG(22, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu5 */
	GPIO_CFG(23, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu4 */
	GPIO_CFG(24, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu3 */
	GPIO_CFG(25, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu2 */
	GPIO_CFG(26, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu1 */
	GPIO_CFG(27, 1, GPIO_CFG_OUTPUT,  GPIO_CFG_NO_PULL, GPIO_CFG_16MA), /* lcdc_blu0 */
};

static void lcdc_samsung_panel_power(int on)
{
	int n;

	/*TODO if on = 0 free the gpio's */
	for (n = 0; n < ARRAY_SIZE(lcd_panel_gpios); ++n)
		gpio_tlmm_config(lcd_panel_gpios[n], 0);
}

static int lcdc_panel_power(int on)
{
	int flag_on = !!on;
	static int lcdc_power_save_on;

	if (lcdc_power_save_on == flag_on)
		return 0;

	lcdc_power_save_on = flag_on;

	lcdc_samsung_panel_power(on);

	return 0;
}

#ifdef CONFIG_MSM_BUS_SCALING
static struct msm_bus_vectors mdp_init_vectors[] = {
	/* For now, 0th array entry is reserved.
	 * Please leave 0 as is and don't use it
	 */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 0,
		.ib = 0,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 0,
		.ib = 0,
	},
};

static struct msm_bus_vectors mdp_sd_smi_vectors[] = {
	/* Default case static display/UI/2d/3d if FB SMI */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 147460000,
		.ib = 184325000,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 0,
		.ib = 0,
	},
};

static struct msm_bus_vectors mdp_sd_ebi_vectors[] = {
	/* Default case static display/UI/2d/3d if FB SMI */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 0,
		.ib = 0,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 168652800,
		.ib = 337305600,
	},
};
static struct msm_bus_vectors mdp_vga_vectors[] = {
	/* VGA and less video */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 37478400,
		.ib = 74956800,
	},
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 206131200,
		.ib = 412262400,
	},
};

static struct msm_bus_vectors mdp_720p_vectors[] = {
	/* 720p and less video */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 112435200,
		.ib = 224870400,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 281088000,
		.ib = 562176000,
	},
};

static struct msm_bus_vectors mdp_1080p_vectors[] = {
	/* 1080p and less video */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 252979200,
		.ib = 505958400,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 421632000,
		.ib = 843264000,
	},
};
static struct msm_bus_paths mdp_bus_scale_usecases[] = {
	{
		ARRAY_SIZE(mdp_init_vectors),
		mdp_init_vectors,
	},
	{
		ARRAY_SIZE(mdp_sd_smi_vectors),
		mdp_sd_smi_vectors,
	},
	{
		ARRAY_SIZE(mdp_sd_ebi_vectors),
		mdp_sd_ebi_vectors,
	},
	{
		ARRAY_SIZE(mdp_vga_vectors),
		mdp_vga_vectors,
	},
	{
		ARRAY_SIZE(mdp_720p_vectors),
		mdp_720p_vectors,
	},
	{
		ARRAY_SIZE(mdp_1080p_vectors),
		mdp_1080p_vectors,
	},
};

static struct msm_bus_scale_pdata mdp_bus_scale_pdata = {
	mdp_bus_scale_usecases,
	ARRAY_SIZE(mdp_bus_scale_usecases),
	.name = "mdp",
};
#endif

#ifdef CONFIG_MSM_BUS_SCALING
static struct msm_bus_vectors dtv_bus_init_vectors[] = {
	/* For now, 0th array entry is reserved.
	 * Please leave 0 as is and don't use it
	 */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 0,
		.ib = 0,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 0,
		.ib = 0,
	},
};
static struct msm_bus_vectors dtv_bus_def_vectors[] = {
	/* For now, 0th array entry is reserved.
	 * Please leave 0 as is and don't use it
	 */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab = 252979200,
		.ib = 505958400,
	},
	/* Master and slaves can be from different fabrics */
	{
		.src = MSM_BUS_MASTER_MDP_PORT0,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab = 421632000,
		.ib = 843264000,
	},
};
static struct msm_bus_paths dtv_bus_scale_usecases[] = {
	{
		ARRAY_SIZE(dtv_bus_init_vectors),
		dtv_bus_init_vectors,
	},
	{
		ARRAY_SIZE(dtv_bus_def_vectors),
		dtv_bus_def_vectors,
	},
};
static struct msm_bus_scale_pdata dtv_bus_scale_pdata = {
	dtv_bus_scale_usecases,
	ARRAY_SIZE(dtv_bus_scale_usecases),
	.name = "dtv",
};

static struct lcdc_platform_data dtv_pdata = {
	.bus_scale_table = &dtv_bus_scale_pdata,
};
#endif

static int mipi_panel_power(int on)
{
	int flag_on = !!on;
	static int mipi_power_save_on;

	if (mipi_power_save_on == flag_on)
		return 0;

	mipi_power_save_on = flag_on;

	pyramid_panel_power(on);

	return 0;
}

static struct lcdc_platform_data lcdc_pdata = {
	.lcdc_power_save   = lcdc_panel_power,
};

/* The solution provide by Novatek to fixup the problem of blank screen while
 * performing static electric strick. Only AUO panel need this function.
 */
int pyd_esd_fixup(uint32_t mfd_data)
{
	/* do two read_scan_line consecutively to avoid flicking */
	if (mipi_novatek_read_scan_line(mfd_data) == 0xf7ff) {
		hr_msleep(1);
		if (mipi_novatek_read_scan_line(mfd_data) == 0xf7ff) {
			pr_info("%s\n", __func__);
			mipi_novatek_restart_vcounter(mfd_data);
		}
	}

	return 0;
}

static struct mipi_dsi_platform_data mipi_pdata = {
	.vsync_gpio		= 28,
	.dsi_power_save		= mipi_panel_power,
};

static struct platform_device mipi_dsi_video_sharp_wvga_panel_device = {
	.name = "dsi_video_sharp_wvga",
	.id = 0,
};

#define GPIO_BACKLIGHT_PWM0 0
#define GPIO_BACKLIGHT_PWM1 1

static int pmic_backlight_gpio[2]
= { GPIO_BACKLIGHT_PWM0, GPIO_BACKLIGHT_PWM1 };
static struct msm_panel_common_pdata lcdc_samsung_panel_data = {
	.gpio_num = pmic_backlight_gpio, /* two LPG CHANNELS for backlight */
};

static struct platform_device lcdc_samsung_panel_device = {
	.name = "lcdc_samsung_wsvga",
	.id = 0,
	.dev = {
		.platform_data = &lcdc_samsung_panel_data,
	}
};

#define BRI_SETTING_MIN                 30
#define BRI_SETTING_DEF                 143
#define BRI_SETTING_MAX                 255

#define SHARP_PWM_MIN                   9	/* 3.5% of max pwm */
#define SHARP_PWM_DEFAULT               69	/* 27% of max pwm  */
#define SHARP_PWM_MAX                   194	/* 76% of max pwm */

static unsigned char pyd_shp_shrink_pwm(int br)
{
	unsigned char shrink_br = BRI_SETTING_MAX;

	if (br <= 0) {
		shrink_br = 0;
	} else if (br > 0 && br <= BRI_SETTING_MIN) {
		shrink_br = SHARP_PWM_MIN;
	} else if (br > BRI_SETTING_MIN && br <= BRI_SETTING_DEF) {
		shrink_br = (SHARP_PWM_MIN + (br - BRI_SETTING_MIN) *
				(SHARP_PWM_DEFAULT - SHARP_PWM_MIN) /
				(BRI_SETTING_DEF - BRI_SETTING_MIN));
	} else if (br > BRI_SETTING_DEF && br <= BRI_SETTING_MAX) {
		shrink_br = (SHARP_PWM_DEFAULT + (br - BRI_SETTING_DEF) *
				(SHARP_PWM_MAX - SHARP_PWM_DEFAULT) /
				(BRI_SETTING_MAX - BRI_SETTING_DEF));
	} else if (br > BRI_SETTING_MAX)
		shrink_br = SHARP_PWM_MAX;
	/* TODO: remove log later */
	PR_DISP_INFO("SHP: brightness orig=%d, transformed=%d\n", br, shrink_br);

	return shrink_br;
}

#define AUO_PWM_MIN                     9	/* 3.5% of max pwm */
#define AUO_PWM_DEFAULT                 87	/* 34% of max pwm  */
#define AUO_PWM_MAX                     255	/* 100% of max pwm  */

static unsigned char pyd_auo_shrink_pwm(int br)
{
	unsigned char shrink_br = 0;

	if (br <= 0) {
		shrink_br = 0;
	} else if (br > 0 && br <= BRI_SETTING_MIN) {
		shrink_br = AUO_PWM_MIN;
	} else if (br > BRI_SETTING_MIN && br <= BRI_SETTING_DEF) {
		shrink_br = (AUO_PWM_MIN + (br - BRI_SETTING_MIN) *
				(AUO_PWM_DEFAULT - AUO_PWM_MIN) /
				(BRI_SETTING_DEF - BRI_SETTING_MIN));
	} else if (br > BRI_SETTING_DEF && br <= BRI_SETTING_MAX) {
		shrink_br = (AUO_PWM_DEFAULT + (br - BRI_SETTING_DEF) *
				(AUO_PWM_MAX - AUO_PWM_DEFAULT) /
				(BRI_SETTING_MAX - BRI_SETTING_DEF));
	} else if (br > BRI_SETTING_MAX)
		shrink_br = AUO_PWM_MAX;
	/* TODO: remove log later */
	PR_DISP_INFO("AUO: brightness orig=%d, transformed=%d\n", br, shrink_br);

	return shrink_br;
}

static struct msm_panel_common_pdata mipi_novatek_panel_data = {
	.shrink_pwm = NULL,
};

static struct platform_device mipi_dsi_cmd_sharp_qhd_panel_device = {
	.name = "mipi_novatek",
	.id = 0,
	.dev = {
		.platform_data = &mipi_novatek_panel_data,
	}
};

static int msm_fb_detect_panel(const char *name)
{
	if (!strcmp(name, "mipi_cmd_novatek_qhd"))
		return 0;

#ifdef CONFIG_FB_MSM_HDMI_MSM_PANEL
	else if (!strcmp(name, "hdmi_msm"))
		return 0;
#endif /* CONFIG_FB_MSM_HDMI_MSM_PANEL */

	pr_warning("%s: not supported '%s'", __func__, name);
	return -ENODEV;
}

static struct msm_fb_platform_data msm_fb_pdata = {
	.detect_client = msm_fb_detect_panel,
	.blt_mode = 1,
	.width = 53,
	.height = 95,
};

static struct platform_device msm_fb_device = {
	.name   = "msm_fb",
	.id     = 0,
	.dev.platform_data = &msm_fb_pdata,
};

int mdp_core_clk_rate_table[] = {
	59080000,
	128000000,
	160000000,
	200000000,
};

struct mdp_reg pyd_color_v11[] = {
	{0x93400, 0x0222, 0x0},
	{0x93404, 0xFFE4, 0x0},
	{0x93408, 0xFFFD, 0x0},
	{0x9340C, 0xFFF1, 0x0},
	{0x93410, 0x0212, 0x0},
	{0x93414, 0xFFF9, 0x0},
	{0x93418, 0xFFF1, 0x0},
	{0x9341C, 0xFFE6, 0x0},
	{0x93420, 0x022D, 0x0},
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

struct mdp_reg pyd_auo_gamma[] = {
	{0x94800, 0x000000, 0x0},
	{0x94804, 0x010201, 0x0},
	{0x94808, 0x020202, 0x0},
	{0x9480C, 0x030304, 0x0},
	{0x94810, 0x040405, 0x0},
	{0x94814, 0x050506, 0x0},
	{0x94818, 0x060508, 0x0},
	{0x9481C, 0x070609, 0x0},
	{0x94820, 0x08070A, 0x0},
	{0x94824, 0x09080B, 0x0},
	{0x94828, 0x0A080C, 0x0},
	{0x9482C, 0x0B090E, 0x0},
	{0x94830, 0x0C0A0F, 0x0},
	{0x94834, 0x0D0B10, 0x0},
	{0x94838, 0x0E0B11, 0x0},
	{0x9483C, 0x0F0C12, 0x0},
	{0x94840, 0x100D13, 0x0},
	{0x94844, 0x110E14, 0x0},
	{0x94848, 0x120E15, 0x0},
	{0x9484C, 0x130F16, 0x0},
	{0x94850, 0x141016, 0x0},
	{0x94854, 0x151117, 0x0},
	{0x94858, 0x161118, 0x0},
	{0x9485C, 0x171219, 0x0},
	{0x94860, 0x18131A, 0x0},
	{0x94864, 0x19141A, 0x0},
	{0x94868, 0x1A151B, 0x0},
	{0x9486C, 0x1B151C, 0x0},
	{0x94870, 0x1C161D, 0x0},
	{0x94874, 0x1D171D, 0x0},
	{0x94878, 0x1E181E, 0x0},
	{0x9487C, 0x1F181F, 0x0},
	{0x94880, 0x201920, 0x0},
	{0x94884, 0x211A20, 0x0},
	{0x94888, 0x221B21, 0x0},
	{0x9488C, 0x231C22, 0x0},
	{0x94890, 0x241C22, 0x0},
	{0x94894, 0x251D23, 0x0},
	{0x94898, 0x261E23, 0x0},
	{0x9489C, 0x271F24, 0x0},
	{0x948A0, 0x282025, 0x0},
	{0x948A4, 0x292025, 0x0},
	{0x948A8, 0x2A2126, 0x0},
	{0x948AC, 0x2B2227, 0x0},
	{0x948B0, 0x2C2327, 0x0},
	{0x948B4, 0x2D2428, 0x0},
	{0x948B8, 0x2E2528, 0x0},
	{0x948BC, 0x2F2529, 0x0},
	{0x948C0, 0x30262A, 0x0},
	{0x948C4, 0x31272A, 0x0},
	{0x948C8, 0x32282B, 0x0},
	{0x948CC, 0x33292C, 0x0},
	{0x948D0, 0x34292C, 0x0},
	{0x948D4, 0x352A2D, 0x0},
	{0x948D8, 0x362B2D, 0x0},
	{0x948DC, 0x372C2E, 0x0},
	{0x948E0, 0x382D2F, 0x0},
	{0x948E4, 0x392E2F, 0x0},
	{0x948E8, 0x3A2E30, 0x0},
	{0x948EC, 0x3B2F30, 0x0},
	{0x948F0, 0x3C3030, 0x0},
	{0x948F4, 0x3D3131, 0x0},
	{0x948F8, 0x3E3231, 0x0},
	{0x948FC, 0x3F3332, 0x0},
	{0x94900, 0x403433, 0x0},
	{0x94904, 0x413434, 0x0},
	{0x94908, 0x423535, 0x0},
	{0x9490C, 0x433635, 0x0},
	{0x94910, 0x443735, 0x0},
	{0x94914, 0x453836, 0x0},
	{0x94918, 0x463937, 0x0},
	{0x9491C, 0x473938, 0x0},
	{0x94920, 0x483A39, 0x0},
	{0x94924, 0x493B3A, 0x0},
	{0x94928, 0x4A3C3B, 0x0},
	{0x9492C, 0x4B3D3C, 0x0},
	{0x94930, 0x4C3E3D, 0x0},
	{0x94934, 0x4C3E3E, 0x0},
	{0x94938, 0x4C3E3F, 0x0},
	{0x9493C, 0x4D3F40, 0x0},
	{0x94940, 0x4E4040, 0x0},
	{0x94944, 0x4F4041, 0x0},
	{0x94948, 0x504142, 0x0},
	{0x9494C, 0x514243, 0x0},
	{0x94950, 0x524344, 0x0},
	{0x94954, 0x534445, 0x0},
	{0x94958, 0x544546, 0x0},
	{0x9495C, 0x554546, 0x0},
	{0x94960, 0x564648, 0x0},
	{0x94964, 0x574748, 0x0},
	{0x94968, 0x584849, 0x0},
	{0x9496C, 0x5A4A4A, 0x0},
	{0x94970, 0x5C4B4A, 0x0},
	{0x94974, 0x5D4C4B, 0x0},
	{0x94978, 0x5E4D4C, 0x0},
	{0x9497C, 0x5F4E4D, 0x0},
	{0x94980, 0x604F4F, 0x0},
	{0x94984, 0x615050, 0x0},
	{0x94988, 0x625050, 0x0},
	{0x9498C, 0x635151, 0x0},
	{0x94990, 0x645252, 0x0},
	{0x94994, 0x655353, 0x0},
	{0x94998, 0x665454, 0x0},
	{0x9499C, 0x675556, 0x0},
	{0x949A0, 0x685657, 0x0},
	{0x949A4, 0x695758, 0x0},
	{0x949A8, 0x6A5759, 0x0},
	{0x949AC, 0x6B585A, 0x0},
	{0x949B0, 0x6C595B, 0x0},
	{0x949B4, 0x6D5A5D, 0x0},
	{0x949B8, 0x6E5B5E, 0x0},
	{0x949BC, 0x6F5C5F, 0x0},
	{0x949C0, 0x705D60, 0x0},
	{0x949C4, 0x715D61, 0x0},
	{0x949C8, 0x725E62, 0x0},
	{0x949CC, 0x735F63, 0x0},
	{0x949D0, 0x746064, 0x0},
	{0x949D4, 0x756166, 0x0},
	{0x949D8, 0x766267, 0x0},
	{0x949DC, 0x776368, 0x0},
	{0x949E0, 0x786469, 0x0},
	{0x949E4, 0x79646A, 0x0},
	{0x949E8, 0x7A656B, 0x0},
	{0x949EC, 0x7B666C, 0x0},
	{0x949F0, 0x7C676E, 0x0},
	{0x949F4, 0x7D686F, 0x0},
	{0x949F8, 0x7E6970, 0x0},
	{0x949FC, 0x7F6A71, 0x0},
	{0x94A00, 0x806B72, 0x0},
	{0x94A04, 0x816B74, 0x0},
	{0x94A08, 0x826C75, 0x0},
	{0x94A0C, 0x836D76, 0x0},
	{0x94A10, 0x846E77, 0x0},
	{0x94A14, 0x856F78, 0x0},
	{0x94A18, 0x867079, 0x0},
	{0x94A1C, 0x87717B, 0x0},
	{0x94A20, 0x88727C, 0x0},
	{0x94A24, 0x89737D, 0x0},
	{0x94A28, 0x8A737E, 0x0},
	{0x94A2C, 0x8B747F, 0x0},
	{0x94A30, 0x8C7581, 0x0},
	{0x94A34, 0x8D7682, 0x0},
	{0x94A38, 0x8E7783, 0x0},
	{0x94A3C, 0x8F7884, 0x0},
	{0x94A40, 0x907985, 0x0},
	{0x94A44, 0x917A87, 0x0},
	{0x94A48, 0x927B88, 0x0},
	{0x94A4C, 0x937B89, 0x0},
	{0x94A50, 0x947C8A, 0x0},
	{0x94A54, 0x957D8B, 0x0},
	{0x94A58, 0x967E8D, 0x0},
	{0x94A5C, 0x977F8E, 0x0},
	{0x94A60, 0x98808F, 0x0},
	{0x94A64, 0x998190, 0x0},
	{0x94A68, 0x9A8291, 0x0},
	{0x94A6C, 0x9B8393, 0x0},
	{0x94A70, 0x9C8494, 0x0},
	{0x94A74, 0x9D8595, 0x0},
	{0x94A78, 0x9E8696, 0x0},
	{0x94A7C, 0x9F8697, 0x0},
	{0x94A80, 0xA08798, 0x0},
	{0x94A84, 0xA18899, 0x0},
	{0x94A88, 0xA2899B, 0x0},
	{0x94A8C, 0xA38A9C, 0x0},
	{0x94A90, 0xA48B9D, 0x0},
	{0x94A94, 0xA58C9E, 0x0},
	{0x94A98, 0xA68D9F, 0x0},
	{0x94A9C, 0xA78EA0, 0x0},
	{0x94AA0, 0xA88FA1, 0x0},
	{0x94AA4, 0xA990A2, 0x0},
	{0x94AA8, 0xAA91A4, 0x0},
	{0x94AAC, 0xAB92A5, 0x0},
	{0x94AB0, 0xAC93A6, 0x0},
	{0x94AB4, 0xAD94A7, 0x0},
	{0x94AB8, 0xAE95A8, 0x0},
	{0x94ABC, 0xAF96A9, 0x0},
	{0x94AC0, 0xB097AA, 0x0},
	{0x94AC4, 0xB198AB, 0x0},
	{0x94AC8, 0xB299AC, 0x0},
	{0x94ACC, 0xB39AAD, 0x0},
	{0x94AD0, 0xB49BAE, 0x0},
	{0x94AD4, 0xB59CAF, 0x0},
	{0x94AD8, 0xB69DB0, 0x0},
	{0x94ADC, 0xB79EB1, 0x0},
	{0x94AE0, 0xB89FB2, 0x0},
	{0x94AE4, 0xB9A0B3, 0x0},
	{0x94AE8, 0xBAA1B4, 0x0},
	{0x94AEC, 0xBBA2B5, 0x0},
	{0x94AF0, 0xBCA3B6, 0x0},
	{0x94AF4, 0xBDA4B7, 0x0},
	{0x94AF8, 0xBEA5B8, 0x0},
	{0x94AFC, 0xBFA6B9, 0x0},
	{0x94B00, 0xC0A7BA, 0x0},
	{0x94B04, 0xC1A8BB, 0x0},
	{0x94B08, 0xC2A9BC, 0x0},
	{0x94B0C, 0xC3AABD, 0x0},
	{0x94B10, 0xC4ACBE, 0x0},
	{0x94B14, 0xC5ADBF, 0x0},
	{0x94B18, 0xC6AEC0, 0x0},
	{0x94B1C, 0xC7AFC1, 0x0},
	{0x94B20, 0xC8B0C2, 0x0},
	{0x94B24, 0xC9B1C3, 0x0},
	{0x94B28, 0xCAB2C4, 0x0},
	{0x94B2C, 0xCBB3C5, 0x0},
	{0x94B30, 0xCCB5C6, 0x0},
	{0x94B34, 0xCDB6C6, 0x0},
	{0x94B38, 0xCEB7C7, 0x0},
	{0x94B3C, 0xCFB8C8, 0x0},
	{0x94B40, 0xD0B9C9, 0x0},
	{0x94B44, 0xD1BBCA, 0x0},
	{0x94B48, 0xD2BCCB, 0x0},
	{0x94B4C, 0xD3BDCC, 0x0},
	{0x94B50, 0xD4BECD, 0x0},
	{0x94B54, 0xD5BFCE, 0x0},
	{0x94B58, 0xD6C1CF, 0x0},
	{0x94B5C, 0xD7C2D0, 0x0},
	{0x94B60, 0xD8C3D1, 0x0},
	{0x94B64, 0xD9C4D2, 0x0},
	{0x94B68, 0xDAC6D3, 0x0},
	{0x94B6C, 0xDBC7D3, 0x0},
	{0x94B70, 0xDCC8D4, 0x0},
	{0x94B74, 0xDDCAD5, 0x0},
	{0x94B78, 0xDECBD6, 0x0},
	{0x94B7C, 0xDFCCD7, 0x0},
	{0x94B80, 0xE0CED8, 0x0},
	{0x94B84, 0xE1CFD9, 0x0},
	{0x94B88, 0xE2D1DA, 0x0},
	{0x94B8C, 0xE3D2DB, 0x0},
	{0x94B90, 0xE4D3DC, 0x0},
	{0x94B94, 0xE5D5DD, 0x0},
	{0x94B98, 0xE6D6DE, 0x0},
	{0x94B9C, 0xE7D8DF, 0x0},
	{0x94BA0, 0xE8D9E0, 0x0},
	{0x94BA4, 0xE9DBE2, 0x0},
	{0x94BA8, 0xEADCE3, 0x0},
	{0x94BAC, 0xEBDEE4, 0x0},
	{0x94BB0, 0xECDFE5, 0x0},
	{0x94BB4, 0xEDE1E6, 0x0},
	{0x94BB8, 0xEEE2E7, 0x0},
	{0x94BBC, 0xEFE4E8, 0x0},
	{0x94BC0, 0xF0E5EA, 0x0},
	{0x94BC4, 0xF1E7EB, 0x0},
	{0x94BC8, 0xF2E9EC, 0x0},
	{0x94BCC, 0xF3EAED, 0x0},
	{0x94BD0, 0xF4ECEF, 0x0},
	{0x94BD4, 0xF5EDF0, 0x0},
	{0x94BD8, 0xF6EFF1, 0x0},
	{0x94BDC, 0xF7F1F3, 0x0},
	{0x94BE0, 0xF8F3F4, 0x0},
	{0x94BE4, 0xF9F4F6, 0x0},
	{0x94BE8, 0xFAF6F7, 0x0},
	{0x94BEC, 0xFBF8F9, 0x0},
	{0x94BF0, 0xFCFAFA, 0x0},
	{0x94BF4, 0xFDFBFC, 0x0},
	{0x94BF8, 0xFEFDFD, 0x0},
	{0x94BFC, 0xFFFFFF, 0x0},
	{0x90070, 0x1F, 0x0},
};

struct mdp_reg pyd_sharp_gamma[] = {
	{0x94800, 0x000000, 0x0},
	{0x94804, 0x010101, 0x0},
	{0x94808, 0x020202, 0x0},
	{0x9480C, 0x030303, 0x0},
	{0x94810, 0x040404, 0x0},
	{0x94814, 0x050505, 0x0},
	{0x94818, 0x060606, 0x0},
	{0x9481C, 0x070707, 0x0},
	{0x94820, 0x080808, 0x0},
	{0x94824, 0x090A09, 0x0},
	{0x94828, 0x0A0B0A, 0x0},
	{0x9482C, 0x0B0C0B, 0x0},
	{0x94830, 0x0C0D0C, 0x0},
	{0x94834, 0x0D0E0D, 0x0},
	{0x94838, 0x0E0F0E, 0x0},
	{0x9483C, 0x0F100F, 0x0},
	{0x94840, 0x101010, 0x0},
	{0x94844, 0x111111, 0x0},
	{0x94848, 0x121212, 0x0},
	{0x9484C, 0x131313, 0x0},
	{0x94850, 0x141414, 0x0},
	{0x94854, 0x151515, 0x0},
	{0x94858, 0x161616, 0x0},
	{0x9485C, 0x171717, 0x0},
	{0x94860, 0x181818, 0x0},
	{0x94864, 0x191919, 0x0},
	{0x94868, 0x1A191A, 0x0},
	{0x9486C, 0x1B1A1B, 0x0},
	{0x94870, 0x1C1B1C, 0x0},
	{0x94874, 0x1D1C1D, 0x0},
	{0x94878, 0x1E1D1E, 0x0},
	{0x9487C, 0x1F1E1F, 0x0},
	{0x94880, 0x201F20, 0x0},
	{0x94884, 0x211F21, 0x0},
	{0x94888, 0x222022, 0x0},
	{0x9488C, 0x232123, 0x0},
	{0x94890, 0x242224, 0x0},
	{0x94894, 0x252325, 0x0},
	{0x94898, 0x262426, 0x0},
	{0x9489C, 0x272427, 0x0},
	{0x948A0, 0x282528, 0x0},
	{0x948A4, 0x292629, 0x0},
	{0x948A8, 0x2A272A, 0x0},
	{0x948AC, 0x2B282B, 0x0},
	{0x948B0, 0x2C282C, 0x0},
	{0x948B4, 0x2D292D, 0x0},
	{0x948B8, 0x2E2A2E, 0x0},
	{0x948BC, 0x2F2B2F, 0x0},
	{0x948C0, 0x302C30, 0x0},
	{0x948C4, 0x312D31, 0x0},
	{0x948C8, 0x322D32, 0x0},
	{0x948CC, 0x332E33, 0x0},
	{0x948D0, 0x342F34, 0x0},
	{0x948D4, 0x353035, 0x0},
	{0x948D8, 0x363136, 0x0},
	{0x948DC, 0x373137, 0x0},
	{0x948E0, 0x383238, 0x0},
	{0x948E4, 0x393339, 0x0},
	{0x948E8, 0x3A343A, 0x0},
	{0x948EC, 0x3B353B, 0x0},
	{0x948F0, 0x3C363C, 0x0},
	{0x948F4, 0x3D363D, 0x0},
	{0x948F8, 0x3E373E, 0x0},
	{0x948FC, 0x3F383F, 0x0},
	{0x94900, 0x403940, 0x0},
	{0x94904, 0x413A41, 0x0},
	{0x94908, 0x423B42, 0x0},
	{0x9490C, 0x433B43, 0x0},
	{0x94910, 0x443C44, 0x0},
	{0x94914, 0x453D45, 0x0},
	{0x94918, 0x463E46, 0x0},
	{0x9491C, 0x473F47, 0x0},
	{0x94920, 0x484048, 0x0},
	{0x94924, 0x494149, 0x0},
	{0x94928, 0x4A414A, 0x0},
	{0x9492C, 0x4B424B, 0x0},
	{0x94930, 0x4C434C, 0x0},
	{0x94934, 0x4D444D, 0x0},
	{0x94938, 0x4E454E, 0x0},
	{0x9493C, 0x4F464F, 0x0},
	{0x94940, 0x504750, 0x0},
	{0x94944, 0x514851, 0x0},
	{0x94948, 0x524952, 0x0},
	{0x9494C, 0x534953, 0x0},
	{0x94950, 0x544A54, 0x0},
	{0x94954, 0x554B55, 0x0},
	{0x94958, 0x564C56, 0x0},
	{0x9495C, 0x574D57, 0x0},
	{0x94960, 0x584E58, 0x0},
	{0x94964, 0x594F59, 0x0},
	{0x94968, 0x5A505A, 0x0},
	{0x9496C, 0x5B515B, 0x0},
	{0x94970, 0x5C525C, 0x0},
	{0x94974, 0x5D535D, 0x0},
	{0x94978, 0x5E535E, 0x0},
	{0x9497C, 0x5F545F, 0x0},
	{0x94980, 0x605560, 0x0},
	{0x94984, 0x615661, 0x0},
	{0x94988, 0x625762, 0x0},
	{0x9498C, 0x635863, 0x0},
	{0x94990, 0x645964, 0x0},
	{0x94994, 0x655A65, 0x0},
	{0x94998, 0x665B66, 0x0},
	{0x9499C, 0x675C67, 0x0},
	{0x949A0, 0x685D68, 0x0},
	{0x949A4, 0x695E69, 0x0},
	{0x949A8, 0x6A5F6A, 0x0},
	{0x949AC, 0x6B606B, 0x0},
	{0x949B0, 0x6C616C, 0x0},
	{0x949B4, 0x6D616D, 0x0},
	{0x949B8, 0x6E626E, 0x0},
	{0x949BC, 0x6F636F, 0x0},
	{0x949C0, 0x706470, 0x0},
	{0x949C4, 0x716571, 0x0},
	{0x949C8, 0x726672, 0x0},
	{0x949CC, 0x736773, 0x0},
	{0x949D0, 0x746874, 0x0},
	{0x949D4, 0x756975, 0x0},
	{0x949D8, 0x766A76, 0x0},
	{0x949DC, 0x776B77, 0x0},
	{0x949E0, 0x786C78, 0x0},
	{0x949E4, 0x796D79, 0x0},
	{0x949E8, 0x7A6E7A, 0x0},
	{0x949EC, 0x7B6F7B, 0x0},
	{0x949F0, 0x7C707C, 0x0},
	{0x949F4, 0x7D717D, 0x0},
	{0x949F8, 0x7E717E, 0x0},
	{0x949FC, 0x7F727F, 0x0},
	{0x94A00, 0x807380, 0x0},
	{0x94A04, 0x817481, 0x0},
	{0x94A08, 0x827582, 0x0},
	{0x94A0C, 0x837683, 0x0},
	{0x94A10, 0x847784, 0x0},
	{0x94A14, 0x857885, 0x0},
	{0x94A18, 0x867986, 0x0},
	{0x94A1C, 0x877A87, 0x0},
	{0x94A20, 0x887B88, 0x0},
	{0x94A24, 0x897C89, 0x0},
	{0x94A28, 0x8A7D8A, 0x0},
	{0x94A2C, 0x8B7D8B, 0x0},
	{0x94A30, 0x8C7E8C, 0x0},
	{0x94A34, 0x8D7F8D, 0x0},
	{0x94A38, 0x8E808E, 0x0},
	{0x94A3C, 0x8F818F, 0x0},
	{0x94A40, 0x908290, 0x0},
	{0x94A44, 0x918391, 0x0},
	{0x94A48, 0x928492, 0x0},
	{0x94A4C, 0x938593, 0x0},
	{0x94A50, 0x948694, 0x0},
	{0x94A54, 0x958695, 0x0},
	{0x94A58, 0x968796, 0x0},
	{0x94A5C, 0x978897, 0x0},
	{0x94A60, 0x988998, 0x0},
	{0x94A64, 0x998A99, 0x0},
	{0x94A68, 0x9A8B9A, 0x0},
	{0x94A6C, 0x9B8C9B, 0x0},
	{0x94A70, 0x9C8D9C, 0x0},
	{0x94A74, 0x9D8D9D, 0x0},
	{0x94A78, 0x9E8E9E, 0x0},
	{0x94A7C, 0x9F8F9F, 0x0},
	{0x94A80, 0xA090A0, 0x0},
	{0x94A84, 0xA191A1, 0x0},
	{0x94A88, 0xA292A2, 0x0},
	{0x94A8C, 0xA393A3, 0x0},
	{0x94A90, 0xA493A4, 0x0},
	{0x94A94, 0xA594A5, 0x0},
	{0x94A98, 0xA695A6, 0x0},
	{0x94A9C, 0xA796A7, 0x0},
	{0x94AA0, 0xA897A8, 0x0},
	{0x94AA4, 0xA998A9, 0x0},
	{0x94AA8, 0xAA99AA, 0x0},
	{0x94AAC, 0xAB99AB, 0x0},
	{0x94AB0, 0xAC9AAC, 0x0},
	{0x94AB4, 0xAD9BAD, 0x0},
	{0x94AB8, 0xAE9CAE, 0x0},
	{0x94ABC, 0xAF9DAF, 0x0},
	{0x94AC0, 0xB09EB0, 0x0},
	{0x94AC4, 0xB19EB1, 0x0},
	{0x94AC8, 0xB29FB2, 0x0},
	{0x94ACC, 0xB3A0B3, 0x0},
	{0x94AD0, 0xB4A1B4, 0x0},
	{0x94AD4, 0xB5A2B5, 0x0},
	{0x94AD8, 0xB6A3B6, 0x0},
	{0x94ADC, 0xB7A3B7, 0x0},
	{0x94AE0, 0xB8A4B8, 0x0},
	{0x94AE4, 0xB9A5B9, 0x0},
	{0x94AE8, 0xBAA6BA, 0x0},
	{0x94AEC, 0xBBA7BB, 0x0},
	{0x94AF0, 0xBCA8BC, 0x0},
	{0x94AF4, 0xBDA8BD, 0x0},
	{0x94AF8, 0xBEA9BE, 0x0},
	{0x94AFC, 0xBFAABF, 0x0},
	{0x94B00, 0xC0ABC0, 0x0},
	{0x94B04, 0xC1ACC1, 0x0},
	{0x94B08, 0xC2ADC2, 0x0},
	{0x94B0C, 0xC3ADC3, 0x0},
	{0x94B10, 0xC4AEC4, 0x0},
	{0x94B14, 0xC5AFC5, 0x0},
	{0x94B18, 0xC6B0C6, 0x0},
	{0x94B1C, 0xC7B1C7, 0x0},
	{0x94B20, 0xC8B2C8, 0x0},
	{0x94B24, 0xC9B3C9, 0x0},
	{0x94B28, 0xCAB4CA, 0x0},
	{0x94B2C, 0xCBB5CB, 0x0},
	{0x94B30, 0xCCB6CC, 0x0},
	{0x94B34, 0xCDB6CD, 0x0},
	{0x94B38, 0xCEB7CE, 0x0},
	{0x94B3C, 0xCFB8CF, 0x0},
	{0x94B40, 0xD0B9D0, 0x0},
	{0x94B44, 0xD1BAD1, 0x0},
	{0x94B48, 0xD2BBD2, 0x0},
	{0x94B4C, 0xD3BCD3, 0x0},
	{0x94B50, 0xD4BDD4, 0x0},
	{0x94B54, 0xD5BED5, 0x0},
	{0x94B58, 0xD6BFD6, 0x0},
	{0x94B5C, 0xD7C0D7, 0x0},
	{0x94B60, 0xD8C1D8, 0x0},
	{0x94B64, 0xD9C2D9, 0x0},
	{0x94B68, 0xDAC3DA, 0x0},
	{0x94B6C, 0xDBC5DB, 0x0},
	{0x94B70, 0xDCC6DC, 0x0},
	{0x94B74, 0xDDC7DD, 0x0},
	{0x94B78, 0xDEC8DE, 0x0},
	{0x94B7C, 0xDFC9DF, 0x0},
	{0x94B80, 0xE0CAE0, 0x0},
	{0x94B84, 0xE1CCE1, 0x0},
	{0x94B88, 0xE2CDE2, 0x0},
	{0x94B8C, 0xE3CEE3, 0x0},
	{0x94B90, 0xE4CFE4, 0x0},
	{0x94B94, 0xE5D1E5, 0x0},
	{0x94B98, 0xE6D2E6, 0x0},
	{0x94B9C, 0xE7D3E7, 0x0},
	{0x94BA0, 0xE8D5E8, 0x0},
	{0x94BA4, 0xE9D6E9, 0x0},
	{0x94BA8, 0xEAD8EA, 0x0},
	{0x94BAC, 0xEBD9EB, 0x0},
	{0x94BB0, 0xECDBEC, 0x0},
	{0x94BB4, 0xEDDCED, 0x0},
	{0x94BB8, 0xEEDEEE, 0x0},
	{0x94BBC, 0xEFDFEF, 0x0},
	{0x94BC0, 0xF0E1F0, 0x0},
	{0x94BC4, 0xF1E3F1, 0x0},
	{0x94BC8, 0xF2E4F2, 0x0},
	{0x94BCC, 0xF3E6F3, 0x0},
	{0x94BD0, 0xF4E8F4, 0x0},
	{0x94BD4, 0xF5EAF5, 0x0},
	{0x94BD8, 0xF6ECF6, 0x0},
	{0x94BDC, 0xF7EDF7, 0x0},
	{0x94BE0, 0xF8EFF8, 0x0},
	{0x94BE4, 0xF9F1F9, 0x0},
	{0x94BE8, 0xFAF3FA, 0x0},
	{0x94BEC, 0xFBF6FB, 0x0},
	{0x94BF0, 0xFCF8FC, 0x0},
	{0x94BF4, 0xFDFAFD, 0x0},
	{0x94BF8, 0xFEFCFE, 0x0},
	{0x94BFC, 0xFFFEFF, 0x0},
	{0x90070, 0x1F, 0x0},
};

int pyd_mdp_color_enhance(void)
{
	mdp_color_enhancement(pyd_color_v11, ARRAY_SIZE(pyd_color_v11));

	return 0;
}

int pyd_mdp_gamma(void)
{
	if (panel_type == PANEL_ID_PYD_SHARP)
		mdp_color_enhancement(pyd_sharp_gamma, ARRAY_SIZE(pyd_sharp_gamma));
	else
		mdp_color_enhancement(pyd_auo_gamma, ARRAY_SIZE(pyd_auo_gamma));

	return 0;
}

#if defined (CONFIG_FB_MSM_MDP_ABL)
static struct gamma_curvy gamma_tbl = {
	.gamma_len = 33,
	.bl_len = 8,
	.ref_y_gamma = {0, 1, 1, 2, 3, 5, 10, 15, 20, 28, 39, 51,
					66, 84, 104, 127, 151, 178, 207, 240, 279,
					319, 358, 417, 473, 527, 583, 650, 719, 779,
					853, 943, 1024},
	.ref_y_shade = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352,
					384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704,
					736, 768, 800, 832, 864, 896, 928, 960, 992, 1024},
	.ref_bl_lvl = {0, 50, 102, 152, 237, 371, 703, 1024},
	.ref_y_lvl = {0, 138, 218, 298, 424, 601, 818, 1024},
};
#endif

static struct msm_panel_common_pdata mdp_pdata = {
	.gpio = 28,
	.mdp_core_clk_rate = 200000000,
	.mdp_core_clk_table = mdp_core_clk_rate_table,
	.num_mdp_clk = ARRAY_SIZE(mdp_core_clk_rate_table),
#ifdef CONFIG_MSM_BUS_SCALING
	.mdp_bus_scale_table = &mdp_bus_scale_pdata,
#endif
	.mdp_color_enhance = pyd_mdp_color_enhance,
	.mdp_gamma = pyd_mdp_gamma,
#if defined (CONFIG_FB_MSM_MDP_ABL)
	.abl_gamma_tbl = &gamma_tbl,
#endif
};

static void __init msm_fb_add_devices(void)
{
	printk(KERN_INFO "panel ID= 0x%x\n", panel_type);
	msm_fb_register_device("mdp", &mdp_pdata);

	msm_fb_register_device("lcdc", &lcdc_pdata);
	if (panel_type != PANEL_ID_NONE)
		msm_fb_register_device("mipi_dsi", &mipi_pdata);
#ifdef CONFIG_MSM_BUS_SCALING
	msm_fb_register_device("dtv", &dtv_pdata);
#endif
}

/*
TODO:
1.find a better way to handle msm_fb_resources, to avoid passing it across file.
2.error handling
 */
int __init pyd_init_panel(struct resource *res, size_t size)
{
	int ret;

	PR_DISP_INFO("%s: res=%p, size=%d\n", __func__, res, size);
	if (panel_type == PANEL_ID_PYD_SHARP)
		mipi_novatek_panel_data.shrink_pwm = pyd_shp_shrink_pwm;
	else
		mipi_novatek_panel_data.shrink_pwm = pyd_auo_shrink_pwm;

	if (panel_type == PANEL_ID_PYD_SHARP)
		mdp_pdata.color_enhancment_tbl = pyd_sharp_gamma;
	else
		mdp_pdata.color_enhancment_tbl = pyd_auo_gamma;

	msm_fb_device.resource = res;
	msm_fb_device.num_resources = size;

#if 1
	/* Cancel the fixup temporally due to it's cause flicking problem. */
	if (panel_type == PANEL_ID_PYD_AUO_NT)
		mipi_pdata.esd_fixup = pyd_esd_fixup;
#endif

	ret = platform_device_register(&msm_fb_device);
	ret = platform_device_register(&lcdc_samsung_panel_device);
	ret = platform_device_register(&mipi_dsi_video_sharp_wvga_panel_device);
	ret = platform_device_register(&mipi_dsi_cmd_sharp_qhd_panel_device);

	msm_fb_add_devices();

	return 0;
}
