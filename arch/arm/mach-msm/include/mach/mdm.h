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
 */

#ifndef _ARCH_ARM_MACH_MSM_MDM_H
#define _ARXH_ARM_MACH_MSM_MDM_H

struct charm_platform_data {
	void (*charm_modem_on)(void);
	void (*charm_modem_off)(void);
	void (*charm_modem_reset)(void);
	void (*charm_modem_suspend)(void);
	void (*charm_modem_resume)(void);

	unsigned gpio_ap2mdm_status;
	unsigned gpio_ap2mdm_wakeup;
	unsigned gpio_ap2mdm_errfatal;
	unsigned gpio_ap2mdm_sync;
	unsigned gpio_ap2mdm_pmic_reset_n;
	unsigned gpio_ap2mdm_kpdpwr_n;
	unsigned gpio_ap2pmic_tmpni_cken;

	unsigned gpio_mdm2ap_status;
	unsigned gpio_mdm2ap_wakeup;
	unsigned gpio_mdm2ap_errfatal;
	unsigned gpio_mdm2ap_sync;
	unsigned gpio_mdm2ap_vfr;
};

/* Added by HTC */
unsigned charm_get_MDM_error_flag(void);
void charm_panic_notify(void);
void charm_panic_wait_mdm_shutdown(void);
void check_mdm9k_serial(void);
/*---------------------------------------------*/
#endif
