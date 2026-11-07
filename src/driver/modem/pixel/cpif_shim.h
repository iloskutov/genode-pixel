/*
 * \brief  Included before every source file of the cpif
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * The memory shared with the CP is reserved memory, outside of what DDE
 * Linux allocates. Without a linear map, the cpif's phys_to_virt of it
 * becomes a mapping of the I/O memory, write-combined: the shared
 * structures are packed, a mapping as device memory faults on their
 * unaligned accesses. The mapping covers the whole I/O-memory resource of
 * the platform device, not only the page asked for.
 *
 * Every wakeup source of the cpif must exist, the common dummies register
 * none (lx_emul.c).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _CPIF_SHIM_H_
#define _CPIF_SHIM_H_

#include <linux/types.h>

void *lx_emul_modem_shared_mem(phys_addr_t phys, size_t size);

#define phys_to_virt(x) lx_emul_modem_shared_mem((phys_addr_t)(x), PAGE_SIZE)

#define wakeup_source_register lx_emul_modem_wakeup_source_register

#endif /* _CPIF_SHIM_H_ */
