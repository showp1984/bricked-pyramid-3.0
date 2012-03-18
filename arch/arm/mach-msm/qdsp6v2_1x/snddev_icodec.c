/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mfd/msm-adie-codec.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/wakelock.h>
#include <linux/pmic8058-othc.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <asm/uaccess.h>
#include <mach/qdsp6v2_1x/audio_dev_ctl.h>
#include <mach/qdsp6v2_1x/apr_audio.h>
#include <mach/vreg.h>
#include <mach/pmic.h>
#include <mach/debug_mm.h>
#include <mach/qdsp6v2_1x/snddev_icodec.h>
#include <linux/spi/spi_aic3254.h>
#include <mach/qdsp6v2_1x/q6afe.h>
#include "audio_acdb.h"

#define SNDDEV_ICODEC_PCM_SZ 32 /* 16 bit / sample stereo mode */
#define SNDDEV_ICODEC_MUL_FACTOR 3 /* Multi by 8 Shift by 3  */
#define SNDDEV_ICODEC_CLK_RATE(freq) \
	(((freq) * (SNDDEV_ICODEC_PCM_SZ)) << (SNDDEV_ICODEC_MUL_FACTOR))
#define SNDDEV_LOW_POWER_MODE 0
#define SNDDEV_HIGH_POWER_MODE 1
/* Voltage required for S4 in microVolts, 2.2V or 2200000microvolts */
#define SNDDEV_VREG_8058_S4_VOLTAGE (2200000)
/* Load Current required for S4 in microAmps,
   36mA - 56mA */
#define SNDDEV_VREG_LOW_POWER_LOAD (36000)
#define SNDDEV_VREG_HIGH_POWER_LOAD (56000)

static struct q6v2audio_icodec_ops default_audio_ops;
static struct q6v2audio_icodec_ops *audio_ops = &default_audio_ops;
static int support_aic3254;
static int support_adie;
static int support_aic3254_use_mclk;
static int aic3254_use_mclk_counter;
int msm_codec_i2s_slave_mode = 1;
static struct q6v2audio_aic3254_ops default_aic3254_ops;
static struct q6v2audio_aic3254_ops *aic3254_ops = &default_aic3254_ops;

/* Global state for the driver */
struct snddev_icodec_drv_state {
	struct mutex rx_lock;
	struct mutex tx_lock;
	u32 rx_active; /* ensure one rx device at a time */
	u32 tx_active; /* ensure one tx device at a time */
	struct clk *rx_osrclk;
	struct clk *rx_bitclk;
	struct clk *tx_osrclk;
	struct clk *tx_bitclk;

	struct wake_lock rx_idlelock;
	struct wake_lock tx_idlelock;

	/* handle to pmic8058 regulator smps4 */
	struct regulator *snddev_vreg;

	struct mutex rx_mclk_lock;
};

static struct snddev_icodec_drv_state snddev_icodec_drv;

struct regulator *vreg_init(void)
{
	int rc;
	struct regulator *vreg_ptr;

	vreg_ptr = regulator_get(NULL, "8058_s4");
	if (IS_ERR(vreg_ptr)) {
		pr_aud_err("%s: regulator_get 8058_s4 failed\n", __func__);
		return NULL;
	}

	rc = regulator_set_voltage(vreg_ptr, SNDDEV_VREG_8058_S4_VOLTAGE,
				SNDDEV_VREG_8058_S4_VOLTAGE);
	if (rc == 0)
		return vreg_ptr;
	else
		return NULL;
}

static void vreg_deinit(struct regulator *vreg)
{
	regulator_put(vreg);
}

static void vreg_mode_vote(struct regulator *vreg, int enable, int mode)
{
	int rc;
	if (enable) {
		rc = regulator_enable(vreg);
		if (rc != 0)
			pr_aud_err("%s:Enabling regulator failed\n", __func__);
		else {
			if (mode)
				regulator_set_optimum_mode(vreg,
						SNDDEV_VREG_HIGH_POWER_LOAD);
			else
				regulator_set_optimum_mode(vreg,
						SNDDEV_VREG_LOW_POWER_LOAD);
		}
	} else {
		rc = regulator_disable(vreg);
		if (rc != 0)
			pr_aud_err("%s:Disabling regulator failed\n", __func__);
	}
}

struct msm_cdcclk_ctl_state {
	unsigned int rx_mclk;
	unsigned int rx_mclk_requested;
	unsigned int tx_mclk;
	unsigned int tx_mclk_requested;
};

static struct msm_cdcclk_ctl_state the_msm_cdcclk_ctl_state;

