/* linux/arch/arm/mach-msm/board-pyramid-mmc.c
 *
 * Copyright (C) 2008 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>

#include <asm/gpio.h>
#include <asm/io.h>

#include <mach/vreg.h>
#include <mach/htc_pwrsink.h>

#include <asm/mach/mmc.h>

#include "devices.h"
#include "board-pyramid.h"
#include "proc_comm.h"
#include <mach/msm_iomap.h>
#include <linux/mfd/pmic8058.h>
//#include <mach/htc_sleep_clk.h>
#include "mpm.h"
#include <linux/irq.h>

#if 0
static int msm_sdcc_cfg_mpm_sdiowakeup(struct device *dev, unsigned mode)
{
       struct platform_device *pdev;
       enum msm_mpm_pin pin;
        int ret = 0;

       pdev = container_of(dev, struct platform_device, dev);

       /* Only SDCC4 slot connected to WLAN chip has wakeup capability */
       if (pdev->id == 4)
               pin = MSM_MPM_PIN_SDC4_DAT1;
       else
               return -EINVAL;

        switch (mode) {
        case SDC_DAT1_DISABLE:
                ret = msm_mpm_enable_pin(pin, 0);
                break;
        case SDC_DAT1_ENABLE:
                ret = msm_mpm_set_pin_type(pin, IRQ_TYPE_LEVEL_LOW);
                ret = msm_mpm_enable_pin(pin, 1);
                break;
        case SDC_DAT1_ENWAKE:
                ret = msm_mpm_set_pin_wake(pin, 1);
                break;
        case SDC_DAT1_DISWAKE:
                ret = msm_mpm_set_pin_wake(pin, 0);
                break;
        default:
                ret = -EINVAL;
                break;
        }
        return ret;
}
#endif

//#include <linux/module.h>
int msm_proc_comm(unsigned cmd, unsigned *data1, unsigned *data2);

extern int msm_add_sdcc(unsigned int controller, struct mmc_platform_data *plat);

/* ---- SDCARD ---- */
/* ---- WIFI ---- */

static uint32_t wifi_on_gpio_table[] = {
	//GPIO_CFG(116, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT3 */
	//GPIO_CFG(117, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT2 */
	//GPIO_CFG(118, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT1 */
	//GPIO_CFG(119, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT0 */
	//GPIO_CFG(111, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), /* CMD */
	//GPIO_CFG(110, 1, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_8MA), /* CLK */
	GPIO_CFG(PYRAMID_GPIO_WIFI_IRQ, 0, GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_4MA), /* WLAN IRQ */
};

static uint32_t wifi_off_gpio_table[] = {
	//GPIO_CFG(116, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT3 */
	//GPIO_CFG(117, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT2 */
	//GPIO_CFG(118, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT1 */
	//GPIO_CFG(119, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* DAT0 */
	//GPIO_CFG(111, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_4MA), /* CMD */
	//GPIO_CFG(110, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_4MA), /* CLK */
	GPIO_CFG(PYRAMID_GPIO_WIFI_IRQ, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_4MA), /* WLAN IRQ */
};

static void config_gpio_table(uint32_t *table, int len)
{
        int n, rc;
        for (n = 0; n < len; n++) {
                rc = gpio_tlmm_config(table[n], GPIO_CFG_ENABLE);
                if (rc) {
                        pr_err("%s: gpio_tlmm_config(%#x)=%d\n",
                                __func__, table[n], rc);
                        break;
                }
        }
}

/* BCM4329 returns wrong sdio_vsn(1) when we read cccr,
 * we use predefined value (sdio_vsn=2) here to initial sdio driver well
 */
static struct embedded_sdio_data pyramid_wifi_emb_data = {
	.cccr	= {
		.sdio_vsn	= 2,
		.multi_block	= 1,
		.low_speed	= 0,
		.wide_bus	= 0,
		.high_power	= 1,
		.high_speed	= 1,
	}
};

