/*
 * \brief  Included before pcie-exynos-rc.c (dwc-whi)
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The driver uses two functions of the Pixel kernel whose headers it
 * includes only in configurations not built here, both are in lx_emul.c.
 * exynos-pm.h is not included, it defines data.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PCIE_SHIM_H_
#define _PCIE_SHIM_H_

#include <linux/types.h>

/* access to a register of the secure world */
#include <soc/google/exynos-el3_mon.h>

/* soc/google/exynos-pm.h: link state for the CPU idle driver */
int register_pcie_is_connect(u32 (*func)(void));

#endif /* _PCIE_SHIM_H_ */