static int msm_snddev_rx_mclk_request(void)
{
	int rc = 0;
/*
	rc = gpio_request(the_msm_cdcclk_ctl_state.rx_mclk,
		"MSM_SNDDEV_RX_MCLK");
	if (rc < 0) {
		pr_aud_err("%s: GPIO request for MSM SNDDEV RX failed\n", __func__);
		return rc;
	}
	the_msm_cdcclk_ctl_state.rx_mclk_requested = 1;
*/
	return rc;
}
static int msm_snddev_tx_mclk_request(void)
{
	int rc = 0;
/*
	rc = gpio_request(the_msm_cdcclk_ctl_state.tx_mclk,
		"MSM_SNDDEV_TX_MCLK");
	if (rc < 0) {
		pr_aud_err("%s: GPIO request for MSM SNDDEV TX failed\n", __func__);
		return rc;
	}
	the_msm_cdcclk_ctl_state.tx_mclk_requested = 1;
*/
	return rc;
}
static void msm_snddev_rx_mclk_free(void)
{
	if (the_msm_cdcclk_ctl_state.rx_mclk_requested) {
		gpio_free(the_msm_cdcclk_ctl_state.rx_mclk);
		the_msm_cdcclk_ctl_state.rx_mclk_requested = 0;
	}
}
static void msm_snddev_tx_mclk_free(void)
{
	if (the_msm_cdcclk_ctl_state.tx_mclk_requested) {
		gpio_free(the_msm_cdcclk_ctl_state.tx_mclk);
		the_msm_cdcclk_ctl_state.tx_mclk_requested = 0;
	}
}

static int get_msm_cdcclk_ctl_gpios(struct platform_device *pdev)
{
	int rc = 0;
	struct resource *res;

	/* Claim all of the GPIOs. */
	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
			"msm_snddev_rx_mclk");
	if (!res) {
		pr_aud_err("%s: failed to get gpio MSM SNDDEV RX\n", __func__);
		return -ENODEV;
	}
	the_msm_cdcclk_ctl_state.rx_mclk = res->start;
	the_msm_cdcclk_ctl_state.rx_mclk_requested = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
			"msm_snddev_tx_mclk");
	if (!res) {
		pr_aud_err("%s: failed to get gpio MSM SNDDEV TX\n", __func__);
		return -ENODEV;
	}
	the_msm_cdcclk_ctl_state.tx_mclk = res->start;
	the_msm_cdcclk_ctl_state.tx_mclk_requested = 0;

	return rc;
}
static int msm_cdcclk_ctl_probe(struct platform_device *pdev)
{
	int rc = 0;

	pr_aud_info("%s:\n", __func__);

	rc = get_msm_cdcclk_ctl_gpios(pdev);
	if (rc < 0) {
		pr_aud_err("%s: GPIO configuration failed\n", __func__);
		return -ENODEV;
	}
	return rc;
}
static struct platform_driver msm_cdcclk_ctl_driver = {
	.probe = msm_cdcclk_ctl_probe,
	.driver = { .name = "msm_cdcclk_ctl"}
};

static int snddev_icodec_rxclk_enable(struct snddev_icodec_state *icodec,
		int en)
{
	int trc;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;

	mutex_lock(&drv->rx_mclk_lock);
	if (en) {
		if (aic3254_use_mclk_counter == 0) {
			drv->rx_osrclk = clk_get(0, "i2s_spkr_osr_clk");
			if (IS_ERR(drv->rx_osrclk)) {
				pr_aud_err("%s turning on RX MCLK Error\n", \
					__func__);
				goto error_invalid_osrclk;
			}

			trc = clk_set_rate(drv->rx_osrclk, \
					SNDDEV_ICODEC_CLK_RATE(\
					icodec->sample_rate));
			if (IS_ERR_VALUE(trc)) {
				pr_aud_err("ERROR setting RX m clock1\n");
				goto error_invalid_freq;
			}
			clk_enable(drv->rx_osrclk);
		}

		aic3254_use_mclk_counter++;

	} else {
		if (aic3254_use_mclk_counter > 0) {
			aic3254_use_mclk_counter--;
			if (aic3254_use_mclk_counter == 0)
				clk_disable(drv->rx_osrclk);
		} else
			pr_aud_info("%s: counter error!\n", __func__);
	}

	mutex_unlock(&drv->rx_mclk_lock);

	pr_aud_info("%s: en: %d counter: %d\n", __func__, en, \
			aic3254_use_mclk_counter);

	return 0;

error_invalid_osrclk:
error_invalid_freq:
	pr_aud_err("%s: encounter error\n", __func__);
	msm_snddev_rx_mclk_free();

	mutex_unlock(&drv->rx_mclk_lock);
	return -ENODEV;
}

