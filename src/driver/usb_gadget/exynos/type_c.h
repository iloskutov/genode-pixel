/*
 * \brief  Cable orientation for the Linux side
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _USB_GADGET__TYPE_C_H_
#define _USB_GADGET__TYPE_C_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 0: CC1 (or unknown), 1: CC2, as EXTCON_PROP_USB_TYPEC_POLARITY */
int usb_gadget_cc_polarity(void);

#ifdef __cplusplus
}
#endif

#endif /* _USB_GADGET__TYPE_C_H_ */