static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int
pyramid_wifi_status_register(void (*callback)(int card_present, void *dev_id),
				void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;

	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

static int pyramid_wifi_cd;	/* WiFi virtual 'card detect' status */

static unsigned int pyramid_wifi_status(struct device *dev)
{
	return pyramid_wifi_cd;
}

static unsigned int pyramid_wifislot_type = MMC_TYPE_SDIO_WIFI;
static struct mmc_platform_data pyramid_wifi_data = {
        .ocr_mask               = MMC_VDD_28_29,
        .status                 = pyramid_wifi_status,
        .register_status_notify = pyramid_wifi_status_register,
        .embedded_sdio          = &pyramid_wifi_emb_data,
        .mmc_bus_width  = MMC_CAP_4_BIT_DATA,
        .slot_type = &pyramid_wifislot_type,
        .msmsdcc_fmin   = 400000,
        .msmsdcc_fmid   = 24000000,
        .msmsdcc_fmax   = 48000000,
        .nonremovable   = 0,
	.pclk_src_dfab	= 1,
	//.cfg_mpm_sdiowakeup = msm_sdcc_cfg_mpm_sdiowakeup,
	// HTC_WIFI_MOD, temp remove dummy52
	//.dummy52_required = 1,
};


int pyramid_wifi_set_carddetect(int val)
{
	printk(KERN_INFO "%s: %d\n", __func__, val);
	pyramid_wifi_cd = val;
	if (wifi_status_cb)
		wifi_status_cb(val, wifi_status_cb_devid);
	else
		printk(KERN_WARNING "%s: Nobody to notify\n", __func__);
	return 0;
}
EXPORT_SYMBOL(pyramid_wifi_set_carddetect);

int pyramid_wifi_power(int on)
{
	const unsigned SDC4_HDRV_PULL_CTL_ADDR = (unsigned) MSM_TLMM_BASE + 0x20A0;

	printk(KERN_INFO "%s: %d\n", __func__, on);

	if (on) {
		//SDC4_CMD_PULL = Pull Up, SDC4_DATA_PULL = Pull up
		writel(0x1FDB, SDC4_HDRV_PULL_CTL_ADDR);
		config_gpio_table(wifi_on_gpio_table,
				  ARRAY_SIZE(wifi_on_gpio_table));
	} else {
		//SDC4_CMD_PULL = Pull Down, SDC4_DATA_PULL = Pull Down
		writel(0x0BDB, SDC4_HDRV_PULL_CTL_ADDR);
		config_gpio_table(wifi_off_gpio_table,
				  ARRAY_SIZE(wifi_off_gpio_table));
	}
	//htc_wifi_bt_sleep_clk_ctl(on, ID_WIFI);
	mdelay(1);//Delay 1 ms, Recommand by Hardware
	gpio_set_value(PYRAMID_GPIO_WIFI_SHUTDOWN_N, on); /* WIFI_SHUTDOWN */

	mdelay(120);
	return 0;
}
EXPORT_SYMBOL(pyramid_wifi_power);

int pyramid_wifi_reset(int on)
{
	printk(KERN_INFO "%s: do nothing\n", __func__);
	return 0;
}

int __init pyramid_init_mmc()
{
	uint32_t id;
	wifi_status_cb = NULL;

	printk(KERN_INFO "pyramid: %s\n", __func__);

	/* initial WIFI_SHUTDOWN# */
	id = GPIO_CFG(PYRAMID_GPIO_WIFI_SHUTDOWN_N, 0, GPIO_CFG_OUTPUT,
		GPIO_CFG_NO_PULL, GPIO_CFG_2MA);
	gpio_tlmm_config(id, 0);
	gpio_set_value(PYRAMID_GPIO_WIFI_SHUTDOWN_N, 0);

	msm_add_sdcc(4, &pyramid_wifi_data);

	return 0;
}