static int snddev_icodec_open_rx(struct snddev_icodec_state *icodec)
{
	int trc;
	int rc_clk;
	int afe_channel_mode;
	union afe_port_config afe_config;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;

	wake_lock(&drv->rx_idlelock);

	if (drv->snddev_vreg) {
		if (!strcmp(icodec->data->name, "headset_stereo_rx"))
			vreg_mode_vote(drv->snddev_vreg, 1,
					SNDDEV_LOW_POWER_MODE);
		else
			vreg_mode_vote(drv->snddev_vreg, 1,
					SNDDEV_HIGH_POWER_MODE);
	}

	if (support_aic3254_use_mclk) {
		rc_clk = snddev_icodec_rxclk_enable(icodec, 1);
		if (IS_ERR_VALUE(rc_clk)) {
			pr_aud_err("%s Enable RX master clock Error\n", \
					__func__);
			goto error_invalid_freq;
		}
	} else {
		msm_snddev_rx_mclk_request();

		drv->rx_osrclk = clk_get(0, "i2s_spkr_osr_clk");
		if (IS_ERR(drv->rx_osrclk))
			pr_aud_err("%s master clock Error\n", __func__);

		trc =  clk_set_rate(drv->rx_osrclk,
				SNDDEV_ICODEC_CLK_RATE(icodec->sample_rate));
		if (IS_ERR_VALUE(trc)) {
			pr_aud_err("ERROR setting m clock1\n");
			goto error_invalid_freq;
		}

		clk_enable(drv->rx_osrclk);
	}

	drv->rx_bitclk = clk_get(0, "i2s_spkr_bit_clk");
	if (IS_ERR(drv->rx_bitclk))
		pr_aud_err("%s clock Error\n", __func__);

	/* ***************************************
	 * 1. CPU MASTER MODE:
	 *     Master clock = Sample Rate * OSR rate bit clock
	 * OSR Rate bit clock = bit/sample * channel master
	 * clock / bit clock = divider value = 8
	 *
	 * 2. CPU SLAVE MODE:
	 *     bitclk = 0
	 * *************************************** */

	if (msm_codec_i2s_slave_mode) {
		pr_debug("%s: configuring bit clock for slave mode\n",
				__func__);
		trc =  clk_set_rate(drv->rx_bitclk, 0);
	} else
		trc =  clk_set_rate(drv->rx_bitclk, 8);

	if (IS_ERR_VALUE(trc)) {
		pr_aud_err("ERROR setting m clock1\n");
		goto error_adie;
	}
	clk_enable(drv->rx_bitclk);

	if (icodec->data->voltage_on)
		icodec->data->voltage_on(1);

	if (support_aic3254) {
		if (aic3254_ops->aic3254_set_mode) {
			if (msm_get_call_state() == 1)
				aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_RX,
					icodec->data->aic3254_voc_id);
			else
				aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_RX,
					icodec->data->aic3254_id);
		}
	}
	if (support_adie) {
		/* Configure ADIE */
		trc = adie_codec_open(icodec->data->profile, &icodec->adie_path);
		if (IS_ERR_VALUE(trc))
			pr_aud_err("%s: adie codec open failed\n", __func__);
		else
			adie_codec_setpath(icodec->adie_path,
						icodec->sample_rate, 256);
		/* OSR default to 256, can be changed for power optimization
		 * If OSR is to be changed, need clock API for setting the divider
		 */
	}

	switch (icodec->data->channel_mode) {
	case 2:
		afe_channel_mode = MSM_AFE_STEREO;
		break;
	case 1:
	default:
		afe_channel_mode = MSM_AFE_MONO;
		break;
	}
	afe_config.mi2s.channel = afe_channel_mode;
	afe_config.mi2s.bitwidth = 16;
	afe_config.mi2s.line = 1;
	if (msm_codec_i2s_slave_mode)
		afe_config.mi2s.ws = 0;
	else
		afe_config.mi2s.ws = 1;

	trc = afe_open(icodec->data->copp_id, &afe_config, icodec->sample_rate);

	if (trc < 0)
		pr_aud_err("%s: afe open failed, trc = %d\n", __func__, trc);

	if (support_adie) {
		/* Enable ADIE */
		if (icodec->adie_path) {
			adie_codec_proceed_stage(icodec->adie_path,
					ADIE_CODEC_DIGITAL_READY);
			adie_codec_proceed_stage(icodec->adie_path,
					ADIE_CODEC_DIGITAL_ANALOG_READY);
		}

		if (msm_codec_i2s_slave_mode)
			adie_codec_set_master_mode(icodec->adie_path, 1);
		else
			adie_codec_set_master_mode(icodec->adie_path, 0);
	}
	/* Enable power amplifier */
	if (icodec->data->pamp_on)
		icodec->data->pamp_on(1);

	icodec->enabled = 1;

	wake_unlock(&drv->rx_idlelock);
	return 0;

error_adie:
	clk_disable(drv->rx_bitclk);
	clk_disable(drv->rx_osrclk);
error_invalid_freq:

	pr_aud_err("%s: encounter error\n", __func__);
	msm_snddev_rx_mclk_free();

	wake_unlock(&drv->rx_idlelock);
	return -ENODEV;
}

