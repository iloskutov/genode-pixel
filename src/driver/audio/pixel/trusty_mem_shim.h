/*
 * \brief  Compile-time adaptation of the vendor trusty-mem.c (force-included)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * get_mem_attr() derives the memory attributes of a shared page from
 * MAIR_EL1, read with 'mrs'. The register is not accessible at EL0, and
 * Genode's MAIR layout differs from Linux' one anyway. The assembler macro
 * below replaces the instruction within this compilation unit by a constant
 * load of the MAIR_EL1 value that fits the attribute indices of the lx_emul
 * shadow (asm/memory.h):
 *
 *   index 0  MT_NORMAL         0xff  normal memory, write-back cached
 *   index 1  MT_NORMAL_TAGGED  0xff
 *   index 2  MT_NORMAL_NC      0x44  normal memory, non-cacheable
 *   index 4  MT_DEVICE_nGnRnE  0x00
 *   index 5  MT_DEVICE_nGnRE   0x04
 *
 * Every page the driver shares is lx_kit DMA memory mapped cached by Genode
 * and passed with PAGE_KERNEL or PAGE_KERNEL_RO (index 0), so Trusty is told
 * "normal, write-back cached, inner shareable", as under Linux.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__TRUSTY_MEM_SHIM_H_
#define _PIXEL_AUDIO__TRUSTY_MEM_SHIM_H_

/* 0x000004000044ffff */
asm(".macro mrs reg:req, sysreg:req\n"
    "	movz \\reg, #0xffff\n"
    "	movk \\reg, #0x0044, lsl #16\n"
    "	movk \\reg, #0x0400, lsl #32\n"
    ".endm\n");

#endif /* _PIXEL_AUDIO__TRUSTY_MEM_SHIM_H_ */
