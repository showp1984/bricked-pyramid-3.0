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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/mfd/pmic8058.h>
#include <linux/pmic8058-othc.h>
#include <linux/mfd/pmic8901.h>
#include <linux/mfd/msm-adie-codec.h>
#include <linux/regulator/pmic8058-regulator.h>
#include <linux/regulator/pmic8901-regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>

#include <mach/qdsp6v2_1x/audio_dev_ctl.h>
#include <mach/qdsp6v2_1x/apr_audio.h>
#include <mach/board.h>
#include <mach/mpp.h>
#include <asm/mach-types.h>
#include <asm/uaccess.h>
#include <mach/qdsp6v2_1x/snddev_icodec.h>
#include <mach/qdsp6v2_1x/snddev_ecodec.h>
#include "timpani_profile_8x60.h"

#ifdef CONFIG_MACH_VIGOR
#include "timpani_profile_8x60_vigor.h"
#else
#include "timpani_profile_8x60_lead.h"
#endif

#include <mach/qdsp6v2_1x/snddev_hdmi.h>
#include "snddev_mi2s.h"
#include <linux/spi/spi_aic3254.h>

#include "snddev_virtual.h"

#ifdef CONFIG_DEBUG_FS
static struct dentry *debugfs_hsed_config;
static void snddev_hsed_config_modify_setting(int type);
static void snddev_hsed_config_restore_setting(void);
#endif

/* define the value for BT_SCO */


#define SNDDEV_GPIO_MIC2_ANCR_SEL 294

static struct q6v2audio_analog_ops default_audio_ops;
static struct q6v2audio_analog_ops *audio_ops = &default_audio_ops;

void speaker_enable(int en)
{
	if (audio_ops->speaker_enable)
		audio_ops->speaker_enable(en);
}

void headset_enable(int en)
{
	if (audio_ops->headset_enable)
		audio_ops->headset_enable(en);
}

void handset_enable(int en)
{
	if (audio_ops->handset_enable)
		audio_ops->handset_enable(en);
}

void headset_speaker_enable(int en)
{
	if (audio_ops->headset_speaker_enable)
		audio_ops->headset_speaker_enable(en);
}

void int_mic_enable(int en)
{
	if (audio_ops->int_mic_enable)
		audio_ops->int_mic_enable(en);
}

void back_mic_enable(int en)
{
	if (audio_ops->back_mic_enable)
		audio_ops->back_mic_enable(en);
}

void ext_mic_enable(int en)
{
	if (audio_ops->ext_mic_enable)
		audio_ops->ext_mic_enable(en);
}

void stereo_mic_enable(int en)
{
	if (audio_ops->stereo_mic_enable)
		audio_ops->stereo_mic_enable(en);
}

void usb_headset_enable(int en)
{
	if (audio_ops->usb_headset_enable)
		audio_ops->usb_headset_enable(en);
}

void fm_headset_enable(int en)
{
	if (audio_ops->fm_headset_enable)
		audio_ops->fm_headset_enable(en);
}

void fm_speaker_enable(int en)
{
	if (audio_ops->fm_speaker_enable)
		audio_ops->fm_speaker_enable(en);
}

void voltage_on(int on)
{
	if (audio_ops->voltage_on)
		audio_ops->voltage_on(on);
}

static struct regulator *s3;
static struct regulator *mvs;

static void msm_snddev_enable_dmic_power(void)
{
	int ret;

	pr_aud_err("%s", __func__);
	s3 = regulator_get(NULL, "8058_s3");
	if (IS_ERR(s3))
		return;

	ret = regulator_set_voltage(s3, 1800000, 1800000);
	if (ret) {
		pr_aud_err("%s: error setting voltage\n", __func__);
		goto fail_s3;
	}

	ret = regulator_enable(s3);
	if (ret) {
		pr_aud_err("%s: error enabling regulator\n", __func__);
		goto fail_s3;
	}

	mvs = regulator_get(NULL, "8901_mvs0");
	if (IS_ERR(mvs))
		goto fail_mvs0_get;

	ret = regulator_enable(mvs);
	if (ret) {
		pr_aud_err("%s: error setting regulator\n", __func__);
		goto fail_mvs0_enable;
	}

	stereo_mic_enable(1);
	return;

fail_mvs0_enable:
	regulator_put(mvs);
	mvs = NULL;
fail_mvs0_get:
	regulator_disable(s3);
fail_s3:
	regulator_put(s3);
	s3 = NULL;
}

static void msm_snddev_disable_dmic_power(void)
{
	int ret;
	pr_aud_err("%s", __func__);

	if (mvs) {
		ret = regulator_disable(mvs);
		if (ret < 0)
			pr_aud_err("%s: error disabling vreg mvs\n", __func__);
		regulator_put(mvs);
		mvs = NULL;
	}

	if (s3) {
		ret = regulator_disable(s3);
		if (ret < 0)
			pr_aud_err("%s: error disabling regulator s3\n", __func__);
		regulator_put(s3);
		s3 = NULL;
	}
	stereo_mic_enable(0);

}

static void msm_snddev_dmic_power(int en)
{
	pr_aud_err("%s", __func__);
	if (en)
		msm_snddev_enable_dmic_power();
	else
		msm_snddev_disable_dmic_power();
}

/* GPIO_CLASS_D0_EN */
#define SNDDEV_GPIO_CLASS_D0_EN 227

/* GPIO_CLASS_D1_EN */
#define SNDDEV_GPIO_CLASS_D1_EN 229

#define PM8901_MPP_3 (2) /* PM8901 MPP starts from 0 */
static void config_class_d1_gpio(int enable)
{
	int rc;

	if (enable) {
		rc = gpio_request(SNDDEV_GPIO_CLASS_D1_EN, "CLASSD1_EN");
		if (rc) {
			pr_aud_err("%s: spkr pamp gpio %d request"
			"failed\n", __func__, SNDDEV_GPIO_CLASS_D1_EN);
			return;
		}
		gpio_direction_output(SNDDEV_GPIO_CLASS_D1_EN, 1);
		gpio_set_value_cansleep(SNDDEV_GPIO_CLASS_D1_EN, 1);
	} else {
		gpio_set_value_cansleep(SNDDEV_GPIO_CLASS_D1_EN, 0);
		gpio_free(SNDDEV_GPIO_CLASS_D1_EN);
	}
}

