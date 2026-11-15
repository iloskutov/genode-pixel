/*
 * \brief  AArch64 trampoline for booting Genode on Google Pixel 7
 * \author Ivan Loskutov
 * \date   2026-02-06
 *
 * Position-independent trampoline with ARM64 Linux Image header.
 * The bootloader loads this as a kernel image. The trampoline shows the
 * boot splash (splash.c), copies the appended Genode binary to its link
 * address, and jumps there.
 *
 * The boot loader enters with the MMU and the data cache off, where every
 * load and store goes to the DRAM on its own. For the splash and the copy,
 * the trampoline maps the source, the destination, the splash image, and
 * itself 1:1 as cached memory in 2-MiB blocks, the framebuffer as normal
 * non-cacheable memory, and the UART and the display registers as device
 * memory, and switches the MMU on at its exception level. After the copy,
 * it writes the cache lines of the cached ranges back and drops them,
 * switches the MMU off, and drops them again, as the CPU may have fetched
 * lines ahead. Bootstrap then finds the image in DRAM and no cache line
 * that it could not see with the MMU off. An image beyond 4 GiB is copied
 * with the MMU off, 8 bytes per step, and without splash.
 *
 * Binary layout (after processing by the pixel_boot_img run plugin):
 *   [0x000 - 0x03F]  ARM64 Image header (64 bytes)
 *   [0x040 - ...  ]  Trampoline code and metadata (patched by script),
 *                    stage 2 including splash.c (trampoline.ld)
 *   [...   - ...  ]  Padding to 4 KiB
 *   [_genode_bin  ]  Genode flat binary, a multiple of 64 bytes
 */

/*
 * SPDX-License-Identifier: MIT
 */

STAGE2      = 0xc0000000   /* stage 2 (at most 16 KiB, see trampoline.ld) */
STACK       = 0xc0008000   /* top of the stack of stage 2 */
TABLES      = 0xc0008000   /* level 1, then 4 level-2 tables of 1 GiB each */
STAGE2_END  = 0xc000d000
BLOCK_MASK  = 0x1fffff     /* 2 MiB */

ATTR_NORMAL = 0x705        /* AF, inner shareable, MAIR index 1, block */
ATTR_NC     = 0x709        /* AF, inner shareable, MAIR index 2, block */
ATTR_DEVICE = 0x401        /* AF, MAIR index 0, block */

/* MAIR index 0: device nGnRnE, 1: normal write-back, 2: normal non-cacheable */
MAIR_LO     = 0xff00
MAIR_HI     = 0x44

/*
 * TCR, valid at EL1 and EL2: T0SZ 32 (4 GiB, walks start at level 1),
 * inner/outer write-back walks, inner shareable, 4-KiB granule, 32-bit
 * physical addresses; bit 23 is EPD1 at EL1 and RES1 at EL2, bit 31 is
 * TG1=4K at EL1 and RES1 at EL2
 */
TCR_LO      = 0x3520
TCR_HI      = 0x8080

SCTLR_ON    = 0x1005       /* M, C, I */
SCTLR_OFF   = 0x5          /* M, C */

/* splash.c */
FB_BASE     = 0xfac00000
FB_SIZE     = 0xa00000
DPP_BASE    = 0x1c0b0000
DECON_BASE  = 0x1c240000
SPLASH_IMAGE = 0           /* offsets in 'struct Splash' */
SPLASH_SIZE  = 8


/* load a 32-bit constant */
.macro mov32 reg, value
	movz    \reg, #((\value) >> 16), lsl #16
	movk    \reg, #((\value) & 0xffff)
.endm

/* write w15 to the UART at x21, uses w20 */
.macro uart_base reg
	movz    \reg, #0x10a0, lsl #16
