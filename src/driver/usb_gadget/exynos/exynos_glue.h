/*
 * \brief  Slim Exynos glue of the GS201's DWC3
 * \author Ivan Loskutov
 * \date   2026-10-23
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _USB_GADGET__EXYNOS_GLUE_H_
#define _USB_GADGET__EXYNOS_GLUE_H_

struct dwc3;

/* vendor register setup, before the gadget is connected */
void exynos_dwc3_config(struct dwc3 *dwc);

#endif /* _USB_GADGET__EXYNOS_GLUE_H_ */
