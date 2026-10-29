/*
 * \brief  Compile-time adaptation of the vendor trusty.c (force-included)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * trusty.c runs unmodified. Two things of it cannot work in a Genode
 * component and are replaced here:
 *
 * - It masks all exceptions around each standard call with 'msr daif'
 *   (asm/daifflags.h), which is not available at EL0. Under lx_emul the
 *   calling task is never preempted by an interrupt handler (interrupts are
 *   signals dispatched when the task schedules), so both helpers are empty.
 *
 * - 'phys_to_page' is not provided by the lx_emul memory shadow (arm64's
 *   definition ends in __va(), which stops the driver). The page of a
 *   physical address is looked up in the lx_kit DMA-buffer registry.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__TRUSTY_SHIM_H_
#define _PIXEL_AUDIO__TRUSTY_SHIM_H_

#include <linux/types.h>

/* keep asm/daifflags.h out */
#define __ASM_DAIFFLAGS_H

#define DAIF_PROCCTX 0

static inline void local_daif_mask(void) { }
static inline void local_daif_restore(unsigned long flags) { }

struct page;

struct page *pixel_audio_phys_to_page(phys_addr_t phys);

#define phys_to_page(phys) pixel_audio_phys_to_page(phys)

#endif /* _PIXEL_AUDIO__TRUSTY_SHIM_H_ */
