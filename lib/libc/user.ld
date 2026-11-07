/*	$e1ruBSD: user.ld,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * User program link script: static ET_EXEC at 0x400000, no PIC.
 * (PIE with unrelocated GOT slots triple-faults; keep -no-pie.)
 */

ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.dynsym*) *(.dynstr*) }
}
