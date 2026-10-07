/* SPDX-License-Identifier: MIT */

ENTRY(_start)
SECTIONS
{
    . = 0x0;
    .text : { *(.text) }
}
