/* linux/arch/arm/mach-msm/board-pyramid-audio.c
 *
 * Copyright (C) 2010-2011 HTC Corporation.
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

#include <linux/android_pmem.h>
#include <linux/mfd/pmic8058.h>
#include <linux/mfd/marimba.h>
#include <linux/delay.h>
#include <linux/pmic8058-othc.h>
#include <linux/spi/spi_aic3254.h>

#include <mach/gpio.h>
#include <mach/dal.h>
#include <mach/tpa2051d3.h>
#include <mach/qdsp6v2_1x/snddev_icodec.h>
#include <mach/qdsp6v2_1x/snddev_ecodec.h>
#include <mach/qdsp6v2_1x/snddev_hdmi.h>
#include <mach/qdsp6v2_1x/apr_audio.h>
#include <mach/qdsp6v2_1x/q6asm.h>
#include <mach/htc_acoustic_8x60.h>

#include "board-pyramid-audio-data.h"
#include <mach/qdsp6v2_1x/audio_dev_ctl.h>

static struct mutex bt_sco_lock;
static struct mutex mic_lock;
static int curr_rx_mode;
static atomic_t aic3254_ctl = ATOMIC_INIT(0);
static atomic_t q6_effect_mode = ATOMIC_INIT(-1);

#define BIT_SPEAKER	(1 << 0)
#define BIT_HEADSET	(1 << 1)
#define BIT_RECEIVER	(1 << 2)
#define BIT_FM_SPK	(1 << 3)
#define BIT_FM_HS	(1 << 4)
#define PYRAMID_AUD_CODEC_RST        (67)
#define PYRAMID_AUD_HP_EN          PMGPIO(18)
#define PYRAMID_AUD_MIC_SEL        PMGPIO(26)
#define PM8058_GPIO_BASE			NR_MSM_GPIOS
#define PM8058_GPIO_PM_TO_SYS(pm_gpio)		(pm_gpio + PM8058_GPIO_BASE)
#define PMGPIO(x) (x-1)
void pyramid_snddev_bmic_pamp_on(int en);
static uint32_t msm_aic3254_reset_gpio[] = {
	GPIO_CFG(PYRAMID_AUD_CODEC_RST, 0, GPIO_CFG_OUTPUT,
		GPIO_CFG_PULL_UP, GPIO_CFG_8MA),
};

void pyramid_snddev_poweramp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		msleep(50);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 1);
		set_speaker_amp(1);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode |= BIT_SPEAKER;
	} else {
		set_speaker_amp(0);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 0);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode &= ~BIT_SPEAKER;
	}
}

void pyramid_snddev_hsed_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		msleep(50);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 1);
		set_headset_amp(1);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode |= BIT_HEADSET;
	} else {
		set_headset_amp(0);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 0);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode &= ~BIT_HEADSET;
	}
}

void pyramid_snddev_hs_spk_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		msleep(50);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 1);
		set_speaker_headset_amp(1);
		if (!atomic_read(&aic3254_ctl)) {
			curr_rx_mode |= BIT_SPEAKER;
			curr_rx_mode |= BIT_HEADSET;
		}
	} else {
		set_speaker_headset_amp(0);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 0);
		if (!atomic_read(&aic3254_ctl)) {
			curr_rx_mode &= ~BIT_SPEAKER;
			curr_rx_mode &= ~BIT_HEADSET;
		}
	}
}

void pyramid_snddev_receiver_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode |= BIT_RECEIVER;
	} else {
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode &= ~BIT_RECEIVER;
	}
}

void pyramid_snddev_bt_sco_pamp_on(int en)
{
	/* to be implemented */
}

/* power on/off externnal mic bias */
void pyramid_mic_enable(int en, int shift)
{
	pr_aud_info("%s: %d, shift %d\n", __func__, en, shift);

	mutex_lock(&mic_lock);

	if (en)
		pm8058_micbias_enable(OTHC_MICBIAS_2, OTHC_SIGNAL_ALWAYS_ON);
	else
		pm8058_micbias_enable(OTHC_MICBIAS_2, OTHC_SIGNAL_OFF);

	mutex_unlock(&mic_lock);
}

void pyramid_snddev_imic_pamp_on(int en)
{
	int ret;

	pr_aud_info("%s %d\n", __func__, en);
	pyramid_snddev_bmic_pamp_on(en);
	if (en) {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_0, OTHC_SIGNAL_ALWAYS_ON);
		if (ret)
			pr_aud_err("%s: Enabling int mic power failed\n", __func__);

	} else {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_0, OTHC_SIGNAL_OFF);
		if (ret)
			pr_aud_err("%s: Enabling int mic power failed\n", __func__);
	}
}