static void config_class_d0_gpio(int enable)
{
	int rc;

	if (enable) {
		rc = pm8901_mpp_config_digital_out(PM8901_MPP_3,
			PM8901_MPP_DIG_LEVEL_MSMIO, 1);

		if (rc) {
			pr_aud_err("%s: CLASS_D0_EN failed\n", __func__);
			return;
		}

		rc = gpio_request(SNDDEV_GPIO_CLASS_D0_EN, "CLASSD0_EN");

		if (rc) {
			pr_aud_err("%s: spkr pamp gpio pm8901 mpp3 request"
			"failed\n", __func__);
			pm8901_mpp_config_digital_out(PM8901_MPP_3,
			PM8901_MPP_DIG_LEVEL_MSMIO, 0);
			return;
		}

		gpio_direction_output(SNDDEV_GPIO_CLASS_D0_EN, 1);
		gpio_set_value(SNDDEV_GPIO_CLASS_D0_EN, 1);

	} else {
		pm8901_mpp_config_digital_out(PM8901_MPP_3,
		PM8901_MPP_DIG_LEVEL_MSMIO, 0);
		gpio_set_value(SNDDEV_GPIO_CLASS_D0_EN, 0);
		gpio_free(SNDDEV_GPIO_CLASS_D0_EN);
	}
}

void msm_snddev_poweramp_on(void)
{

	pr_debug("%s: enable stereo spkr amp\n", __func__);
	config_class_d0_gpio(1);
	config_class_d1_gpio(1);
}

void msm_snddev_poweramp_off(void)
{

	pr_debug("%s: disable stereo spkr amp\n", __func__);
	config_class_d0_gpio(0);
	config_class_d1_gpio(0);
	msleep(30);
}


static void msm_snddev_enable_dmic_sec_power(void)
{
	pr_aud_err("%s", __func__);
	msm_snddev_enable_dmic_power();

#ifdef CONFIG_PMIC8058_OTHC
	pm8058_micbias_enable(OTHC_MICBIAS_2, OTHC_SIGNAL_ALWAYS_ON);
#endif
}

static void msm_snddev_disable_dmic_sec_power(void)
{
	pr_aud_err("%s", __func__);
	msm_snddev_disable_dmic_power();

#ifdef CONFIG_PMIC8058_OTHC
	pm8058_micbias_enable(OTHC_MICBIAS_2, OTHC_SIGNAL_OFF);
#endif
}

static void msm_snddev_dmic_sec_power(int en)
{
	pr_aud_err("%s", __func__);
	if (en)
		msm_snddev_enable_dmic_sec_power();
	else
		msm_snddev_disable_dmic_sec_power();
}

static struct adie_codec_action_unit iearpiece_48KHz_osr256_actions[] =
	EAR_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry iearpiece_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = iearpiece_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(iearpiece_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile iearpiece_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = iearpiece_settings,
	.setting_sz = ARRAY_SIZE(iearpiece_settings),
};

static struct snddev_icodec_data snddev_iearpiece_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "handset_rx",
	.copp_id = 0,
	.profile = &iearpiece_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = handset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_RECEIVER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_RECEIVER,
	.default_aic3254_id = PLAYBACK_RECEIVER,
};

static struct platform_device msm_iearpiece_device = {
	.name = "snddev_icodec",
	.id = 0,
	.dev = { .platform_data = &snddev_iearpiece_data },
};

static struct adie_codec_action_unit iearpiece_hac_48KHz_osr256_actions[] =
	EAR_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry iearpiece_hac_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = iearpiece_hac_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(iearpiece_hac_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile iearpiece_hac_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = iearpiece_hac_settings,
	.setting_sz = ARRAY_SIZE(iearpiece_hac_settings),
};

static struct snddev_icodec_data snddev_ihac_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "hac_rx",
	.copp_id = 0,
	.profile = &iearpiece_hac_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = handset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_RECEIVER,
	.aic3254_voc_id = HAC,
	.default_aic3254_id = PLAYBACK_RECEIVER,
};

static struct platform_device msm_ihac_device = {
	.name = "snddev_icodec",
	.id = 38,
	.dev = { .platform_data = &snddev_ihac_data },
};

static struct adie_codec_action_unit imic_48KHz_osr256_actions[] =
	AMIC_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry imic_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = imic_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(imic_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile imic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = imic_settings,
	.setting_sz = ARRAY_SIZE(imic_settings),
};

static struct snddev_icodec_data snddev_imic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "handset_tx",
	.copp_id = 1,
	.profile = &imic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = int_mic_enable,
	.aic3254_id = VOICERECOGNITION_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_RECEIVER,
	.default_aic3254_id = VOICERECOGNITION_IMIC,
};

static struct platform_device msm_imic_device = {
	.name = "snddev_icodec",
	.id = 1,
	.dev = { .platform_data = &snddev_imic_data },
};

static struct snddev_icodec_data snddev_nomic_headset_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "nomic_headset_tx",
	.copp_id = 1,
	.profile = &imic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = int_mic_enable,
	.aic3254_id = VOICERECORD_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_HEADSET,
	.default_aic3254_id = VOICERECORD_IMIC,
};

static struct platform_device msm_nomic_headset_tx_device = {
	.name = "snddev_icodec",
	.id = 40,
	.dev = { .platform_data = &snddev_nomic_headset_data },
};

static struct adie_codec_action_unit headset_ab_cpls_48KHz_osr256_actions[] =
	HEADSET_AB_CPLS_48000_OSR_256;

static struct adie_codec_hwsetting_entry headset_ab_cpls_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = headset_ab_cpls_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(headset_ab_cpls_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile headset_ab_cpls_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = headset_ab_cpls_settings,
	.setting_sz = ARRAY_SIZE(headset_ab_cpls_settings),
};

static struct snddev_icodec_data snddev_ihs_stereo_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_stereo_rx",
	.copp_id = 0,
	.profile = &headset_ab_cpls_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_HEADSET,
	.aic3254_voc_id = CALL_DOWNLINK_EMIC_HEADSET,
	.default_aic3254_id = PLAYBACK_HEADSET,
};

static struct platform_device msm_headset_stereo_device = {
	.name = "snddev_icodec",
	.id = 34,
	.dev = { .platform_data = &snddev_ihs_stereo_rx_data },
};

static struct snddev_icodec_data snddev_nomic_ihs_stereo_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "nomic_headset_stereo_rx",
	.copp_id = 0,
	.profile = &headset_ab_cpls_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_HEADSET,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_HEADSET,
	.default_aic3254_id = PLAYBACK_HEADSET,
};

static struct platform_device msm_nomic_headset_stereo_device = {
	.name = "snddev_icodec",
	.id = 39,
	.dev = { .platform_data = &snddev_nomic_ihs_stereo_rx_data },
};

static struct adie_codec_action_unit headset_anc_48KHz_osr256_actions[] =
	ANC_HEADSET_CPLS_AMIC1_AUXL_RX1_48000_OSR_256;

static struct adie_codec_hwsetting_entry headset_anc_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = headset_anc_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(headset_anc_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile headset_anc_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = headset_anc_settings,
	.setting_sz = ARRAY_SIZE(headset_anc_settings),
};

