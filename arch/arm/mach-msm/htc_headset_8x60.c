/*
 *
 * /arch/arm/mach-msm/htc_headset_8x60.c
 *
 * HTC 8X60 headset driver.
 *
 * Copyright (C) 2010 HTC, Inc.
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

#include <linux/pmic8058-xoadc.h>
#include <linux/slab.h>

#include <mach/htc_headset_mgr.h>
#include <mach/htc_headset_8x60.h>

#ifdef HTC_HEADSET_KERNEL_3_0
#include <linux/msm_adc.h>
#else
#include <linux/m_adc.h>
#endif

#define DRIVER_NAME "HS_8X60"

static struct workqueue_struct *button_wq;
static void button_8x60_work_func(struct work_struct *work);
static DECLARE_WORK(button_8x60_work, button_8x60_work_func);

static struct htc_headset_8x60_info *hi;

static int hs_8x60_remote_adc(int *adc)
{
	int ret = 0;

	HS_DBG();

	ret = pm8058_htc_config_mpp_and_adc_read(adc, 1, CHANNEL_ADC_HDSET,
						 hi->pdata.adc_mpp,
						 hi->pdata.adc_amux);
	if (ret) {
#if 0 /* ADC function in suspend mode */
		*adc = -1;
#endif
		HS_LOG("Failed to read remote ADC");
		return 0;
	}

	HS_LOG("Remote ADC %d (0x%X)", *adc, *adc);

	return 1;
}

static int hs_8x60_mic_status(void)
{
	int adc = 0;
	int mic = HEADSET_UNKNOWN_MIC;

	HS_DBG();

	if (!hs_8x60_remote_adc(&adc))
		return HEADSET_UNKNOWN_MIC;

/*
	if (hi->pdata.driver_flag & DRIVER_HS_PMIC_DYNAMIC_THRESHOLD)
		hs_pmic_remote_threshold((unsigned int) adc);
*/


	if (adc >= hi->pdata.adc_mic_bias[0] &&
	    adc <= hi->pdata.adc_mic_bias[1])
		mic = HEADSET_MIC;
	else if (adc < hi->pdata.adc_mic_bias[0])
		mic = HEADSET_NO_MIC;
	else
		mic = HEADSET_UNKNOWN_MIC;

	return mic;
}

static int hs_8x60_adc_to_keycode(int adc)
{
	int key_code = HS_MGR_KEY_INVALID;

	HS_DBG();

	if (!hi->pdata.adc_remote[5])
		return HS_MGR_KEY_INVALID;

	if (adc >= hi->pdata.adc_remote[0] &&
	    adc <= hi->pdata.adc_remote[1])
		key_code = HS_MGR_KEY_PLAY;
	else if (adc >= hi->pdata.adc_remote[2] &&
		 adc <= hi->pdata.adc_remote[3])
		key_code = HS_MGR_KEY_BACKWARD;
	else if (adc >= hi->pdata.adc_remote[4] &&
		 adc <= hi->pdata.adc_remote[5])
		key_code = HS_MGR_KEY_FORWARD;
	else if (adc > hi->pdata.adc_remote[5])
		key_code = HS_MGR_KEY_NONE;

	if (key_code != HS_MGR_KEY_INVALID)
		HS_LOG("Key code %d", key_code);
	else
		HS_LOG("Unknown key code %d", key_code);

	return key_code;
}

static void button_8x60_work_func(struct work_struct *work)
{
	int adc = 0;
	int key_code = HS_MGR_KEY_INVALID;

	HS_DBG();

	if (!hs_8x60_remote_adc(&adc))
		return;

	key_code = hs_8x60_adc_to_keycode(adc);

	if (key_code != HS_MGR_KEY_INVALID)
		hs_notify_key_event(key_code);
}

void hs_8x60_notify_key_event(void)
{
	HS_DBG();

	wake_lock_timeout(&hi->hs_wake_lock, HS_WAKE_LOCK_TIMEOUT);
	queue_work(button_wq, &button_8x60_work);
}
EXPORT_SYMBOL(hs_8x60_notify_key_event);

static void hs_8x60_register(void)
{
	struct headset_notifier notifier;

	notifier.id = HEADSET_REG_REMOTE_ADC;
	notifier.func = hs_8x60_remote_adc;
	headset_notifier_register(&notifier);

	notifier.id = HEADSET_REG_REMOTE_KEYCODE;
	notifier.func = hs_8x60_adc_to_keycode;
	headset_notifier_register(&notifier);

	notifier.id = HEADSET_REG_MIC_STATUS;
	notifier.func = hs_8x60_mic_status;
	headset_notifier_register(&notifier);
}

static int htc_headset_8x60_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct htc_headset_8x60_platform_data *pdata = pdev->dev.platform_data;

	HS_LOG("++++++++++++++++++++");

	hi = kzalloc(sizeof(struct htc_headset_8x60_info), GFP_KERNEL);
	if (!hi) {
		HS_ERR("Failed to allocate memory for headset info");
		return -ENOMEM;
	}

	hi->pdata.adc_mpp = pdata->adc_mpp;
	hi->pdata.adc_amux = pdata->adc_amux;

	if (pdata->adc_mic_bias[0] && pdata->adc_mic_bias[1]) {
		memcpy(hi->pdata.adc_mic_bias, pdata->adc_mic_bias,
		       sizeof(hi->pdata.adc_mic_bias));
	} else {
		hi->pdata.adc_mic_bias[0] = HS_DEF_MIC_ADC_15_BIT_MIN;
		hi->pdata.adc_mic_bias[1] = HS_DEF_MIC_ADC_15_BIT_MAX;
	}

	if (pdata->adc_remote[5])
		memcpy(hi->pdata.adc_remote, pdata->adc_remote,
		       sizeof(hi->pdata.adc_remote));

	wake_lock_init(&hi->hs_wake_lock, WAKE_LOCK_SUSPEND, DRIVER_NAME);

	button_wq = create_workqueue("HS_8X60_BUTTON");
	if (button_wq == NULL) {
		ret = -ENOMEM;
		HS_ERR("Failed to create button workqueue");
		goto err_create_button_work_queue;
	}

	hs_8x60_register();
	hs_notify_driver_ready(DRIVER_NAME);

	HS_LOG("--------------------");

	return 0;

err_create_button_work_queue:
	kfree(hi);

	HS_ERR("Failed to register %s driver", DRIVER_NAME);

	return ret;
}

static int htc_headset_8x60_remove(struct platform_device *pdev)
{
	destroy_workqueue(button_wq);
	kfree(hi);

	return 0;
}

static struct platform_driver htc_headset_8x60_driver = {
	.probe		= htc_headset_8x60_probe,
	.remove		= htc_headset_8x60_remove,
	.driver		= {
		.name		= "HTC_HEADSET_8X60",
		.owner		= THIS_MODULE,
	},
};

static int __init htc_headset_8x60_init(void)
{
	return platform_driver_register(&htc_headset_8x60_driver);
}

static void __exit htc_headset_8x60_exit(void)
{
	platform_driver_unregister(&htc_headset_8x60_driver);
}

module_init(htc_headset_8x60_init);
module_exit(htc_headset_8x60_exit);

MODULE_DESCRIPTION("HTC 8X60 headset driver");
MODULE_LICENSE("GPL");