void pyramid_snddev_bmic_pamp_on(int en)
{
	int ret;

	pr_aud_info("%s %d\n", __func__, en);

	if (en) {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_ALWAYS_ON);
		if (ret)
			pr_aud_err("%s: Enabling back mic power failed\n", __func__);

		/* select internal mic path */
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_MIC_SEL),
						"AUD_MIC_SEL");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_MIC_SEL), 0);
	} else {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
		if (ret)
			pr_aud_err("%s: Enabling back mic power failed\n", __func__);
	}
}

void pyramid_snddev_emic_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);

	if (en) {
		/* select external mic path */
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_MIC_SEL),
						"AUD_MIC_SEL");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_MIC_SEL), 1);
	}
}

void pyramid_snddev_stereo_mic_pamp_on(int en)
{
	int ret;

	pr_aud_info("%s %d\n", __func__, en);

	if (en) {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_0, OTHC_SIGNAL_ALWAYS_ON);
		if (ret)
			pr_aud_err("%s: Enabling int mic power failed\n", __func__);

		ret = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_ALWAYS_ON);
		if (ret)
			pr_aud_err("%s: Enabling back mic power failed\n", __func__);

		/* select external mic path */
		gpio_set_value(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_MIC_SEL), 0);
	} else {
		ret = pm8058_micbias_enable(OTHC_MICBIAS_0, OTHC_SIGNAL_OFF);
		if (ret)
			pr_aud_err("%s: Disabling int mic power failed\n", __func__);


		ret = pm8058_micbias_enable(OTHC_MICBIAS_1, OTHC_SIGNAL_OFF);
		if (ret)
			pr_aud_err("%s: Disabling back mic power failed\n", __func__);
	}
}

void pyramid_snddev_fmspk_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 1);
		set_speaker_amp(1);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode |= BIT_FM_SPK;
	} else {
		set_speaker_amp(0);
		gpio_request(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN),
						"AUD_HP_EN");
		gpio_direction_output(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 0);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode &= ~BIT_FM_SPK;
	}
}

void pyramid_snddev_fmhs_pamp_on(int en)
{
	pr_aud_info("%s %d\n", __func__, en);
	if (en) {
		gpio_set_value(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 1);
		set_headset_amp(1);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode |= BIT_FM_HS;
	} else {
		set_headset_amp(0);
		gpio_set_value(PM8058_GPIO_PM_TO_SYS(PYRAMID_AUD_HP_EN), 0);
		if (!atomic_read(&aic3254_ctl))
			curr_rx_mode &= ~BIT_FM_HS;
	}
}

void pyramid_voltage_on(int en)
{
}

int pyramid_get_rx_vol(uint8_t hw, int network, int level)
{
	int vol = 0;

	/* to be implemented */

	pr_aud_info("%s(%d, %d, %d) => %d\n", __func__, hw, network, level, vol);

	return vol;
}

void pyramid_rx_amp_enable(int en)
{
	if (curr_rx_mode != 0) {
		atomic_set(&aic3254_ctl, 1);
		pr_aud_info("%s: curr_rx_mode 0x%x, en %d\n",
			__func__, curr_rx_mode, en);
		if (curr_rx_mode & BIT_SPEAKER)
			pyramid_snddev_poweramp_on(en);
		if (curr_rx_mode & BIT_HEADSET)
			pyramid_snddev_hsed_pamp_on(en);
		if (curr_rx_mode & BIT_RECEIVER)
			pyramid_snddev_receiver_pamp_on(en);
		if (curr_rx_mode & BIT_FM_SPK)
			pyramid_snddev_fmspk_pamp_on(en);
		if (curr_rx_mode & BIT_FM_HS)
			pyramid_snddev_fmhs_pamp_on(en);
		atomic_set(&aic3254_ctl, 0);;
	}
}

int pyramid_support_aic3254(void)
{
	return 1;
}

int pyramid_support_back_mic(void)
{
	return 1;
}

void pyramid_get_acoustic_tables(struct acoustic_tables *tb)
{
	switch (system_rev) {
	case 0:
	case 1:
		strcpy(tb->tpa2051, "TPA2051_CFG.csv");
		break;
	case 2:
		strcpy(tb->tpa2051, "TPA2051_CFG_XC.csv");
		break;
	default:
		strcpy(tb->tpa2051, "TPA2051_CFG_XC.csv");
		break;
	}
}

int pyramid_support_beats(void)
{
	/* this means HW support 1V for beats */
	/* PCB ID is defined by HW revision
	 * [0] raised means support 1V headset
	 * [7-4] set as "1000" stands for PVT device */
	if (((system_rev&0x1) == 0x1) && ((system_rev>>4&0xF) == 0x8))
		return 1;
	else
		return 0;
}

