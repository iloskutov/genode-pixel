/*
 * \brief  Included before exynos-pcie-iommu-whi.c
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The SysMMU driver finds a second-level page table through the physical
 * address in the first-level entry. Its tables are allocated by DDE Linux,
 * which knows their virtual address. Without a linear map, phys_to_virt
 * asks DDE Linux.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _IOMMU_SHIM_H_
#define _IOMMU_SHIM_H_

#include <lx_emul/alloc.h>

#define phys_to_virt(x) ((void *)lx_emul_mem_virt_addr((void *)(unsigned long)(x)))

#endif /* _IOMMU_SHIM_H_ */
