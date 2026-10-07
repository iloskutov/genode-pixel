/*
 * \brief  AArch64 trampoline for booting Genode on Google Pixel 7
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * Position-independent trampoline with ARM64 Linux Image header.
 * The bootloader loads this as a kernel image. The trampoline copies
 * the appended Genode binary to its link address and jumps there.
 *
 * Binary layout (after processing by the pixel_boot_img run plugin):
 *   [0x000 - 0x03F]  ARM64 Image header (64 bytes)
 *   [0x040 - 0x1BF]  Trampoline code
 *   [0x1C0 - 0x1CF]  Metadata (patched by script)
 *   [0x1D0 - 0xFFF]  Padding
 *   [0x1000 - ...]   Genode flat binary
 */

/*
 * SPDX-License-Identifier: MIT
 */

.section .text

/*
 * ARM64 Linux Image header (64 bytes)
 * See: Linux Documentation/arch/arm64/booting.rst
 */
.globl _start
_start:
	b       __stage1                /* code0: executable instruction   */
	.long   0                       /* code1: reserved                 */
	.quad   0                       /* text_offset: load offset (0=default) */
	.quad   0                       /* image_size: patched by script   */
	.quad   0xa                     /* flags: LE, 4K pages             */
	.quad   0                       /* res2                            */
	.quad   0                       /* res3                            */
	.quad   0                       /* res4                            */
	.long   0x644d5241              /* magic: "ARM\x64"                */
	.long   0                       /* res5: PE COFF offset            */


/*
 * Stage 1: relocate stage 2 to safe memory, then jump there
 *
 * On entry from bootloader (ARM64 boot protocol):
 *   x0 = physical address of DTB (device tree blob)
 *   MMU off, D-cache off, I-cache may be on
 *   CPU at EL2 or EL1
 */
__stage1:

	/* Debug: print '+' to Samsung Exynos UART at 0x10a00000 */
	movz    x14, #0x10a0, lsl #16  /* x14 = 0x10a00000 (UART base) */
1:	ldr     w15, [x14, #0x10]      /* read UTRSTAT register */
	tbz     w15, #1, 1b            /* wait until TX buffer empty */
	mov     w15, #'+'
	str     w15, [x14, #0x20]      /* write to UTXH register */

	/*
	 * Copy stage 2 code to safe high address (0xC0000000).
	 * This avoids self-overwrite if bootloader loaded us at 0x80000000.
	 */
	movz    x10, #0xC000, lsl #16  /* x10 = 0xC0000000 (relocation target) */
	adr     x11, __stage2
	adr     x12, __stage2_end
1:	cmp     x11, x12
	b.ge    2f
	ldr     x13, [x11], #8
	str     x13, [x10], #8
	b       1b
2:

	/* Prepare arguments for stage 2 */
	adr     x0, _genode_bin         /* x0 = source address of Genode binary */
	adr     x4, _genode_size
	ldr     x1, [x4]               /* x1 = size of Genode binary */
	adr     x4, _entry_point
	ldr     x4, [x4]               /* x4 = entry point address */
	movz    x2, #0x8000, lsl #16   /* x2 = 0x80000000 (destination) */

	/* Invalidate instruction cache and synchronize */
	ic      iallu
	dsb     sy
	isb

	/* Jump to relocated stage 2 */
	movz    x10, #0xC000, lsl #16  /* x10 = 0xC0000000 */
	br      x10


/*
 * Stage 2: copy Genode binary to link address and jump
 *
 * Runs at 0xC0000000 (relocated). Fully position-independent.
 *   x0 = source address of Genode binary
 *   x1 = size in bytes (8-byte aligned)
 *   x2 = destination (0x80000000)
 *   x4 = entry point address
 *   x14 = UART base (0x10a00000, preserved from stage 1)
 */
.align 3
__stage2:
	/* Copy loop: 8 bytes at a time */
	mov     x3, x1                  /* x3 = remaining bytes */
1:	cbz     x3, 2f
	ldr     x5, [x0], #8
	str     x5, [x2], #8
	sub     x3, x3, #8
	b       1b
2:

	/* Invalidate instruction cache and synchronize */
	ic      iallu
	dsb     sy
	isb

	/* Debug: print '*' to UART (copy complete) */
1:	ldr     w15, [x14, #0x10]
	tbz     w15, #1, 1b
	mov     w15, #'*'
	str     w15, [x14, #0x20]

	/* Jump to Genode entry point */
	br      x4
__stage2_end:


/*
 * Metadata — patched by the run plugin via sentinel replacement
 */
.align 3
_genode_size:
	.quad   0xDEADBEEFCAFEBABE      /* sentinel: replaced with binary size */

_entry_point:
	.quad   0xCAFEBABEDEADBEEF      /* sentinel: replaced with ELF entry point */

.align 12                               /* pad to 4KB page boundary */
_genode_bin:
	/* Genode flat binary is appended here by the run plugin */
