/* tb/firmware/fpu_zhinx_main.c
 *
 * Phase 5: standard-Zhinx integration test built with clang -march=rv32im_zhinx
 * (real toolchain encoding, no hand-rolled .word values for the FP ops).
 *
 * Every entry of the shared zhinx_vectors table (fpu_vectors.h) is executed with
 * the matching standard Zhinx instruction via inline asm:
 *   op 0..3   fadd.h/fsub.h/fmul.h/fdiv.h   -> hardware PCPI path
 *   op 4..16  fmin.h..fclass.h              -> trap to the 0x800 emulator
 *
 * Operands are full 32-bit words moved verbatim into rs1/rs2 (the hardware and
 * emulator only look at the low 16 bits except FCVT.H.W/WU which use the full
 * value). Results are stored as full 32-bit words at 0x1000+, then magic
 * 0x5F50555D @ 0x1C00 and done @ 0x1C04.
 */

#include <stdint.h>

#include "fpu_vectors.h"

#define RESULTS_BASE      0x1000u
#define TEST_MAGIC_ADDR   0x1C00u
#define DONE_ADDR         0x1C04u
#define DONE_MAGIC        0xDEADBEEFu
#define TEST_MAGIC_ZHINX  0x5F50555Du

volatile uint32_t *const results = (volatile uint32_t *)RESULTS_BASE;

/* binary FP op: a -> x10, b -> x11, result -> x12, then out. */
#define ZH_BIN(insn)                                                            \
    __asm__ volatile(                                                           \
        "mv x10, %[a]\n\t"                                                      \
        "mv x11, %[b]\n\t"                                                      \
        insn " x12, x10, x11\n\t"                                               \
        "mv %[out], x12\n\t"                                                    \
        : [out] "=r"(out)                                                       \
        : [a] "r"(a), [b] "r"(b)                                                \
        : "x10", "x11", "x12")

/* unary FP op: a -> x10, result -> x12, then out. */
#define ZH_UN(insn)                                                             \
    __asm__ volatile(                                                           \
        "mv x10, %[a]\n\t"                                                      \
        insn " x12, x10\n\t"                                                    \
        "mv %[out], x12\n\t"                                                    \
        : [out] "=r"(out)                                                       \
        : [a] "r"(a)                                                            \
        : "x10", "x12")

static uint32_t zhinx_op(unsigned op, uint32_t a, uint32_t b) {
    uint32_t out;
    switch (op) {
    case 0:  ZH_BIN("fadd.h");   break;
    case 1:  ZH_BIN("fsub.h");   break;
    case 2:  ZH_BIN("fmul.h");   break;
    case 3:  ZH_BIN("fdiv.h");   break;
    case 4:  ZH_BIN("fmin.h");   break;
    case 5:  ZH_BIN("fmax.h");   break;
    case 6:  ZH_BIN("feq.h");    break;
    case 7:  ZH_BIN("flt.h");    break;
    case 8:  ZH_BIN("fle.h");    break;
    case 9:  ZH_UN("fcvt.h.w");  break;
    case 10: ZH_UN("fcvt.h.wu"); break;
    case 11: ZH_UN("fcvt.w.h");  break;
    case 12: ZH_UN("fcvt.wu.h"); break;
    case 13: ZH_BIN("fsgnj.h");  break;
    case 14: ZH_BIN("fsgnjn.h"); break;
    case 15: ZH_BIN("fsgnjx.h"); break;
    default: ZH_UN("fclass.h");  break;
    }
    return out;
}

void _start(void) __attribute__((section(".text._start"), noreturn));
int main(void);
void _start(void) {
    // maskirq x0, 0: unmask the ebreak IRQ (PicoRV32 resets irq_mask = ~0),
    // required for the emulated ops (4..16) to trap to the 0x800 handler.
    __asm__ volatile(".word 0x0600000B" ::: "memory");
    (void)main();
    for (;;) ;
}

int main(void) {
    unsigned i;
    for (i = 0; i < zhinx_count; i++)
        results[i] = zhinx_op(zhinx_vectors[i].op, zhinx_vectors[i].a, zhinx_vectors[i].b);

    *(volatile uint32_t *)TEST_MAGIC_ADDR = TEST_MAGIC_ZHINX;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;
    return 0;
}