static int snddev_icodec_open_tx(struct snddev_icodec_state *icodec)
{
	int trc;
	int rc_clk;
	int afe_channel_mode;
	union afe_port_config afe_config;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;;

	wake_lock(&drv->tx_idlelock);

	if (drv->snddev_vreg)
		vreg_mode_vote(drv->snddev_vreg, 1, SNDDEV_HIGH_POWER_MODE);

	if (support_aic3254_use_mclk) {
		rc_clk = snddev_icodec_rxclk_enable(icodec, 1);
		if (IS_ERR_VALUE(rc_clk)) {
			pr_aud_err("%s Enable RX master clock Error\n", \
					__func__);
			goto error_invalid_osrclk;
		}
	}

	msm_snddev_tx_mclk_request();

	drv->tx_osrclk = clk_get(0, "i2s_mic_osr_clk");
	if (IS_ERR(drv->tx_osrclk)) {
		pr_aud_err("%s master clock Error\n", __func__);
		goto error_invalid_osrclk;
	}

	trc =  clk_set_rate(drv->tx_osrclk,
			SNDDEV_ICODEC_CLK_RATE(icodec->sample_rate));
	if (IS_ERR_VALUE(trc)) {
		pr_aud_err("ERROR setting m clock1\n");
		goto error_invalid_freq;
	}

	clk_enable(drv->tx_osrclk);

	drv->tx_bitclk = clk_get(0, "i2s_mic_bit_clk");
	if (IS_ERR(drv->tx_bitclk)) {
		pr_aud_err("%s clock Error\n", __func__);
		goto error_invalid_bitclk;
	}

	/* ***************************************
	 * 1. CPU MASTER MODE:
	 *     Master clock = Sample Rate * OSR rate bit clock
	 * OSR Rate bit clock = bit/sample * channel master
	 * clock / bit clock = divider value = 8
	 *
	 * 2. CPU SLAVE MODE:
	 *     bitclk = 0
	 * *************************************** */
	if (msm_codec_i2s_slave_mode) {
		pr_debug("%s: configuring bit clock for slave mode\n",
				__func__);
		trc =  clk_set_rate(drv->tx_bitclk, 0);
	} else
		trc =  clk_set_rate(drv->tx_bitclk, 8);

	clk_enable(drv->tx_bitclk);

	if (support_aic3254) {
		if (aic3254_ops->aic3254_set_mode) {
			if (msm_get_call_state() == 1)
				aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_TX,
					icodec->data->aic3254_voc_id);
			else
				aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_TX,
					icodec->data->aic3254_id);
		}
	}
	if (support_adie) {
		/* Enable ADIE */
		trc = adie_codec_open(icodec->data->profile, &icodec->adie_path);
		if (IS_ERR_VALUE(trc))
			pr_aud_err("%s: adie codec open failed\n", __func__);
		else
			adie_codec_setpath(icodec->adie_path,
						icodec->sample_rate, 256);
	}
	switch (icodec->data->channel_mode) {
	case 2:
		afe_channel_mode = MSM_AFE_STEREO;
		break;
	case 1:
	default:
		afe_channel_mode = MSM_AFE_MONO;
		break;
	}
	afe_config.mi2s.channel = afe_channel_mode;
	afe_config.mi2s.bitwidth = 16;
	afe_config.mi2s.line = 1;
	if (msm_codec_i2s_slave_mode)
		afe_config.mi2s.ws = 0;
	else
		afe_config.mi2s.ws = 1;

	trc = afe_open(icodec->data->copp_id, &afe_config, icodec->sample_rate);

	if (icodec->adie_path && support_adie) {
		adie_codec_proceed_stage(icodec->adie_path,
					ADIE_CODEC_DIGITAL_READY);
		adie_codec_proceed_stage(icodec->adie_path,
					ADIE_CODEC_DIGITAL_ANALOG_READY);

		if (msm_codec_i2s_slave_mode)
			adie_codec_set_master_mode(icodec->adie_path, 1);
		else
			adie_codec_set_master_mode(icodec->adie_path, 0);
	}

	/* Reuse pamp_on for TX platform-specific setup  */
	if (icodec->data->pamp_on)
		icodec->data->pamp_on(1);

	icodec->enabled = 1;

	wake_unlock(&drv->tx_idlelock);
	return 0;

error_invalid_bitclk:
	clk_disable(drv->tx_osrclk);
error_invalid_freq:
error_invalid_osrclk:
	if (icodec->data->pamp_on)
		icodec->data->pamp_on(0);
	msm_snddev_tx_mclk_free();

	pr_aud_err("%s: encounter error\n", __func__);

	wake_unlock(&drv->tx_idlelock);
	return -ENODEV;
}

