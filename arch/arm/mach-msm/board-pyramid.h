/* linux/arch/arm/mach-msm/board-spade.h
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
 */

#ifndef __ARCH_ARM_MACH_MSM_BOARD_PYRAMID_H
#define __ARCH_ARM_MACH_MSM_BOARD_PYRAMID_H

#include <mach/board.h>

#define PYRAMID_PROJECT_NAME	"pyramid"

#define MSM_RAM_CONSOLE_BASE	MSM_HTC_RAM_CONSOLE_PHYS
#define MSM_RAM_CONSOLE_SIZE	MSM_HTC_RAM_CONSOLE_SIZE

/* Memory map */

#if defined(CONFIG_CRYPTO_DEV_QCRYPTO) || \
		defined(CONFIG_CRYPTO_DEV_QCRYPTO_MODULE) || \
		defined(CONFIG_CRYPTO_DEV_QCEDEV) || \
		defined(CONFIG_CRYPTO_DEV_QCEDEV_MODULE)
#define QCE_SIZE		0x10000
#define QCE_0_BASE		0x18500000
#endif

#ifdef CONFIG_FB_MSM_TRIPLE_BUFFER
#define MSM_FB_PRIM_BUF_SIZE (960 * ALIGN(540, 32) * 4 * 3) /* 4 bpp x 3 pages */
#else
#define MSM_FB_PRIM_BUF_SIZE (960 * ALIGN(540, 32) * 4 * 2) /* 4 bpp x 2 pages */
#endif

#ifdef CONFIG_FB_MSM_HDMI_MSM_PANEL
#define MSM_FB_EXT_BUF_SIZE  (1920 * 1080 * 2 * 1) /* 2 bpp x 1 page */
#else
#define MSM_FB_EXT_BUFT_SIZE	0
#endif

#ifdef CONFIG_FB_MSM_OVERLAY_WRITEBACK
/* width x height x 3 bpp x 2 frame buffer */
#define MSM_FB_WRITEBACK_SIZE roundup(960 * ALIGN(540, 32) * 3 * 2, 4096)
#define MSM_FB_WRITEBACK_OFFSET 0
#else
#define MSM_FB_WRITEBACK_SIZE	0
#define MSM_FB_WRITEBACK_OFFSET 0
#endif

/* Note: must be multiple of 4096 */
#define MSM_FB_SIZE roundup(MSM_FB_PRIM_BUF_SIZE + MSM_FB_EXT_BUF_SIZE, 4096)

#define MSM_PMEM_MDP_SIZE	0x2000000
#define MSM_PMEM_ADSP_SIZE	0x23AC000
#define MSM_PMEM_ADSP2_SIZE	0x654000 /* 1152 * 1920 * 1.5 * 2 */
#define MSM_PMEM_AUDIO_SIZE	0x239000
#define MSM_PMEM_KERNEL_EBI1_SIZE	0xC7000

#define MSM_FB_WRITEBACK_BASE	(0x45C00000)
#define MSM_PMEM_AUDIO_BASE	(0x46400000)
#define MSM_PMEM_ADSP_BASE	(0x40400000)
#define MSM_PMEM_ADSP2_BASE	(MSM_PMEM_ADSP_BASE + MSM_PMEM_ADSP_SIZE)
#define MSM_FB_BASE		(0x70000000 - MSM_FB_SIZE)
#define MSM_PMEM_MDP_BASE	(0x6D600000)
#define MSM_PMEM_KERNEL_EBI1_BASE	(MSM_PMEM_AUDIO_BASE + MSM_PMEM_AUDIO_SIZE)

#define MSM_SMI_BASE          0x38000000
#define MSM_SMI_SIZE          0x4000000

/* Kernel SMI PMEM Region for video core, used for Firmware */
/* and encoder,decoder scratch buffers */
/* Kernel SMI PMEM Region Should always precede the user space */
/* SMI PMEM Region, as the video core will use offset address */
/* from the Firmware base */
#define KERNEL_SMI_BASE       (MSM_SMI_BASE)
#define KERNEL_SMI_SIZE       0x400000

/* User space SMI PMEM Region for video core*/
/* used for encoder, decoder input & output buffers  */
#define USER_SMI_BASE         (KERNEL_SMI_BASE + KERNEL_SMI_SIZE)
#define USER_SMI_SIZE         (MSM_SMI_SIZE - KERNEL_SMI_SIZE)
#define MSM_PMEM_SMIPOOL_BASE USER_SMI_BASE
#define MSM_PMEM_SMIPOOL_SIZE USER_SMI_SIZE

