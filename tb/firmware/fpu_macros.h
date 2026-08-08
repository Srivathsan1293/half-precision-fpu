/* tb/firmware/fpu_macros.h
 *
 * Inline-assembly wrappers for the half-precision FPU custom instructions.
 * These target the PicoRV32 PCPI interface: the CPU presents any custom0
 * (opcode 0x0b) instruction that it does not understand to the coprocessor.
 *
 * Encoding (R-type, funct3 = 000):
 *     | funct7      | rs2 | rs1 | funct3 | rd | opcode    |
 *     | 0000110..09 | reg | reg | 000    | reg| 0001011   |
 *
 *     funct7 0000110 = FADD    0000111 = FSUB
 *            0001000 = FMUL    0001001 = FDIV
 *
 * Operands are half-precision (binary16) bit patterns held in the low 16
 * bits of rs1/rs2; the 16-bit result is zero-extended into rd.
 *
 * The wrapper functions copy the operands into x10/x11, execute the custom
 * instruction (result into x1), and copy it back, declaring the registers as
 * clobbered so the compiler never relies on their values across the op.
 */

#ifndef FPU_MACROS_H
#define FPU_MACROS_H

#define FPU_ASM(f7, a, b, out)                                                        \
    __asm__ volatile(                                                                 \
        "mv x10, %[a]\n\t"                                                            \
        "mv x11, %[b]\n\t"                                                            \
        ".word %[insn]\n\t"                                                           \
        "mv %[out], x1\n\t"                                                           \
        : [out] "=r"(out)                                                             \
        : [a] "r"(a), [b] "r"(b),                                                     \
          [insn] "i"(((f7) << 25) | (11 << 20) | (10 << 15) | (0 << 12) | (1 << 7) | 0x0b) \
        : "x10", "x11", "x1")

static inline uint32_t fadd_half(uint32_t a, uint32_t b) {
    uint32_t out;
    FPU_ASM(0x06, a, b, out);
    return out;
}

static inline uint32_t fsub_half(uint32_t a, uint32_t b) {
    uint32_t out;
    FPU_ASM(0x07, a, b, out);
    return out;
}

static inline uint32_t fmul_half(uint32_t a, uint32_t b) {
    uint32_t out;
    FPU_ASM(0x08, a, b, out);
    return out;
}

static inline uint32_t fdiv_half(uint32_t a, uint32_t b) {
    uint32_t out;
    FPU_ASM(0x09, a, b, out);
    return out;
}

#endif /* FPU_MACROS_H */