static int snddev_icodec_close_rx(struct snddev_icodec_state *icodec)
{
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;
	struct snddev_icodec_data *data = icodec->data;

	wake_lock(&drv->rx_idlelock);

	if (drv->snddev_vreg)
		vreg_mode_vote(drv->snddev_vreg, 0, SNDDEV_HIGH_POWER_MODE);

	/* Disable power amplifier */
	if (icodec->data->pamp_on)
		icodec->data->pamp_on(0);

	if (support_aic3254) {
		/* Restore default id for A3254 */
		if (data->aic3254_id != data->default_aic3254_id)
			data->aic3254_id = data->default_aic3254_id;
		/* Disable External Codec A3254 */
		if (aic3254_ops->aic3254_set_mode)
			aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_RX, DOWNLINK_OFF);
	}
	if (support_adie) {
		/* Disable ADIE */
		if (icodec->adie_path) {
			adie_codec_proceed_stage(icodec->adie_path,
				ADIE_CODEC_DIGITAL_OFF);
			adie_codec_close(icodec->adie_path);
			icodec->adie_path = NULL;
		}
	}

	afe_close(icodec->data->copp_id);

	if (icodec->data->voltage_on)
		icodec->data->voltage_on(0);

	clk_disable(drv->rx_bitclk);

	if (support_aic3254_use_mclk)
		snddev_icodec_rxclk_enable(icodec, 0);
	else
		clk_disable(drv->rx_osrclk);

	msm_snddev_rx_mclk_free();

	icodec->enabled = 0;

	wake_unlock(&drv->rx_idlelock);
	return 0;
}

static int snddev_icodec_close_tx(struct snddev_icodec_state *icodec)
{
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;
	struct snddev_icodec_data *data = icodec->data;

	wake_lock(&drv->tx_idlelock);

	if (drv->snddev_vreg)
		vreg_mode_vote(drv->snddev_vreg, 0, SNDDEV_HIGH_POWER_MODE);

	/* Reuse pamp_off for TX platform-specific setup  */
	if (icodec->data->pamp_on)
		icodec->data->pamp_on(0);

	if (support_aic3254) {
		/* Restore default id for A3254 */
		if (data->aic3254_id != data->default_aic3254_id)
			data->aic3254_id = data->default_aic3254_id;
		/* Disable External Codec A3254 */
		if (aic3254_ops->aic3254_set_mode)
			aic3254_ops->aic3254_set_mode(AIC3254_CONFIG_TX, UPLINK_OFF);
	}
	if (support_adie) {
		/* Disable ADIE */
		if (icodec->adie_path) {
			adie_codec_proceed_stage(icodec->adie_path,
						ADIE_CODEC_DIGITAL_OFF);
			adie_codec_close(icodec->adie_path);
			icodec->adie_path = NULL;
		}
	}
	afe_close(icodec->data->copp_id);

	clk_disable(drv->tx_bitclk);
	clk_disable(drv->tx_osrclk);

	if (support_aic3254_use_mclk)
		snddev_icodec_rxclk_enable(icodec, 0);

	msm_snddev_tx_mclk_free();


	icodec->enabled = 0;

	wake_unlock(&drv->tx_idlelock);
	return 0;
}

static int snddev_icodec_set_device_volume_impl(
		struct msm_snddev_info *dev_info, u32 volume)
{
	struct snddev_icodec_state *icodec;

	int rc = 0;

	icodec = dev_info->private_data;

	if (icodec->data->dev_vol_type & SNDDEV_DEV_VOL_DIGITAL) {

		rc = adie_codec_set_device_digital_volume(icodec->adie_path,
				icodec->data->channel_mode, volume);
		if (rc < 0) {
			pr_aud_err("%s: unable to set_device_digital_volume for"
				"%s volume in percentage = %u\n",
				__func__, dev_info->name, volume);
			return rc;
		}

	} else if (icodec->data->dev_vol_type & SNDDEV_DEV_VOL_ANALOG)
		rc = adie_codec_set_device_analog_volume(icodec->adie_path,
				icodec->data->channel_mode, volume);
		if (rc < 0) {
			pr_aud_err("%s: unable to set_device_analog_volume for"
				"%s volume in percentage = %u\n",
				__func__, dev_info->name, volume);
			return rc;
		}
	else {
		pr_aud_err("%s: Invalid device volume control\n", __func__);
		return -EPERM;
	}
	return rc;
}

static int snddev_icodec_open(struct msm_snddev_info *dev_info)
{
	int rc = 0;
	struct snddev_icodec_state *icodec;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;

	if (!dev_info) {
		rc = -EINVAL;
		goto error;
	}

	icodec = dev_info->private_data;

	if (icodec->data->capability & SNDDEV_CAP_RX) {
		mutex_lock(&drv->rx_lock);
		if (drv->rx_active) {
			mutex_unlock(&drv->rx_lock);
			pr_aud_err("%s: rx_active is set, return EBUSY\n",
				__func__);
			rc = -EBUSY;
			goto error;
		}
		rc = snddev_icodec_open_rx(icodec);

		if (!IS_ERR_VALUE(rc)) {
			drv->rx_active = 1;
			if (support_adie && (icodec->data->dev_vol_type & (
				SNDDEV_DEV_VOL_DIGITAL |
				SNDDEV_DEV_VOL_ANALOG)))
				rc = snddev_icodec_set_device_volume_impl(
					dev_info, dev_info->dev_volume);
			if (!IS_ERR_VALUE(rc))
				drv->rx_active = 1;
			else
				pr_aud_err("%s: set_device_volume_impl"
					" error(rx) = %d\n", __func__, rc);
		}
		mutex_unlock(&drv->rx_lock);
	} else {
		mutex_lock(&drv->tx_lock);
		if (drv->tx_active) {
			mutex_unlock(&drv->tx_lock);
			pr_aud_err("%s: tx_active is set, return EBUSY\n",
				__func__);
			rc = -EBUSY;
			goto error;
		}
		rc = snddev_icodec_open_tx(icodec);

		if (!IS_ERR_VALUE(rc)) {
			drv->tx_active = 1;
			if (support_adie && (icodec->data->dev_vol_type & (
				SNDDEV_DEV_VOL_DIGITAL |
				SNDDEV_DEV_VOL_ANALOG)))
				rc = snddev_icodec_set_device_volume_impl(
					dev_info, dev_info->dev_volume);
			if (!IS_ERR_VALUE(rc))
				drv->tx_active = 1;
			else
				pr_aud_err("%s: set_device_volume_impl"
					" error(tx) = %d\n", __func__, rc);
		}
		mutex_unlock(&drv->tx_lock);
	}
error:
	return rc;
}