static struct snddev_icodec_data snddev_anc_headset_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE | SNDDEV_CAP_ANC),
	.name = "anc_headset_stereo_rx",
	.copp_id = PRIMARY_I2S_RX,
	.profile = &headset_anc_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = int_mic_enable,
};

static struct platform_device msm_anc_headset_device = {
	.name = "snddev_icodec",
	.id = 51,
	.dev = { .platform_data = &snddev_anc_headset_data },
};

static struct adie_codec_action_unit ispkr_stereo_48KHz_osr256_actions[] =
	SPEAKER_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry ispkr_stereo_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ispkr_stereo_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(ispkr_stereo_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ispkr_stereo_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ispkr_stereo_settings,
	.setting_sz = ARRAY_SIZE(ispkr_stereo_settings),
};

static struct snddev_icodec_data snddev_ispkr_stereo_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "speaker_stereo_rx",
	.copp_id = 0,
	.profile = &ispkr_stereo_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_SPEAKER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_SPEAKER,
	.default_aic3254_id = PLAYBACK_SPEAKER,
};

static struct platform_device msm_ispkr_stereo_device = {
	.name = "snddev_icodec",
	.id = 2,
	.dev = { .platform_data = &snddev_ispkr_stereo_data },
};

static struct adie_codec_action_unit idmic_mono_48KHz_osr256_actions[] =
	AMIC_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry idmic_mono_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = idmic_mono_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(idmic_mono_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile idmic_mono_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = idmic_mono_settings,
	.setting_sz = ARRAY_SIZE(idmic_mono_settings),
};

static struct snddev_icodec_data snddev_ispkr_mic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "speaker_mono_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &idmic_mono_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = int_mic_enable,
	.aic3254_id = VOICERECORD_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_SPEAKER,
	.default_aic3254_id = VOICERECORD_IMIC,
};

static struct platform_device msm_ispkr_mic_device = {
	.name = "snddev_icodec",
	.id = 3,
	.dev = { .platform_data = &snddev_ispkr_mic_data },
};

static struct adie_codec_action_unit handset_dual_mic_endfire_8KHz_osr256_actions[] =
	DMIC1_PRI_STEREO_8000_OSR_256;

static struct adie_codec_action_unit spk_dual_mic_endfire_8KHz_osr256_actions[] =
	DMIC1_PRI_STEREO_8000_OSR_256;

static struct adie_codec_hwsetting_entry handset_dual_mic_endfire_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = handset_dual_mic_endfire_8KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(handset_dual_mic_endfire_8KHz_osr256_actions),
	}
};

static struct adie_codec_hwsetting_entry spk_dual_mic_endfire_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = spk_dual_mic_endfire_8KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(spk_dual_mic_endfire_8KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile handset_dual_mic_endfire_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = handset_dual_mic_endfire_settings,
	.setting_sz = ARRAY_SIZE(handset_dual_mic_endfire_settings),
};

static struct adie_codec_dev_profile spk_dual_mic_endfire_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = spk_dual_mic_endfire_settings,
	.setting_sz = ARRAY_SIZE(spk_dual_mic_endfire_settings),
};

static struct snddev_icodec_data snddev_dual_mic_endfire_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "handset_dual_mic_endfire_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &handset_dual_mic_endfire_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = msm_snddev_dmic_power,
	.aic3254_id = VOICERECORD_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_RECEIVER,
	.default_aic3254_id = VOICERECORD_IMIC,
};

static struct platform_device msm_hs_dual_mic_endfire_device = {
	.name = "snddev_icodec",
	.id = 14,
	.dev = { .platform_data = &snddev_dual_mic_endfire_data },
};

static struct snddev_icodec_data snddev_dual_mic_spkr_endfire_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "speaker_dual_mic_endfire_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &spk_dual_mic_endfire_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = msm_snddev_dmic_power,
	.aic3254_id = VOICERECORD_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_SPEAKER,
	.default_aic3254_id = VOICERECORD_IMIC,
};

static struct platform_device msm_spkr_dual_mic_endfire_device = {
	.name = "snddev_icodec",
	.id = 15,
	.dev = { .platform_data = &snddev_dual_mic_spkr_endfire_data },
};

static struct adie_codec_action_unit dual_mic_broadside_8osr256_actions[] =
	HS_DMIC2_STEREO_8000_OSR_256;

static struct adie_codec_hwsetting_entry dual_mic_broadside_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = dual_mic_broadside_8osr256_actions,
		.action_sz = ARRAY_SIZE(dual_mic_broadside_8osr256_actions),
	}
};

static struct adie_codec_dev_profile dual_mic_broadside_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = dual_mic_broadside_settings,
	.setting_sz = ARRAY_SIZE(dual_mic_broadside_settings),
};

static struct snddev_icodec_data snddev_hs_dual_mic_broadside_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "handset_dual_mic_broadside_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &dual_mic_broadside_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = msm_snddev_dmic_sec_power,
};

static struct platform_device msm_hs_dual_mic_broadside_device = {
	.name = "snddev_icodec",
	.id = 21,
	.dev = { .platform_data = &snddev_hs_dual_mic_broadside_data },
};

static struct snddev_icodec_data snddev_spkr_dual_mic_broadside_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "speaker_dual_mic_broadside_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &dual_mic_broadside_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = msm_snddev_dmic_sec_power,
};

static struct platform_device msm_spkr_dual_mic_broadside_device = {
	.name = "snddev_icodec",
	.id = 18,
	.dev = { .platform_data = &snddev_spkr_dual_mic_broadside_data },
};

static struct snddev_hdmi_data snddev_hdmi_stereo_rx_data = {
	.capability = SNDDEV_CAP_RX ,
	.name = "hdmi_stereo_rx",
	.copp_id = HDMI_RX,
	.channel_mode = 0,
	.default_sample_rate = 48000,
};

static struct platform_device msm_snddev_hdmi_stereo_rx_device = {
	.name = "snddev_hdmi",
	.id = 0,
	.dev = { .platform_data = &snddev_hdmi_stereo_rx_data },
};

static struct snddev_mi2s_data snddev_mi2s_fm_tx_data = {
	.capability = SNDDEV_CAP_TX ,
	.name = "fmradio_stereo_tx",
	.copp_id = MI2S_TX,
	.channel_mode = 2, /* stereo */
	.sd_lines = MI2S_SD3, /* sd3 */
	.sample_rate = 48000,
};

static struct platform_device msm_mi2s_fm_tx_device = {
	.name = "snddev_mi2s",
	.id = 0,
	.dev = { .platform_data = &snddev_mi2s_fm_tx_data },
};

static struct adie_codec_action_unit ifmradio_speaker_osr256_actions[] =
	AUXPGA_SPEAKER_RX;

static struct adie_codec_hwsetting_entry ifmradio_speaker_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ifmradio_speaker_osr256_actions,
		.action_sz = ARRAY_SIZE(ifmradio_speaker_osr256_actions),
	}
};

