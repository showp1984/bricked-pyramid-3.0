 /*
 * Copyright (C) 2011 HTC Corporation
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

#ifndef __HTC_WATCHDOG_MONITOR_H
#define __HTC_WATCHDOG_MONITOR_H

void htc_watchdog_monitor_init(void);
void htc_watchdog_pet_cpu_record(void);
void htc_watchdog_top_stat(void);

#endif