static int snddev_icodec_close(struct msm_snddev_info *dev_info)
{
	int rc = 0;
	struct snddev_icodec_state *icodec;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;
	if (!dev_info) {
		rc = -EINVAL;
		goto error;
	}

	icodec = dev_info->private_data;

	if (icodec->data->capability & SNDDEV_CAP_RX) {
		mutex_lock(&drv->rx_lock);
		if (!drv->rx_active) {
			mutex_unlock(&drv->rx_lock);
			pr_aud_err("%s: rx_active not set, return\n", __func__);
			rc = -EPERM;
			goto error;
		}
		rc = snddev_icodec_close_rx(icodec);
		if (!IS_ERR_VALUE(rc))
			drv->rx_active = 0;
		else
			pr_aud_err("%s: close rx failed, rc = %d\n", __func__, rc);
		mutex_unlock(&drv->rx_lock);
	} else {
		mutex_lock(&drv->tx_lock);
		if (!drv->tx_active) {
			mutex_unlock(&drv->tx_lock);
			pr_aud_err("%s: tx_active not set, return\n", __func__);
			rc = -EPERM;
			goto error;
		}
		rc = snddev_icodec_close_tx(icodec);
		if (!IS_ERR_VALUE(rc))
			drv->tx_active = 0;
		else
			pr_aud_err("%s: close tx failed, rc = %d\n", __func__, rc);
		mutex_unlock(&drv->tx_lock);
	}

error:
	return rc;
}

static int snddev_icodec_check_freq(u32 req_freq)
{
	int rc = -EINVAL;

	if ((req_freq != 0) && (req_freq >= 8000) && (req_freq <= 48000)) {
		if ((req_freq == 8000) || (req_freq == 11025) ||
			(req_freq == 12000) || (req_freq == 16000) ||
			(req_freq == 22050) || (req_freq == 24000) ||
			(req_freq == 32000) || (req_freq == 44100) ||
			(req_freq == 48000)) {
				rc = 0;
		} else
			pr_aud_info("%s: Unsupported Frequency:%d\n", __func__,
								req_freq);
	}
	return rc;
}

static int snddev_icodec_set_freq(struct msm_snddev_info *dev_info, u32 rate)
{
	int rc;
	struct snddev_icodec_state *icodec;

	if (!dev_info) {
		rc = -EINVAL;
		goto error;
	}

	icodec = dev_info->private_data;
	if (support_adie &&
		adie_codec_freq_supported(icodec->data->profile, rate) != 0) {
		pr_aud_err("%s: adie_codec_freq_supported() failed\n", __func__);
		rc = -EINVAL;
		goto error;
	} else {
		if (snddev_icodec_check_freq(rate) != 0) {
			pr_aud_err("%s: check_freq failed\n", __func__);
			rc = -EINVAL;
			goto error;
		} else
			icodec->sample_rate = rate;
	}

	if (icodec->enabled) {
		snddev_icodec_close(dev_info);
		snddev_icodec_open(dev_info);
	}

	return icodec->sample_rate;

error:
	return rc;
}

