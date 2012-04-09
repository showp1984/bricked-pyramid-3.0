/* include/linux/cy8c_tmg_ts.c
 *
 * Copyright (C) 2007-2008 HTC Corporation.
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

#ifndef CY8C_I2C_H
#define CY8C_I2C_H

#include <linux/types.h>
#include <linux/input.h>

#define CYPRESS_TMA_NAME "CY8CTMA340"

struct cy8c_i2c_platform_data {
	uint16_t version;
	uint16_t gpio_irq;
	uint8_t orient;
	uint8_t timeout;
	uint8_t interval;
	uint8_t unlock_attr;
	uint8_t auto_reset;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	int abs_pressure_min;
	int abs_pressure_max;
	int abs_width_min;
	int abs_width_max;
	int (*power)(int on);
	int (*wake)(void);
	int (*reset)(void);
	uint16_t filter_level[4];
};

/* Sweep2Wake */
extern void sweep2wake_setdev(struct input_dev * input_device);

#endif