#define PHY_BASE_ADDR1  0x48000000
#define SIZE_ADDR1      0x25600000

/* GPIO definition */

/* Direct Keys */
#define PYRAMID_GPIO_KEY_POWER          (125)

/* Battery */
#define PYRAMID_GPIO_MBAT_IN            (61)
#define PYRAMID_GPIO_CHG_INT		(126)

/* Wifi */
#define PYRAMID_GPIO_WIFI_IRQ              (46)
#define PYRAMID_GPIO_WIFI_SHUTDOWN_N       (96)
/* Sensors */
#define PYRAMID_SENSOR_I2C_SDA		(72)
#define PYRAMID_SENSOR_I2C_SCL		(73)
#define PYRAMID_GYRO_INT               (127)
#define PYRAMID_ECOMPASS_INT           (128)
#define PYRAMID_GSENSOR_INT           (129)

/* Microp */

/* TP */
#define PYRAMID_TP_I2C_SDA           (51)
#define PYRAMID_TP_I2C_SCL           (52)
#define PYRAMID_TP_ATT_N             (65)
#define PYRAMID_TP_ATT_N_XB       (50)

/* LCD */
#define GPIO_LCM_RST_N			(66)
#define GPIO_LCM_ID			(50)

/* Audio */
#define PYRAMID_AUD_CODEC_RST        (67)

/* BT */
#define PYRAMID_GPIO_BT_HOST_WAKE      (45)
#define PYRAMID_GPIO_BT_UART1_TX       (53)
#define PYRAMID_GPIO_BT_UART1_RX       (54)
#define PYRAMID_GPIO_BT_UART1_CTS      (55)
#define PYRAMID_GPIO_BT_UART1_RTS      (56)
#define PYRAMID_GPIO_BT_SHUTDOWN_N     (100)
#define PYRAMID_GPIO_BT_CHIP_WAKE      (130)
#define PYRAMID_GPIO_BT_RESET_N        (142)

/* USB */
#define PYRAMID_GPIO_USB_ID        (63)
#define PYRAMID_GPIO_MHL_RESET        (70)
#define PYRAMID_GPIO_MHL_INT        (71)
#define PYRAMID_GPIO_MHL_USB_SWITCH        (99)

/* Camera */
#define PYRAMID_CAM_CAM1_ID           (10)
#define PYRAMID_CAM_I2C_SDA           (47)
#define PYRAMID_CAM_I2C_SCL           (48)

/* General */
#define PYRAMID_GENERAL_I2C_SDA		(59)
#define PYRAMID_GENERAL_I2C_SCL		(60)

/* Flashlight */
#define PYRAMID_FLASH_EN             (29)
#define PYRAMID_TORCH_EN             (30)

/* Accessory */
#define PYRAMID_GPIO_AUD_HP_DET        (31)

/* SPI */
#define PYRAMID_SPI_DO                 (33)
#define PYRAMID_SPI_DI                 (34)
#define PYRAMID_SPI_CS                 (35)
#define PYRAMID_SPI_CLK                (36)

/* PMIC */

/* PMIC GPIO definition */
#define PMGPIO(x) (x-1)
#define PYRAMID_VOL_UP             PMGPIO(16)
#define PYRAMID_VOL_DN             PMGPIO(17)
#define PYRAMID_AUD_HP_EN          PMGPIO(18)
#define PYRAMID_HAP_ENABLE         PMGPIO(19)
#define PYRAMID_AUD_QTR_RESET      PMGPIO(21)
#define PYRAMID_TP_RST             PMGPIO(23)
#define PYRAMID_GREEN_LED          PMGPIO(24)
#define PYRAMID_AMBER_LED          PMGPIO(25)
#define PYRAMID_AUD_MIC_SEL        PMGPIO(26)
#define PYRAMID_CHG_STAT	   PMGPIO(33)
#define PYRAMID_SDC3_DET           PMGPIO(34)
#define PYRAMID_PLS_INT            PMGPIO(35)
#define PYRAMID_AUD_REMO_PRES      PMGPIO(37)
#define PYRAMID_WIFI_BT_SLEEP_CLK  PMGPIO(38)


int __init pyramid_init_mmc(void);
void __init pyramid_audio_init(void);
int __init pyramid_init_keypad(void);
int __init pyramid_wifi_init(void);

#endif /* __ARCH_ARM_MACH_MSM_BOARD_PYRAMID_H */