static int snddev_icodec_enable_sidetone(struct msm_snddev_info *dev_info,
	u32 enable, uint16_t gain)
{
	int rc = 0;
	struct snddev_icodec_state *icodec;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;

	/*3254 sidetone will be binded with dsp image.*/
	if (support_aic3254 || !support_adie)
		goto error;

	if (!dev_info) {
		pr_aud_err("invalid dev_info\n");
		rc = -EINVAL;
		goto error;
	}

	icodec = dev_info->private_data;

	if (icodec->data->capability & SNDDEV_CAP_RX) {
		mutex_lock(&drv->rx_lock);
		if (!drv->rx_active || !dev_info->opened) {
			pr_aud_err("dev not active\n");
			rc = -EPERM;
			mutex_unlock(&drv->rx_lock);
			goto error;
		}
		rc = afe_sidetone(PRIMARY_I2S_TX, PRIMARY_I2S_RX, enable, gain);
		if (rc < 0)
			pr_aud_err("%s: AFE command sidetone failed\n", __func__);
		mutex_unlock(&drv->rx_lock);
	} else {
		rc = -EINVAL;
		pr_aud_err("rx device only\n");
	}

error:
	return rc;

}
static int snddev_icodec_enable_anc(struct msm_snddev_info *dev_info,
	u32 enable)
{
	int rc = 0;
	struct adie_codec_anc_data *reg_writes;
	struct acdb_cal_block cal_block;
	struct snddev_icodec_state *icodec;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;

	if (support_aic3254 || !support_adie)
		goto error;

	pr_aud_info("%s: enable=%d\n", __func__, enable);

	if (!dev_info) {
		pr_aud_err("invalid dev_info\n");
		rc = -EINVAL;
		goto error;
	}
	icodec = dev_info->private_data;

	if ((icodec->data->capability & SNDDEV_CAP_RX) &&
		(icodec->data->capability & SNDDEV_CAP_ANC)) {
		mutex_lock(&drv->rx_lock);

		if (!drv->rx_active || !dev_info->opened) {
			pr_aud_err("dev not active\n");
			rc = -EPERM;
			mutex_unlock(&drv->rx_lock);
			goto error;
		}
		if (enable) {
			get_anc_cal(&cal_block);
			reg_writes = (struct adie_codec_anc_data *)
				cal_block.cal_kvaddr;

			if (reg_writes == NULL) {
				pr_aud_err("error, no calibration data\n");
				rc = -1;
				mutex_unlock(&drv->rx_lock);
				goto error;
			}

			rc = adie_codec_enable_anc(icodec->adie_path,
			1, reg_writes);
		} else {
			rc = adie_codec_enable_anc(icodec->adie_path,
			0, NULL);
		}
		mutex_unlock(&drv->rx_lock);
	} else {
		rc = -EINVAL;
		pr_aud_err("rx and ANC device only\n");
	}

error:
	return rc;

}

int snddev_icodec_set_device_volume(struct msm_snddev_info *dev_info,
		u32 volume)
{
	struct snddev_icodec_state *icodec;
	struct mutex *lock;
	struct snddev_icodec_drv_state *drv = &snddev_icodec_drv;
	int rc = -EPERM;

	if (!dev_info) {
		pr_aud_info("%s : device not intilized.\n", __func__);
		return  -EINVAL;
	}

	icodec = dev_info->private_data;

	if (!(icodec->data->dev_vol_type & (SNDDEV_DEV_VOL_DIGITAL
				| SNDDEV_DEV_VOL_ANALOG))) {

		pr_aud_info("%s : device %s does not support device volume "
				"control.", __func__, dev_info->name);
		return -EPERM;
	}
	dev_info->dev_volume =  volume;

	if (icodec->data->capability & SNDDEV_CAP_RX)
		lock = &drv->rx_lock;
	else
		lock = &drv->tx_lock;

	mutex_lock(lock);

	rc = snddev_icodec_set_device_volume_impl(dev_info,
			dev_info->dev_volume);
	mutex_unlock(lock);
	return rc;
}

void htc_8x60_register_icodec_ops(struct q6v2audio_icodec_ops *ops)
{
	audio_ops = ops;
}

static int snddev_icodec_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct snddev_icodec_data *pdata;
	struct msm_snddev_info *dev_info;
	struct snddev_icodec_state *icodec;
	static int first_time = 1;

	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Invalid caller\n");
		rc = -1;
		goto error;
	}
	pdata = pdev->dev.platform_data;
	if ((pdata->capability & SNDDEV_CAP_RX) &&
	   (pdata->capability & SNDDEV_CAP_TX)) {
		pr_aud_err("%s: invalid device data either RX or TX\n", __func__);
		goto error;
	}
	icodec = kzalloc(sizeof(struct snddev_icodec_state), GFP_KERNEL);
	if (!icodec) {
		rc = -ENOMEM;
		goto error;
	}
	dev_info = kmalloc(sizeof(struct msm_snddev_info), GFP_KERNEL);
	if (!dev_info) {
		kfree(icodec);
		rc = -ENOMEM;
		goto error;
	}

	dev_info->name = pdata->name;
	dev_info->copp_id = pdata->copp_id;
	dev_info->private_data = (void *) icodec;
	dev_info->dev_ops.open = snddev_icodec_open;
	dev_info->dev_ops.close = snddev_icodec_close;
	dev_info->dev_ops.set_freq = snddev_icodec_set_freq;
	dev_info->dev_ops.set_device_volume = snddev_icodec_set_device_volume;
	dev_info->capability = pdata->capability;
	dev_info->opened = 0;
	msm_snddev_register(dev_info);
	icodec->data = pdata;
	icodec->sample_rate = pdata->default_sample_rate;
	dev_info->sample_rate = pdata->default_sample_rate;
	dev_info->channel_mode = pdata->channel_mode;
	if (pdata->capability & SNDDEV_CAP_RX)
		dev_info->dev_ops.enable_sidetone =
			snddev_icodec_enable_sidetone;
	else
		dev_info->dev_ops.enable_sidetone = NULL;

	if (pdata->capability & SNDDEV_CAP_ANC) {
		dev_info->dev_ops.enable_anc =
		snddev_icodec_enable_anc;
	} else {
		dev_info->dev_ops.enable_anc = NULL;
	}
	if (first_time) {
		if (audio_ops->support_aic3254)
			support_aic3254 = audio_ops->support_aic3254();
		else
			support_aic3254 = 0;

		pr_aud_info("%s: support_aic3254 = %d\n",
			__func__, support_aic3254);

		if (audio_ops->support_adie)
			support_adie = audio_ops->support_adie();
		else
			support_adie = 0;

		pr_aud_info("%s: support_adie = %d\n",
			__func__, support_adie);

		if (audio_ops->is_msm_i2s_slave)
			msm_codec_i2s_slave_mode = audio_ops->is_msm_i2s_slave();
		else
			msm_codec_i2s_slave_mode = 0;

		pr_aud_info("%s: msm_codec_i2s_slave_mode = %d\n",
			__func__, msm_codec_i2s_slave_mode);

		if (audio_ops->support_aic3254_use_mclk)
			support_aic3254_use_mclk = \
					audio_ops->support_aic3254_use_mclk();
		else
			support_aic3254_use_mclk = 0;
		pr_aud_info("%s: support_aic3254_use_mclk = %d\n",
			__func__, support_aic3254_use_mclk);

		first_time = 0;
	}