static struct adie_codec_dev_profile ifmradio_speaker_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ifmradio_speaker_settings,
	.setting_sz = ARRAY_SIZE(ifmradio_speaker_settings),
};

static struct snddev_mi2s_data snddev_mi2s_fm_rx_data = {
	.capability = SNDDEV_CAP_RX ,
	.name = "fmradio_stereo_rx",
	.copp_id = MI2S_RX,
	.channel_mode = 2, /* stereo */
	.sd_lines = MI2S_SD3, /* sd3 */
	.sample_rate = 48000,
};

static struct platform_device msm_mi2s_fm_rx_device = {
	.name = "snddev_mi2s",
	.id = 1,
	.dev = { .platform_data = &snddev_mi2s_fm_rx_data },
};

static struct snddev_icodec_data snddev_ifmradio_speaker_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_FM),
	.name = "fmradio_speaker_rx",
	.copp_id = 0,
	.profile = &ifmradio_speaker_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = fm_speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = FM_OUT_SPEAKER,
	.aic3254_voc_id = FM_OUT_SPEAKER,
	.default_aic3254_id = FM_OUT_SPEAKER,
};

static struct platform_device msm_ifmradio_speaker_device = {
	.name = "snddev_icodec",
	.id = 9,
	.dev = { .platform_data = &snddev_ifmradio_speaker_data },
};

static struct adie_codec_action_unit ifmradio_headset_osr256_actions[] =
	AUXPGA_HEADSET_AB_CPLS_RX_48000;

static struct adie_codec_hwsetting_entry ifmradio_headset_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ifmradio_headset_osr256_actions,
		.action_sz = ARRAY_SIZE(ifmradio_headset_osr256_actions),
	}
};

static struct adie_codec_dev_profile ifmradio_headset_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ifmradio_headset_settings,
	.setting_sz = ARRAY_SIZE(ifmradio_headset_settings),
};

static struct snddev_icodec_data snddev_ifmradio_headset_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_FM),
	.name = "fmradio_headset_rx",
	.copp_id = 0,
	.profile = &ifmradio_headset_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = fm_headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = FM_OUT_HEADSET,
	.aic3254_voc_id = FM_OUT_HEADSET,
	.default_aic3254_id = FM_OUT_HEADSET,
};

static struct platform_device msm_ifmradio_headset_device = {
	.name = "snddev_icodec",
	.id = 10,
	.dev = { .platform_data = &snddev_ifmradio_headset_data },
};

static struct adie_codec_action_unit iheadset_mic_tx_osr256_actions[] =
	HS_AMIC2_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry iheadset_mic_tx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = iheadset_mic_tx_osr256_actions,
		.action_sz = ARRAY_SIZE(iheadset_mic_tx_osr256_actions),
	}
};

static struct adie_codec_dev_profile iheadset_mic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = iheadset_mic_tx_settings,
	.setting_sz = ARRAY_SIZE(iheadset_mic_tx_settings),
};

static struct snddev_icodec_data snddev_headset_mic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "headset_mono_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &iheadset_mic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = ext_mic_enable,
	.aic3254_id = VOICERECOGNITION_EMIC,
	.aic3254_voc_id = CALL_UPLINK_EMIC_HEADSET,
	.default_aic3254_id = VOICERECORD_EMIC,
};

static struct platform_device msm_headset_mic_device = {
	.name = "snddev_icodec",
	.id = 33,
	.dev = { .platform_data = &snddev_headset_mic_data },
};

static struct adie_codec_action_unit
	ihs_stereo_speaker_stereo_rx_48KHz_osr256_actions[] =
	SPEAKER_HPH_AB_CPL_PRI_STEREO_48000_OSR_256;

static struct adie_codec_hwsetting_entry
	ihs_stereo_speaker_stereo_rx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ihs_stereo_speaker_stereo_rx_48KHz_osr256_actions,
		.action_sz =
		ARRAY_SIZE(ihs_stereo_speaker_stereo_rx_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ihs_stereo_speaker_stereo_rx_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ihs_stereo_speaker_stereo_rx_settings,
	.setting_sz = ARRAY_SIZE(ihs_stereo_speaker_stereo_rx_settings),
};

static struct snddev_icodec_data snddev_ihs_stereo_speaker_stereo_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_stereo_speaker_stereo_rx",
	.copp_id = 0,
	.profile = &ihs_stereo_speaker_stereo_rx_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = headset_speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = RING_HEADSET_SPEAKER,
	.aic3254_voc_id = RING_HEADSET_SPEAKER,
	.default_aic3254_id = RING_HEADSET_SPEAKER,
};

static struct platform_device msm_ihs_stereo_speaker_stereo_rx_device = {
	.name = "snddev_icodec",
	.id = 22,
	.dev = { .platform_data = &snddev_ihs_stereo_speaker_stereo_rx_data },
};

static struct snddev_ecodec_data snddev_bt_sco_earpiece_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "bt_sco_rx",
	.copp_id = PCM_RX,
	.channel_mode = 1,
};

static struct snddev_ecodec_data snddev_bt_sco_mic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "bt_sco_tx",
	.copp_id = PCM_TX,
	.channel_mode = 1,
};

struct platform_device msm_bt_sco_earpiece_device = {
	.name = "msm_snddev_ecodec",
	.id = 0,
	.dev = { .platform_data = &snddev_bt_sco_earpiece_data },
};

struct platform_device msm_bt_sco_mic_device = {
	.name = "msm_snddev_ecodec",
	.id = 1,
	.dev = { .platform_data = &snddev_bt_sco_mic_data },
};

static struct adie_codec_action_unit itty_mono_tx_actions[] =
	TTY_HEADSET_MONO_TX_48000_OSR_256;

static struct adie_codec_hwsetting_entry itty_mono_tx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = itty_mono_tx_actions,
		.action_sz = ARRAY_SIZE(itty_mono_tx_actions),
	},
};

static struct adie_codec_dev_profile itty_mono_tx_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = itty_mono_tx_settings,
	.setting_sz = ARRAY_SIZE(itty_mono_tx_settings),
};

static struct snddev_icodec_data snddev_itty_mono_tx_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE | SNDDEV_CAP_TTY),
	.name = "tty_headset_mono_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &itty_mono_tx_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = ext_mic_enable,
	.aic3254_id = TTY_IN_FULL,
	.aic3254_voc_id = TTY_IN_FULL,
	.default_aic3254_id = TTY_IN_FULL,
};

static struct platform_device msm_itty_mono_tx_device = {
	.name = "snddev_icodec",
	.id = 16,
	.dev = { .platform_data = &snddev_itty_mono_tx_data },
};

static struct adie_codec_action_unit itty_mono_rx_actions[] =
	TTY_HEADSET_MONO_RX_48000_OSR_256;

