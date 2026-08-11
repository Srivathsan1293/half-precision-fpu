/* tb/firmware/link.ld
 *
 * Bare-metal RV32I linker script for PicoRV32 with PROGADDR_RESET=0.
 * The image is a flat RAM image (LMA == VMA); the CPU stack pointer is
 * initialized by the PicoRV32 STACKADDR parameter, so no stack section is
 * needed here.
 *
 * 0x0000-0x07FF  main code (.text), must fit below 0x800 (asserted)
 * 0x0800-0x0FFF  IRQ handler + software emulator (.irq_handler, PROGADDR_IRQ = 0x800)
 * 0x1000-0x1BFF  reserved for test results (0x1000)
 * 0x1C00         test magic / done marker (0x1C00 / 0x1C04)
 * 0x2000         stack top (STACKADDR); the stack grows down, so everything
 *                above 0x2000 is free for .rodata/.data/.bss
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

    ASSERT(. <= 0x800, "firmware .text overflows the reserved results region at 0x800")

    .irq_handler 0x800 : {
        KEEP(*(.irq_handler))
    } > ram

    /* IRQ handler + software emulator share 0x800..0xFFF (below the results
     * region at 0x1000); the emulator .text is renamed into .irq_handler. */
    ASSERT(. <= 0x1000, "IRQ handler + emulator overflow into the results region at 0x1000")

    /* NOTE: lld ignores a bare ". = 0x2000;" after an explicit-address output
     * section (.irq_handler 0x800), so .rodata must carry the explicit address
     * itself. It sits above the stack top (0x2000); the stack grows down so it
     * never collides with these data regions. */
    .rodata 0x2000 : {
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

    /* RAM is 16 KB (0x4000); keep the data regions inside it. */
    ASSERT(. <= 0x4000, "firmware .bss exceeds the 16 KB RAM")

    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
        *(.eh_frame)
    }
}
