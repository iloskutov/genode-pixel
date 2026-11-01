/*
 * \brief  Shadow copy of asm/memory.h
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * dde_linux's shadow header for arm64 leaves 'virt_to_phys' and '__va'
 * unimplemented. io-pgtable-arm needs both for the page tables of the GPU's
 * MMU: the physical address of a table goes into the parent entry and into
 * the translation-table base register, and a walk turns it back into a
 * pointer. Kernel memory of this driver is DMA memory with a known physical
 * address (lx_kit/memory_dma.cc), so both directions can be answered.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#pragma once

#include_next <asm/memory.h>

#ifndef __ASSEMBLY__

#undef  virt_to_phys
#define virt_to_phys(v) ((phys_addr_t)lx_emul_mem_dma_addr((void *)(v)))

#undef  __va
#define __va(x) ((void *)lx_emul_mem_virt_addr((void *)(unsigned long)(x)))

#endif /* __ASSEMBLY__ */