static struct adie_codec_hwsetting_entry itty_mono_rx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = itty_mono_rx_actions,
		.action_sz = ARRAY_SIZE(itty_mono_rx_actions),
	},
};

static struct adie_codec_dev_profile itty_mono_rx_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = itty_mono_rx_settings,
	.setting_sz = ARRAY_SIZE(itty_mono_rx_settings),
};

static struct snddev_icodec_data snddev_itty_mono_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE | SNDDEV_CAP_TTY),
	.name = "tty_headset_mono_rx",
	.copp_id = PRIMARY_I2S_RX,
	.profile = &itty_mono_rx_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = TTY_OUT_FULL,
	.aic3254_voc_id = TTY_OUT_FULL,
	.default_aic3254_id = TTY_OUT_FULL,
};

static struct platform_device msm_itty_mono_rx_device = {
	.name = "snddev_icodec",
	.id = 17,
	.dev = { .platform_data = &snddev_itty_mono_rx_data },
};

static struct adie_codec_action_unit bmic_tx_48KHz_osr256_actions[] =
	AMIC_SEC_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry bmic_tx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = bmic_tx_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(bmic_tx_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile bmic_tx_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = bmic_tx_settings,
	.setting_sz = ARRAY_SIZE(bmic_tx_settings),
};

static struct snddev_icodec_data snddev_bmic_tx_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "back_mic_tx",
	.copp_id = 1,
	.profile = &bmic_tx_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = back_mic_enable,
	.aic3254_id = VOICERECORD_EMIC,
	.aic3254_voc_id = VOICERECORD_EMIC,
	.default_aic3254_id = VOICERECORD_EMIC,
};

static struct platform_device msm_bmic_tx_device = {
	.name = "snddev_icodec",
	.id = 50, /* FIX ME */
	.dev = { .platform_data = &snddev_bmic_tx_data },
};

static struct adie_codec_action_unit headset_mono_ab_cpls_48KHz_osr256_actions[] =
	HEADSET_AB_CPLS_48000_OSR_256; /* FIX ME */

static struct adie_codec_hwsetting_entry headset_mono_ab_cpls_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = headset_mono_ab_cpls_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(headset_mono_ab_cpls_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile headset_mono_ab_cpls_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = headset_mono_ab_cpls_settings,
	.setting_sz = ARRAY_SIZE(headset_mono_ab_cpls_settings),
};

static struct snddev_icodec_data snddev_ihs_mono_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_mono_rx",
	.copp_id = 0,
	.profile = &headset_mono_ab_cpls_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_HEADSET,
	.aic3254_voc_id = CALL_DOWNLINK_EMIC_HEADSET,
	.default_aic3254_id = PLAYBACK_HEADSET,
};

static struct platform_device msm_headset_mono_ab_cpls_device = {
	.name = "snddev_icodec",
	.id = 35,  /* FIX ME */
	.dev = { .platform_data = &snddev_ihs_mono_rx_data },
};

static struct adie_codec_action_unit ihs_ispk_stereo_rx_48KHz_osr256_actions[] =
	SPEAKER_HPH_AB_CPL_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry ihs_ispk_stereo_rx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ihs_ispk_stereo_rx_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(ihs_ispk_stereo_rx_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ihs_ispk_stereo_rx_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ihs_ispk_stereo_rx_settings,
	.setting_sz = ARRAY_SIZE(ihs_ispk_stereo_rx_settings),
};

static struct snddev_icodec_data snddev_ihs_ispk_stereo_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_speaker_stereo_rx",
	.copp_id = 0,
	.profile = &ihs_ispk_stereo_rx_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = headset_speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = RING_HEADSET_SPEAKER,
	.aic3254_voc_id = RING_HEADSET_SPEAKER,
	.default_aic3254_id = RING_HEADSET_SPEAKER,
};

static struct platform_device msm_iheadset_ispeaker_rx_device = {
	.name = "snddev_icodec",
	.id = 36,  /* FIX ME */
	.dev = { .platform_data = &snddev_ihs_ispk_stereo_rx_data },
};

static struct adie_codec_action_unit idual_mic_48KHz_osr256_actions[] =
	DUAL_MIC_STEREO_TX_48000_OSR_256;

static struct adie_codec_hwsetting_entry idual_mic_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = idual_mic_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(idual_mic_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile idual_mic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = idual_mic_settings,
	.setting_sz = ARRAY_SIZE(idual_mic_settings),
};

static struct snddev_icodec_data snddev_idual_mic_endfire_real_stereo_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "dual_mic_stereo_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &idual_mic_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = stereo_mic_enable,
	.aic3254_id = VIDEORECORD_IMIC,
	.aic3254_voc_id = VOICERECORD_EMIC, /* FIX ME */
	.default_aic3254_id = VIDEORECORD_IMIC,
};

static struct platform_device msm_real_stereo_tx_device = {
	.name = "snddev_icodec",
	.id = 26,  /* FIX ME */
	.dev = { .platform_data = &snddev_idual_mic_endfire_real_stereo_data },
};



static struct adie_codec_action_unit iusb_headset_stereo_rx_48KHz_osr256_actions[] =
	SPEAKER_HPH_AB_CPL_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry iusb_headset_stereo_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = iusb_headset_stereo_rx_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(iusb_headset_stereo_rx_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile iusb_headset_stereo_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = iusb_headset_stereo_settings,
	.setting_sz = ARRAY_SIZE(iusb_headset_stereo_settings),
};

static struct snddev_icodec_data snddev_iusb_headset_stereo_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "usb_headset_stereo_rx",
	.copp_id = 0,
	.profile = &iusb_headset_stereo_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = usb_headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = USB_AUDIO,
	.aic3254_voc_id = USB_AUDIO,
	.default_aic3254_id = USB_AUDIO,
};

static struct platform_device msm_iusb_headset_rx_device = {
	.name = "snddev_icodec",
	.id = 27,  /* FIX ME */
	.dev = { .platform_data = &snddev_iusb_headset_stereo_rx_data },
};

static struct adie_codec_action_unit ispkr_mono_48KHz_osr256_actions[] =
	SPEAKER_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry ispkr_mono_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ispkr_mono_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(ispkr_mono_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ispkr_mono_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ispkr_mono_settings,
	.setting_sz = ARRAY_SIZE(ispkr_mono_settings),
};

static struct snddev_icodec_data snddev_ispkr_mono_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "speaker_mono_rx",
	.copp_id = 0,
	.profile = &ispkr_mono_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_SPEAKER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_SPEAKER,
	.default_aic3254_id = PLAYBACK_SPEAKER,
};

static struct platform_device msm_ispkr_mono_device = {
	.name = "snddev_icodec",
	.id = 28,
	.dev = { .platform_data = &snddev_ispkr_mono_data },
};

