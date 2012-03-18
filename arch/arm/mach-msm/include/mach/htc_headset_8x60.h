/*
 *
 * /arch/arm/mach-msm/include/mach/htc_headset_8x60.h
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

#ifndef HTC_HEADSET_8X60_H
#define HTC_HEADSET_8X60_H

struct htc_headset_8x60_platform_data {
	uint32_t adc_mpp;
	uint32_t adc_amux;
	uint32_t adc_mic_bias[2];
	uint32_t adc_remote[6];
};

struct htc_headset_8x60_info {
	struct htc_headset_8x60_platform_data pdata;
	struct wake_lock hs_wake_lock;
};

#endif
