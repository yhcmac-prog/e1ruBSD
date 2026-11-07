/*	$e1ruBSD: kern.ld,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Kernel link script: VMA in the higher half, LMA at physical 1MB so
 * objcopy -O binary produces a flat image stage2 can load at 0x100000.
 */

ENTRY(kern_entry)

DMAP_BASE = 0xFFFFFFFF80000000;

SECTIONS {
    . = 0xFFFFFFFF80100000;

    .text : AT(ADDR(.text) - DMAP_BASE) {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : AT(ADDR(.data) - DMAP_BASE) {
        *(.data .data.*)
    }

    .bss : AT(ADDR(.bss) - DMAP_BASE) {
        *(.bss .bss.*)
        *(COMMON)
    }

    kern_end = .;

    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.dynsym*) *(.dynstr*) }
}