static struct adie_codec_action_unit camcorder_imic_48KHz_osr256_actions[] =
	AMIC_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry camcorder_imic_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = camcorder_imic_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(camcorder_imic_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile camcorder_imic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = camcorder_imic_settings,
	.setting_sz = ARRAY_SIZE(camcorder_imic_settings),
};

static struct snddev_icodec_data snddev_camcorder_imic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "camcorder_mono_tx",
	.copp_id = 1,
	.profile = &camcorder_imic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = int_mic_enable,
	.aic3254_id = VOICERECOGNITION_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_RECEIVER,
	.default_aic3254_id = VOICERECOGNITION_IMIC,
};

static struct platform_device msm_camcorder_imic_device = {
	.name = "snddev_icodec",
	.id = 53,
	.dev = { .platform_data = &snddev_camcorder_imic_data },
};

static struct adie_codec_action_unit camcorder_idual_mic_48KHz_osr256_actions[] =
	DUAL_MIC_STEREO_TX_48000_OSR_256;

static struct adie_codec_hwsetting_entry camcorder_idual_mic_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = camcorder_idual_mic_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(camcorder_idual_mic_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile camcorder_idual_mic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = camcorder_idual_mic_settings,
	.setting_sz = ARRAY_SIZE(camcorder_idual_mic_settings),
};

static struct snddev_icodec_data snddev_camcorder_imic_stereo_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "camcorder_stereo_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &camcorder_idual_mic_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = stereo_mic_enable,
	.aic3254_id = VIDEORECORD_IMIC,
	.aic3254_voc_id = VOICERECORD_EMIC, /* FIX ME */
	.default_aic3254_id = VIDEORECORD_IMIC,
};

static struct platform_device msm_camcorder_imic_stereo_device = {
	.name = "snddev_icodec",
	.id = 54,  /* FIX ME */
	.dev = { .platform_data = &snddev_camcorder_imic_stereo_data },
};

static struct adie_codec_action_unit camcorder_idual_mic_rev_48KHz_osr256_actions[] =
	DUAL_MIC_STEREO_TX_48000_OSR_256;

static struct adie_codec_hwsetting_entry camcorder_idual_mic_rev_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = camcorder_idual_mic_rev_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(camcorder_idual_mic_rev_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile camcorder_idual_mic_rev_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = camcorder_idual_mic_rev_settings,
	.setting_sz = ARRAY_SIZE(camcorder_idual_mic_rev_settings),
};

static struct snddev_icodec_data snddev_camcorder_imic_stereo_rev_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "camcorder_stereo_rev_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &camcorder_idual_mic_rev_profile,
	.channel_mode = 2,
	.default_sample_rate = 48000,
	.pamp_on = stereo_mic_enable,
	.aic3254_id = VIDEORECORD_IMIC,
	.aic3254_voc_id = VOICERECORD_EMIC, /* FIX ME */
	.default_aic3254_id = VIDEORECORD_IMIC,
};

static struct platform_device msm_camcorder_imic_stereo_rev_device = {
	.name = "snddev_icodec",
	.id = 55,
	.dev = { .platform_data = &snddev_camcorder_imic_stereo_rev_data },
};

static struct adie_codec_action_unit camcorder_iheadset_mic_tx_osr256_actions[] =
	HS_AMIC2_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry camcorder_iheadset_mic_tx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = camcorder_iheadset_mic_tx_osr256_actions,
		.action_sz = ARRAY_SIZE(camcorder_iheadset_mic_tx_osr256_actions),
	}
};

static struct adie_codec_dev_profile camcorder_iheadset_mic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = camcorder_iheadset_mic_tx_settings,
	.setting_sz = ARRAY_SIZE(camcorder_iheadset_mic_tx_settings),
};

static struct snddev_icodec_data snddev_camcorder_headset_mic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "camcorder_headset_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &camcorder_iheadset_mic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = ext_mic_enable,
	.aic3254_id = VOICERECOGNITION_EMIC,
	.aic3254_voc_id = CALL_UPLINK_EMIC_HEADSET,
	.default_aic3254_id = VOICERECORD_EMIC,
};

static struct platform_device msm_camcorder_headset_mic_device = {
	.name = "snddev_icodec",
	.id = 56,
	.dev = { .platform_data = &snddev_camcorder_headset_mic_data },
};

static struct adie_codec_action_unit vr_iearpiece_48KHz_osr256_actions[] =
	EAR_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry vr_iearpiece_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = vr_iearpiece_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(vr_iearpiece_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile vr_iearpiece_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = vr_iearpiece_settings,
	.setting_sz = ARRAY_SIZE(vr_iearpiece_settings),
};

static struct snddev_icodec_data snddev_vr_iearpiece_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "vr_handset_mono_tx",
	.copp_id = 0,
	.profile = &vr_iearpiece_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = handset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_RECEIVER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_RECEIVER,
	.default_aic3254_id = PLAYBACK_RECEIVER,
};

static struct platform_device msm_vr_iearpiece_device = {
	.name = "snddev_icodec",
	.id = 57,
	.dev = { .platform_data = &snddev_vr_iearpiece_data },
};

static struct adie_codec_action_unit vr_iheadset_mic_tx_osr256_actions[] =
	HS_AMIC2_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry vr_iheadset_mic_tx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = vr_iheadset_mic_tx_osr256_actions,
		.action_sz = ARRAY_SIZE(vr_iheadset_mic_tx_osr256_actions),
	}
};

static struct adie_codec_dev_profile vr_iheadset_mic_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = vr_iheadset_mic_tx_settings,
	.setting_sz = ARRAY_SIZE(vr_iheadset_mic_tx_settings),
};

static struct snddev_icodec_data snddev_vr_headset_mic_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "vr_headset_mono_tx",
	.copp_id = PRIMARY_I2S_TX,
	.profile = &vr_iheadset_mic_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = ext_mic_enable,
	.aic3254_id = VOICERECOGNITION_EMIC,
	.aic3254_voc_id = CALL_UPLINK_EMIC_HEADSET,
	.default_aic3254_id = VOICERECORD_EMIC,
};

static struct platform_device msm_vr_headset_mic_device = {
	.name = "snddev_icodec",
	.id = 58,
	.dev = { .platform_data = &snddev_vr_headset_mic_data },
};

static struct adie_codec_action_unit ispkr_mono_alt_48KHz_osr256_actions[] =
	SPEAKER_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry ispkr_mono_alt_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ispkr_mono_alt_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(ispkr_mono_alt_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ispkr_mono_alt_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ispkr_mono_alt_settings,
	.setting_sz = ARRAY_SIZE(ispkr_mono_alt_settings),
};

