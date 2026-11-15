/* SPDX-License-Identifier: MIT */

/*
 * trampoline.o first (header, stage 1, metadata, stage 2), then splash.o,
 * which stage 1 relocates together with stage 2. splash.c has no data, as
 * stage 2 runs at another address than it is linked to.
 */
ENTRY(_start)
SECTIONS
{
    . = 0x0;
    .text : {
        *trampoline.o(.text)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(8);
        __stage2_end = .;
        . = ALIGN(4096);
        _genode_bin = .;
    }
    .data : { *(.data .data.* .bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*) }

    ASSERT(SIZEOF(.data) == 0, "splash.c must not have data")
    ASSERT(__stage2_end - __stage2 <= 0x4000, "stage 2 is larger than 16 KiB")
}