void pyramid_enable_beats(int en)
{
	pr_aud_info("%s: %d\n", __func__, en);
	set_beats_on(en);
}

int pyramid_is_msm_i2s_slave(void)
{
	/* 1 - CPU slave, 0 - CPU master */
	return 1;
}

void pyramid_reset_3254(void)
{
	gpio_tlmm_config(msm_aic3254_reset_gpio[0], GPIO_CFG_ENABLE);
	gpio_set_value(PYRAMID_AUD_CODEC_RST, 0);
	mdelay(1);
	gpio_set_value(PYRAMID_AUD_CODEC_RST, 1);
}

void pyramid_set_q6_effect_mode(int mode)
{
	pr_aud_info("%s: mode %d\n", __func__, mode);
	atomic_set(&q6_effect_mode, mode);
}

int pyramid_get_q6_effect_mode(void)
{
	int mode = atomic_read(&q6_effect_mode);
	pr_aud_info("%s: mode %d\n", __func__, mode);
	return mode;
}

static struct q6v2audio_analog_ops ops = {
	.speaker_enable	        = pyramid_snddev_poweramp_on,
	.headset_enable	        = pyramid_snddev_hsed_pamp_on,
	.handset_enable	        = pyramid_snddev_receiver_pamp_on,
	.headset_speaker_enable	= pyramid_snddev_hs_spk_pamp_on,
	.bt_sco_enable	        = pyramid_snddev_bt_sco_pamp_on,
	.int_mic_enable         = pyramid_snddev_imic_pamp_on,
	.back_mic_enable        = pyramid_snddev_bmic_pamp_on,
	.ext_mic_enable         = pyramid_snddev_emic_pamp_on,
	.stereo_mic_enable      = pyramid_snddev_stereo_mic_pamp_on,
	.fm_headset_enable      = pyramid_snddev_fmhs_pamp_on,
	.fm_speaker_enable      = pyramid_snddev_fmspk_pamp_on,
	.voltage_on		= pyramid_voltage_on,
};

static struct q6v2audio_icodec_ops iops = {
	.support_aic3254 = pyramid_support_aic3254,
	.is_msm_i2s_slave = pyramid_is_msm_i2s_slave,
};

static struct q6v2audio_ecodec_ops eops = {
	.bt_sco_enable  = pyramid_snddev_bt_sco_pamp_on,
};

static struct aic3254_ctl_ops cops = {
	.rx_amp_enable        = pyramid_rx_amp_enable,
	.reset_3254           = pyramid_reset_3254,
	.lb_dsp_init          = &LOOPBACK_DSP_INIT_PARAM,
	.lb_receiver_imic     = &LOOPBACK_Receiver_IMIC_PARAM,
	.lb_speaker_imic      = &LOOPBACK_Speaker_IMIC_PARAM,
	.lb_headset_emic      = &LOOPBACK_Headset_EMIC_PARAM,
	.lb_receiver_bmic     = &LOOPBACK_Receiver_BMIC_PARAM,
	.lb_speaker_bmic      = &LOOPBACK_Speaker_BMIC_PARAM,
	.lb_headset_bmic      = &LOOPBACK_Headset_BMIC_PARAM,
};

static struct acoustic_ops acoustic = {
	.enable_mic_bias = pyramid_mic_enable,
	.support_aic3254 = pyramid_support_aic3254,
	.support_back_mic = pyramid_support_back_mic,
	.get_acoustic_tables = pyramid_get_acoustic_tables,
	.support_beats = pyramid_support_beats,
	.enable_beats = pyramid_enable_beats,
	.set_q6_effect = pyramid_set_q6_effect_mode,
};

void pyramid_aic3254_set_mode(int config, int mode)
{
	aic3254_set_mode(config, mode);
}


static struct q6v2audio_aic3254_ops aops = {
       .aic3254_set_mode = pyramid_aic3254_set_mode,
};

static struct q6asm_ops qops = {
	.get_q6_effect = pyramid_get_q6_effect_mode,
};

void __init pyramid_audio_init(void)
{
	mutex_init(&bt_sco_lock);
	mutex_init(&mic_lock);

#ifdef CONFIG_MSM8X60_AUDIO
	pr_aud_info("%s\n", __func__);
	htc_8x60_register_analog_ops(&ops);
	htc_8x60_register_icodec_ops(&iops);
	htc_8x60_register_ecodec_ops(&eops);
	acoustic_register_ops(&acoustic);
	htc_8x60_register_aic3254_ops(&aops);
	htc_8x60_register_q6asm_ops(&qops);
	msm_set_voc_freq(8000, 8000);
#endif

	aic3254_register_ctl_ops(&cops);

	/* PMIC GPIO Init (See board-pyramid.c) */

	/* Reset AIC3254 */
	pyramid_reset_3254();
}
