/*
 * \brief  Included before shm_ipc.c of the cpif
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * shm_ipc.c looks up the reserved memory of the CP at probe time only when
 * built as a module, otherwise in the early boot, which DDE Linux has not.
 * It is built as a module (-DMODULE) with the module's initcall turned into
 * a device initcall.
 *
 * The reserved memory has no struct pages in DDE Linux. The mapping of a
 * region without cache (cp_shmem_get_nc_region) collects the pages of the
 * region and maps them with vmap: here a page pointer is the physical
 * address of the page, and the region's pages are contiguous, so the
 * mapping of the first page's address is the I/O memory of the whole
 * region (lx_emul.c). The release of a region to the page allocator is
 * replaced by lx_emul.c, the original is renamed and unused.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SHM_IPC_SHIM_H_
#define _SHM_IPC_SHIM_H_

#include <linux/module.h>
#include <linux/vmalloc.h>

#undef  module_init
#define module_init(fn) __define_initcall(fn, 6)
#undef  module_exit
#define module_exit(fn)
#undef  MODULE_DEVICE_TABLE
#define MODULE_DEVICE_TABLE(type, name)
#undef  THIS_MODULE
#define THIS_MODULE ((struct module *)0)

void *lx_emul_modem_shm_vmap(struct page **pages, unsigned int count);

#define phys_to_page(phys) ((struct page *)(phys))
#define vmap(pages, count, flags, prot) lx_emul_modem_shm_vmap(pages, count)

#define cp_shmem_release_rmem shm_ipc_unused_cp_shmem_release_rmem

#endif /* _SHM_IPC_SHIM_H_ */