.endm
.macro putc
.Lputc\@:
	ldr     w20, [x21, #0x10]      /* UTRSTAT */
	tbz     w20, #1, .Lputc\@      /* TX buffer empty */
	str     w15, [x21, #0x20]      /* UTXH */
.endm


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
	mov     x24, x0                 /* x24 = device tree */

	uart_base x21
	mov     w15, #'+'
	putc

	/*
	 * Copy stage 2 code to safe high address (0xC0000000).
	 * This avoids self-overwrite if bootloader loaded us at 0x80000000.
	 */
	mov32   x10, STAGE2
	adr     x11, __stage2
	adr     x12, __stage2_end
1:	cmp     x11, x12
	b.ge    2f
	ldr     x13, [x11], #8
	str     x13, [x10], #8
	b       1b
2:

	/* Prepare arguments for stage 2 */
	adr     x23, _genode_bin        /* x23 = source address of Genode binary */
	ldr     x26, _genode_size       /* x26 = size of Genode binary */
	ldr     x25, _genode_dest       /* x25 = link address of Genode binary */
	ldr     x22, _entry_point       /* x22 = entry point address */

	/* Invalidate instruction cache and synchronize */
	ic      iallu
	dsb     sy
	isb

	/* Jump to relocated stage 2 */
	mov32   x10, STAGE2
	br      x10


/*
 * Metadata — patched by the run plugin via sentinel replacement
 */
.align 3
_genode_size:
	.quad   0xDEADBEEFCAFEBABE      /* sentinel: replaced with binary size */

_entry_point:
	.quad   0xCAFEBABEDEADBEEF      /* sentinel: replaced with ELF entry point */

_genode_dest:
	.quad   0xD0D0CACAD0D0CACA      /* sentinel: replaced with link address */


/*
 * Stage 2: show the splash, copy Genode binary to link address and jump
 *
 * Runs at 0xC0000000 (relocated), together with splash.c. Fully
 * position-independent. The registers survive the calls of splash.c:
 *   x21 = UART base
 *   x22 = entry point address
 *   x23 = source address of Genode binary
 *   x24 = device tree
 *   x25 = destination (link address)
 *   x26 = size in bytes (multiple of 64)
 *   x27 = exception level, x28 = size of a data-cache line
 *   sp  = struct Splash of splash.c
 */
.align 3
.globl __stage2
__stage2:
	mov32   x5, STACK
	sub     sp, x5, #32

	/* the 1:1 map covers 4 GiB */
	add     x5, x23, x26
	add     x6, x25, x26
	orr     x5, x5, x6
	lsr     x5, x5, #32
	cbnz    x5, __copy_uncached

	/* x28 = size of a data-cache line */
	mrs     x5, ctr_el0
	ubfx    x5, x5, #16, #4
	mov     x28, #4
	lsl     x28, x28, x5

	/* x27 = exception level */
	mrs     x27, CurrentEL
	lsr     x27, x27, #2
	cmp     x27, #2
	b.ne    1f

	/*
	 * The firmware may enter with the virtualization host extensions
	 * (HCR_EL2.E2H), which change the EL2 registers. Keep only RW.
	 */
	mov     x5, #(1 << 31)
	msr     hcr_el2, x5
	isb
1:

	/* the splash image and the display state, MMU off */
	mov     x0, x24
	mov     x1, sp
	bl      splash_find

	/* zero the tables */
	mov32   x5, TABLES
	add     x6, x5, #0x5000
1:	stp     xzr, xzr, [x5], #16
	cmp     x5, x6
	b.lo    1b

	/* level 1: four tables of level 2, one per GiB, contiguous */
	mov32   x7, TABLES
	add     x5, x7, #0x1000
	orr     x5, x5, #3
	str     x5, [x7]
	add     x5, x5, #0x1000
	str     x5, [x7, #8]
	add     x5, x5, #0x1000
	str     x5, [x7, #16]
	add     x5, x5, #0x1000
	str     x5, [x7, #24]

	/* blocks of the source, the destination, and stage 2 */
	mov     x18, #ATTR_NORMAL
	mov     x16, x23
	add     x17, x23, x26
	bl      __map
	mov     x16, x25
	add     x17, x25, x26
	bl      __map
	mov32   x16, STAGE2
	mov32   x17, STAGE2_END
	bl      __map

	/* device registers, never executable (bit 54, XN at EL2, UXN at EL1) */
	mov     x18, #ATTR_DEVICE
	movk    x18, #0x0040, lsl #48
	mov     x16, x21
	add     x17, x21, #0x1000
	bl      __map

	/* the splash image, the framebuffer, and the display registers */
	ldr     x16, [sp, #SPLASH_IMAGE]
	cbz     x16, 1f
	mov32   x16, DPP_BASE
	add     x17, x16, #0x1000
	bl      __map
	mov32   x16, DECON_BASE
	add     x17, x16, #0x1000
	bl      __map
	mov     x18, #ATTR_NC
	mov32   x16, FB_BASE
	mov32   x17, FB_BASE + FB_SIZE
	bl      __map
	mov     x18, #ATTR_NORMAL
	ldr     x16, [sp, #SPLASH_IMAGE]
	ldr     x17, [sp, #SPLASH_SIZE]
	add     x17, x16, x17
	bl      __map
1:

	/* drop lines of the tables that the walks could hit */
	dsb     sy
	mov     x18, #1
	mov32   x16, STAGE2
	mov32   x17, STAGE2_END
	bl      __dc_range
	dsb     sy

	/* MMU and caches on */
	movz    x5, #MAIR_LO
	movk    x5, #MAIR_HI, lsl #16
	movz    x6, #TCR_LO
	movk    x6, #TCR_HI, lsl #16
	mov32   x7, TABLES
	mov     x8, #SCTLR_ON
	cmp     x27, #2
	b.ne    1f
	msr     mair_el2, x5
	msr     tcr_el2, x6
	msr     ttbr0_el2, x7
	isb
	tlbi    alle2
	dsb     sy
	isb
	mrs     x9, sctlr_el2
	orr     x9, x9, x8
	bic     x9, x9, #(1 << 1)       /* A   */
	bic     x9, x9, #(1 << 19)      /* WXN */
	msr     sctlr_el2, x9
	isb
	b       2f
1:	msr     mair_el1, x5
	msr     tcr_el1, x6
	msr     ttbr0_el1, x7
	isb
	tlbi    vmalle1
	dsb     sy
	isb
	mrs     x9, sctlr_el1
	orr     x9, x9, x8
	bic     x9, x9, #(1 << 1)       /* A   */
	bic     x9, x9, #(1 << 19)      /* WXN */
	msr     sctlr_el1, x9
	isb
2:

	/* the splash first */
	ldr     x16, [sp, #SPLASH_IMAGE]
	cbz     x16, 1f
	mov     x0, sp
	bl      splash_draw
1:

	/* copy, 64 bytes per step */
	mov     x0, x23
	mov     x2, x25
	mov     x3, x26
1:	ldp     x5,  x6,  [x0]
	ldp     x7,  x8,  [x0, #16]
	ldp     x9,  x10, [x0, #32]
	ldp     x11, x12, [x0, #48]
	add     x0, x0, #64
	stp     x5,  x6,  [x2]
	stp     x7,  x8,  [x2, #16]
	stp     x9,  x10, [x2, #32]
	stp     x11, x12, [x2, #48]
	add     x2, x2, #64
	subs    x3, x3, #64
	b.hi    1b

	/* write back and drop the lines of all cached blocks */
	mov     x18, #0
	bl      __dc_blocks
	dsb     sy

	/* MMU and data cache off */
	mov     x8, #SCTLR_OFF
	cmp     x27, #2
	b.ne    1f
	mrs     x9, sctlr_el2
	bic     x9, x9, x8
	msr     sctlr_el2, x9
	isb
	tlbi    alle2
	b       2f
1:	mrs     x9, sctlr_el1
	bic     x9, x9, x8
	msr     sctlr_el1, x9
	isb
	tlbi    vmalle1
2:	dsb     sy
	isb

	/* drop the lines that the CPU fetched ahead before the MMU went off */
	mov     x18, #1
	bl      __dc_blocks
	dsb     sy
	b       __done

__copy_uncached:
	mov     x0, x23
	mov     x2, x25
	mov     x3, x26
1:	cbz     x3, 2f
	ldr     x5, [x0], #8
	str     x5, [x2], #8
	sub     x3, x3, #8
	b       1b
2:

__done:
	/* Invalidate instruction cache and synchronize */
	ic      iallu
	dsb     sy
	isb

	/* Debug: print '*' to UART (copy complete) */
	mov     w15, #'*'
	putc

	/* Jump to Genode entry point */
	br      x22


/*
 * Enter blocks of [x16, x17) with attributes x18 into the level-2 tables,
 * uses x19, x20
 */
__map:
	lsr     x16, x16, #21
	sub     x17, x17, #1
	lsr     x17, x17, #21
	mov32   x19, TABLES
	add     x19, x19, #0x1000
1:	lsl     x20, x16, #21
	orr     x20, x20, x18
	str     x20, [x19, x16, lsl #3]
	add     x16, x16, #1
	cmp     x16, x17
	b.ls    1b
	ret


/*
 * Clean and invalidate (x18 = 0) or invalidate (x18 = 1) the data-cache
 * lines of the 2-MiB blocks that cover [x16, x17), line size x28,
 * uses x19
 */
__dc_range:
	bic     x16, x16, #BLOCK_MASK
	mov     x19, #BLOCK_MASK
	add     x17, x17, x19
	bic     x17, x17, #BLOCK_MASK
1:	cbnz    x18, 2f
	dc      civac, x16
	b       3f
2:	dc      ivac, x16
3:	add     x16, x16, x28
	cmp     x16, x17
	b.lo    1b
	ret


/*
 * '__dc_range' for the cached blocks: the source, the destination,
 * stage 2, and the splash image, uses x16, x17, x19, x29
 */
__dc_blocks:
	mov     x29, x30
	mov     x16, x23
	add     x17, x23, x26
	bl      __dc_range
	mov     x16, x25
	add     x17, x25, x26
	bl      __dc_range
	mov32   x16, STAGE2
	mov32   x17, STAGE2_END
	bl      __dc_range
	ldr     x16, [sp, #SPLASH_IMAGE]
	cbz     x16, 1f
	ldr     x17, [sp, #SPLASH_SIZE]
	add     x17, x16, x17
	bl      __dc_range
1:	mov     x30, x29
	ret

	/* splash.c follows, then '__stage2_end' and '_genode_bin' (trampoline.ld) */