error:
	return rc;
}

static int snddev_icodec_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver snddev_icodec_driver = {
  .probe = snddev_icodec_probe,
  .remove = snddev_icodec_remove,
  .driver = { .name = "snddev_icodec" }
};


void htc_8x60_register_aic3254_ops(struct q6v2audio_aic3254_ops *ops)
{
	aic3254_ops = ops;
}

int update_aic3254_info(struct aic3254_info *info)
{
	struct msm_snddev_info *dev_info;
	int rc = 0;

	dev_info = audio_dev_ctrl_find_dev(info->dev_id);
	if (IS_ERR(dev_info))
		rc = -ENODEV;
	else {
		if ((dev_info->copp_id == PRIMARY_I2S_RX) ||
			(dev_info->copp_id == PRIMARY_I2S_TX)) {
			struct snddev_icodec_state *icodec;
			icodec = dev_info->private_data;
			icodec->data->aic3254_id = info->path_id;
			pr_aud_info("%s: update aic3254 id of device %s as %d\n",
				__func__, dev_info->name, icodec->data->aic3254_id);
		}
	}

	return rc;
}

module_param(msm_codec_i2s_slave_mode, bool, 0);
MODULE_PARM_DESC(msm_codec_i2s_slave_mode, "Set MSM to I2S slave clock mode");

static int __init snddev_icodec_init(void)
{
	s32 rc;
	struct snddev_icodec_drv_state *icodec_drv = &snddev_icodec_drv;

	rc = platform_driver_register(&snddev_icodec_driver);
	if (IS_ERR_VALUE(rc)) {
		pr_aud_err("%s: platform_driver_register for snddev icodec failed\n",
					__func__);
		goto error_snddev_icodec_driver;
	}

	rc = platform_driver_register(&msm_cdcclk_ctl_driver);
	if (IS_ERR_VALUE(rc)) {
		pr_aud_err("%s: platform_driver_register for msm snddev failed\n",
					__func__);
		goto error_msm_cdcclk_ctl_driver;
	}

	mutex_init(&icodec_drv->rx_lock);
	mutex_init(&icodec_drv->tx_lock);

	mutex_init(&icodec_drv->rx_mclk_lock);

	icodec_drv->rx_active = 0;
	icodec_drv->tx_active = 0;
	icodec_drv->snddev_vreg = vreg_init();

	wake_lock_init(&icodec_drv->tx_idlelock, WAKE_LOCK_IDLE,
			"snddev_tx_idle");
	wake_lock_init(&icodec_drv->rx_idlelock, WAKE_LOCK_IDLE,
			"snddev_rx_idle");
	return 0;

error_msm_cdcclk_ctl_driver:
	platform_driver_unregister(&snddev_icodec_driver);
error_snddev_icodec_driver:
	return -ENODEV;
}

static void __exit snddev_icodec_exit(void)
{
	struct snddev_icodec_drv_state *icodec_drv = &snddev_icodec_drv;

	platform_driver_unregister(&snddev_icodec_driver);
	platform_driver_unregister(&msm_cdcclk_ctl_driver);

	clk_put(icodec_drv->rx_osrclk);
	clk_put(icodec_drv->tx_osrclk);
	if (icodec_drv->snddev_vreg) {
		vreg_deinit(icodec_drv->snddev_vreg);
		icodec_drv->snddev_vreg = NULL;
	}
	return;
}

module_init(snddev_icodec_init);
module_exit(snddev_icodec_exit);

MODULE_DESCRIPTION("ICodec Sound Device driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
