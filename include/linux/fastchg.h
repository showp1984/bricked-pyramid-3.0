/*
 * Author: Chad Froebel <chadfroebel@gmail.com>
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


#ifndef _LINUX_FASTCHG_H
#define _LINUX_FASTCHG_H

extern int force_fast_charge;

#define FAST_CHARGE_DISABLED 0	/* default */
#define FAST_CHARGE_FORCE_AC 1
#define FAST_CHARGE_FORCE_AC_IF_NO_USB 2

extern int USB_peripheral_detected;

#define USB_ACC_NOT_DETECTED 0	/* default */
#define USB_ACC_DETECTED 1

#define USB_INVALID_DETECTED 0
#define USB_SDP_DETECTED 1
#define USB_DCP_DETECTED 2
#define USB_CDP_DETECTED 3
#define USB_ACA_A_DETECTED 4
#define USB_ACA_B_DETECTED 5
#define USB_ACA_C_DETECTED 6
#define USB_ACA_DOCK_DETECTED 7
#define NO_USB_DETECTED 10	/* default */

extern int USB_porttype_detected;

#endif
