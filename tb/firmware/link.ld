/* tb/firmware/link.ld
 *
 * Bare-metal RV32I linker script for PicoRV32 with PROGADDR_RESET=0.
 * The image is a flat RAM image (LMA == VMA); the CPU stack pointer is
 * initialized by the PicoRV32 STACKADDR parameter, so no stack section is
 * needed here.
 *
 * 0x0000-0x00FF  code (.text)
 * 0x0100-0x02FF  reserved for test results + done marker (not linked)
 * 0x0300+        .rodata/.data/.bss
 */

ENTRY(_start)

MEMORY {
    ram (rwx) : ORIGIN = 0x00000000, LENGTH = 0x4000
}

SECTIONS {
    .text : {
        KEEP(*(.text._start))
        *(.text .text.*)
    } > ram

    . = 0x300;

    .rodata : {
        *(.rodata .rodata.*)
    } > ram

    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
    } > ram

    .bss (NOLOAD) : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
    } > ram

    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
        *(.eh_frame)
    }
}
