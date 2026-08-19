/* tb/firmware/link_ai.ld
 *
 * Linker script for the AI-layer benchmark (FPU_TEST=benchai) only.
 *
 * The AI dense-layer + normalization kernel uses all three soft-float routines
 * (soft_fadd, soft_fmul, soft_fdiv), which pushes .text past the 0x800 cap that
 * link.ld reserves for the IRQ handler + emulator. The AI-layer bench links no
 * IRQ stub or emulator, so that window is unused here and the main-code budget
 * is relaxed from 0x800 to 0x1000 (the start of the results region).
 *
 * 0x0000-0x0FFF  main code (.text), must stay below the results region (asserted)
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

    ASSERT(. <= 0x1000, "firmware .text overflows the results region at 0x1000")

    /* NOTE: no .irq_handler section — benchai links no IRQ stub/emulator. */

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

    ASSERT(. <= 0x4000, "firmware .bss exceeds the 16 KB RAM")

    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
        *(.eh_frame)
    }
}