static struct snddev_icodec_data snddev_ispkr_mono_alt_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "speaker_mono_alt_rx",
	.copp_id = 0,
	.profile = &ispkr_mono_alt_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_SPEAKER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_SPEAKER,
	.default_aic3254_id = PLAYBACK_SPEAKER,
};

static struct platform_device msm_ispkr_mono_alt_device = {
	.name = "snddev_icodec",
	.id = 59,
	.dev = { .platform_data = &snddev_ispkr_mono_alt_data },
};


static struct adie_codec_action_unit imic_note_48KHz_osr256_actions[] =
	AMIC_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry imic_note_settings[] = {
	{
		.freq_plan = 16000,
		.osr = 256,
		.actions = imic_note_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(imic_note_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile imic_note_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = imic_note_settings,
	.setting_sz = ARRAY_SIZE(imic_note_settings),
};

static struct snddev_icodec_data snddev_imic_note_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "imic_note_tx",
	.copp_id = 1,
	.profile = &imic_note_profile,
	.channel_mode = 2,
	.default_sample_rate = 16000,
	.pamp_on = stereo_mic_enable,
	.aic3254_id = VOICERECORD_IMIC,
	.aic3254_voc_id = CALL_UPLINK_IMIC_RECEIVER,
	.default_aic3254_id = VOICERECORD_IMIC,
};

static struct platform_device msm_imic_note_device = {
	.name = "snddev_icodec",
	.id = 60,
	.dev = { .platform_data = &snddev_imic_note_data },
};

static struct adie_codec_action_unit ispkr_note_48KHz_osr256_actions[] =
	SPEAKER_PRI_48000_OSR_256;

static struct adie_codec_hwsetting_entry ispkr_note_settings[] = {
	{
		.freq_plan = 16000,
		.osr = 256,
		.actions = ispkr_note_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(ispkr_note_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ispkr_note_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ispkr_note_settings,
	.setting_sz = ARRAY_SIZE(ispkr_note_settings),
};

static struct snddev_icodec_data snddev_ispkr_note_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "ispkr_note_rx",
	.copp_id = 0,
	.profile = &ispkr_note_profile,
	.channel_mode = 2,
	.default_sample_rate = 16000,
	.pamp_on = speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_SPEAKER,
	.aic3254_voc_id = CALL_DOWNLINK_IMIC_SPEAKER,
	.default_aic3254_id = PLAYBACK_SPEAKER,
};

static struct platform_device msm_ispkr_note_device = {
	.name = "snddev_icodec",
	.id = 61,
	.dev = { .platform_data = &snddev_ispkr_note_data },
};

static struct adie_codec_action_unit emic_note_16KHz_osr256_actions[] =
	AMIC_PRI_MONO_48000_OSR_256;

static struct adie_codec_hwsetting_entry emic_note_settings[] = {
	{
		.freq_plan = 16000,
		.osr = 256,
		.actions = emic_note_16KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(emic_note_16KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile emic_note_profile = {
	.path_type = ADIE_CODEC_TX,
	.settings = emic_note_settings,
	.setting_sz = ARRAY_SIZE(emic_note_settings),
};

static struct snddev_icodec_data snddev_emic_note_data = {
	.capability = (SNDDEV_CAP_TX | SNDDEV_CAP_VOICE),
	.name = "emic_note_tx",
	.copp_id = 1,
	.profile = &emic_note_profile,
	.channel_mode = 1,
	.default_sample_rate = 16000,
	.pamp_on = ext_mic_enable,
	.aic3254_id = VOICERECORD_EMIC,
	.aic3254_voc_id = VOICERECORD_EMIC,
	.default_aic3254_id = VOICERECORD_EMIC,
};

static struct platform_device msm_emic_note_device = {
	.name = "snddev_icodec",
	.id = 62,
	.dev = { .platform_data = &snddev_emic_note_data },
};

static struct adie_codec_action_unit headset_note_48KHz_osr256_actions[] =
	HEADSET_AB_CPLS_48000_OSR_256;

static struct adie_codec_hwsetting_entry headset_note_settings[] = {
	{
		.freq_plan = 16000,
		.osr = 256,
		.actions = headset_note_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE(headset_note_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile headset_note_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = headset_note_settings,
	.setting_sz = ARRAY_SIZE(headset_note_settings),
};

static struct snddev_icodec_data snddev_ihs_note_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_note_rx",
	.copp_id = 0,
	.profile = &headset_note_profile,
	.channel_mode = 2,
	.default_sample_rate = 16000,
	.pamp_on = headset_enable,
	.voltage_on = voltage_on,
	.aic3254_id = PLAYBACK_HEADSET,
	.aic3254_voc_id = CALL_DOWNLINK_EMIC_HEADSET,
	.default_aic3254_id = PLAYBACK_HEADSET,
};

static struct platform_device msm_headset_note_device = {
	.name = "snddev_icodec",
	.id = 63,
	.dev = { .platform_data = &snddev_ihs_note_data },
};

static struct adie_codec_action_unit
	ihs_mono_speaker_mono_rx_48KHz_osr256_actions[] =
	SPEAKER_HPH_AB_CPL_PRI_STEREO_48000_OSR_256;

static struct adie_codec_hwsetting_entry
	ihs_mono_speaker_mono_rx_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions = ihs_mono_speaker_mono_rx_48KHz_osr256_actions,
		.action_sz =
		ARRAY_SIZE(ihs_mono_speaker_mono_rx_48KHz_osr256_actions),
	}
};

static struct adie_codec_dev_profile ihs_mono_speaker_mono_rx_profile = {
	.path_type = ADIE_CODEC_RX,
	.settings = ihs_mono_speaker_mono_rx_settings,
	.setting_sz = ARRAY_SIZE(ihs_mono_speaker_mono_rx_settings),
};

static struct snddev_icodec_data snddev_ihs_mono_speaker_mono_rx_data = {
	.capability = (SNDDEV_CAP_RX | SNDDEV_CAP_VOICE),
	.name = "headset_mono_speaker_mono_rx",
	.copp_id = 0,
	.profile = &ihs_mono_speaker_mono_rx_profile,
	.channel_mode = 1,
	.default_sample_rate = 48000,
	.pamp_on = headset_speaker_enable,
	.voltage_on = voltage_on,
	.aic3254_id = RING_HEADSET_SPEAKER,
	.aic3254_voc_id = RING_HEADSET_SPEAKER,
	.default_aic3254_id = RING_HEADSET_SPEAKER,
};

static struct platform_device msm_ihs_mono_speaker_mono_rx_device = {
	.name = "snddev_icodec",
	.id = 64,
	.dev = { .platform_data = &snddev_ihs_mono_speaker_mono_rx_data },
};

#ifdef CONFIG_DEBUG_FS
static struct adie_codec_action_unit
	ihs_stereo_rx_class_d_legacy_48KHz_osr256_actions[] =
	HPH_PRI_D_LEG_STEREO;

static struct adie_codec_hwsetting_entry
	ihs_stereo_rx_class_d_legacy_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions =
		ihs_stereo_rx_class_d_legacy_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE
		(ihs_stereo_rx_class_d_legacy_48KHz_osr256_actions),
	}
};

static struct adie_codec_action_unit
	ihs_stereo_rx_class_ab_legacy_48KHz_osr256_actions[] =
	HPH_PRI_AB_LEG_STEREO;

static struct adie_codec_hwsetting_entry
	ihs_stereo_rx_class_ab_legacy_settings[] = {
	{
		.freq_plan = 48000,
		.osr = 256,
		.actions =
		ihs_stereo_rx_class_ab_legacy_48KHz_osr256_actions,
		.action_sz = ARRAY_SIZE
		(ihs_stereo_rx_class_ab_legacy_48KHz_osr256_actions),
	}
};

static void snddev_hsed_config_modify_setting(int type)
{
	struct platform_device *device;
	struct snddev_icodec_data *icodec_data;

	device = &msm_headset_stereo_device;
	icodec_data = (struct snddev_icodec_data *)device->dev.platform_data;

	if (icodec_data) {
		if (type == 1) {
			icodec_data->voltage_on = NULL;
			icodec_data->profile->settings =
				ihs_stereo_rx_class_d_legacy_settings;
			icodec_data->profile->setting_sz =
			ARRAY_SIZE(ihs_stereo_rx_class_d_legacy_settings);
		} else if (type == 2) {
			icodec_data->voltage_on = NULL;
			icodec_data->profile->settings =
				ihs_stereo_rx_class_ab_legacy_settings;
			icodec_data->profile->setting_sz =
			ARRAY_SIZE(ihs_stereo_rx_class_ab_legacy_settings);
		}
	}
}

static void snddev_hsed_config_restore_setting(void)
{
	struct platform_device *device;
	struct snddev_icodec_data *icodec_data;

	device = &msm_headset_stereo_device;
	icodec_data = (struct snddev_icodec_data *)device->dev.platform_data;

	if (icodec_data) {
		icodec_data->voltage_on = voltage_on;
		icodec_data->profile->settings = headset_ab_cpls_settings;
		icodec_data->profile->setting_sz =
			ARRAY_SIZE(headset_ab_cpls_settings);
	}
}

static ssize_t snddev_hsed_config_debug_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char *lb_str = filp->private_data;
	char cmd;

	if (get_user(cmd, ubuf))
		return -EFAULT;

	if (!strcmp(lb_str, "msm_hsed_config")) {
		switch (cmd) {
		case '0':
			snddev_hsed_config_restore_setting();
			break;

		case '1':
			snddev_hsed_config_modify_setting(1);
			break;

		case '2':
			snddev_hsed_config_modify_setting(2);
			break;

		default:
			break;
		}
	}
	return cnt;
}

static int snddev_hsed_config_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations snddev_hsed_config_debug_fops = {
	.open = snddev_hsed_config_debug_open,
	.write = snddev_hsed_config_debug_write
};
#endif

static struct snddev_virtual_data snddev_uplink_rx_data = {
	.capability = SNDDEV_CAP_RX,
	.name = "uplink_rx",
	.copp_id = VOICE_PLAYBACK_TX,
};

static struct platform_device msm_uplink_rx_device = {
	.name = "snddev_virtual",
	.dev = { .platform_data = &snddev_uplink_rx_data },
};

static struct platform_device *snd_devices_common[] __initdata = {
	&msm_uplink_rx_device,
};

static struct platform_device *snd_devices_surf[] __initdata = {
	&msm_iearpiece_device,
	&msm_imic_device,
	&msm_ispkr_stereo_device,
	&msm_snddev_hdmi_stereo_rx_device,
	&msm_headset_mic_device,
	&msm_ispkr_mic_device,
	&msm_bt_sco_earpiece_device,
	&msm_bt_sco_mic_device,
	&msm_headset_stereo_device,
	&msm_itty_mono_tx_device,
	&msm_itty_mono_rx_device,
	&msm_mi2s_fm_tx_device,
	&msm_mi2s_fm_rx_device,
	&msm_ifmradio_speaker_device,
	&msm_ifmradio_headset_device,
	&msm_hs_dual_mic_endfire_device,
	&msm_spkr_dual_mic_endfire_device,
	&msm_hs_dual_mic_broadside_device,
	&msm_spkr_dual_mic_broadside_device,
	&msm_ihs_stereo_speaker_stereo_rx_device,
	&msm_headset_mono_ab_cpls_device,
	&msm_iheadset_ispeaker_rx_device,
	&msm_bmic_tx_device,
	&msm_anc_headset_device,
	&msm_real_stereo_tx_device,
	&msm_ihac_device,
	&msm_nomic_headset_tx_device,
	&msm_nomic_headset_stereo_device,
	&msm_iusb_headset_rx_device,
	&msm_ispkr_mono_device,
	&msm_camcorder_imic_device,
	&msm_camcorder_imic_stereo_device,
	&msm_camcorder_imic_stereo_rev_device,
	&msm_camcorder_headset_mic_device,
	&msm_vr_iearpiece_device,
	&msm_vr_headset_mic_device,
	&msm_ispkr_mono_alt_device,
	&msm_imic_note_device,
	&msm_ispkr_note_device,
	&msm_emic_note_device,
	&msm_headset_note_device,
	&msm_ihs_mono_speaker_mono_rx_device,
};

void htc_8x60_register_analog_ops(struct q6v2audio_analog_ops *ops)
{
	audio_ops = ops;
}

void __init msm_snddev_init(void)
{
	int i;
	int dev_id;

	platform_add_devices(snd_devices_surf, ARRAY_SIZE(snd_devices_surf));
#ifdef CONFIG_DEBUG_FS
	debugfs_hsed_config = debugfs_create_file("msm_hsed_config",
				S_IFREG | S_IRUGO, NULL,
		(void *) "msm_hsed_config", &snddev_hsed_config_debug_fops);
#endif

	for (i = 0, dev_id = 0; i < ARRAY_SIZE(snd_devices_common); i++)
		snd_devices_common[i]->id = dev_id++;

	platform_add_devices(snd_devices_common,
		ARRAY_SIZE(snd_devices_common));

	/* 8x60 project use externel amp
	rc = gpio_request(SNDDEV_GPIO_CLASS_D1_EN, "CLASSD1_EN");
	if (rc) {
		pr_aud_err("%s: spkr pamp gpio %d request"
			"failed\n", __func__, SNDDEV_GPIO_CLASS_D1_EN);
	} else {
		gpio_direction_output(SNDDEV_GPIO_CLASS_D1_EN, 0);
		gpio_free(SNDDEV_GPIO_CLASS_D1_EN);
	}
	*/
}
