/* tb/firmware/fpu_edge_main.c
 *
 * Stage E edge-case / trap-hunt test. Built with clang -march=rv32im_zhinx
 * (standard Zhinx encodings). Runs every entry of the shared edge_vectors table
 * (fpu_vectors.h) through the matching standard Zhinx instruction:
 *   op 0..3   fadd.h/fsub.h/fmul.h/fdiv.h   -> hardware PCPI path
 *   op 4..16  fmin.h..fclass.h              -> trap to the 0x800 emulator
 *
 * Focus: boundary / trap-prone operands that the basic zhinx sweep omits
 * (sNaN payloads, signed zeros, Inf/NaN FCVT saturation, min/max subnormals,
 * INT_MIN/INT_MAX/UINT32_MAX conversions, every FCLASS category bit). The
 * harness (check_edge, MAGIC_EDGE = 0x5F505545) verifies every result against
 * the same golden model used by check_zhinx.
 *
 * If the core traps (rather than reaching done) this test FAILS, so a passing
 * run also proves none of these encodings/operands trip the CPU.
 */

#include <stdint.h>

#include "fpu_vectors.h"

#define RESULTS_BASE      0x1000u
#define TEST_MAGIC_ADDR   0x1C00u
#define DONE_ADDR         0x1C04u
#define DONE_MAGIC        0xDEADBEEFu
#define TEST_MAGIC_EDGE   0x5F505545u

volatile uint32_t *const results = (volatile uint32_t *)RESULTS_BASE;

#define ZH_BIN(insn)                                                            \
    __asm__ volatile(                                                           \
        "mv x10, %[a]\n\t"                                                      \
        "mv x11, %[b]\n\t"                                                      \
        insn " x12, x10, x11\n\t"                                               \
        "mv %[out], x12\n\t"                                                    \
        : [out] "=r"(out)                                                       \
        : [a] "r"(a), [b] "r"(b)                                                \
        : "x10", "x11", "x12")

#define ZH_UN(insn)                                                             \
    __asm__ volatile(                                                           \
        "mv x10, %[a]\n\t"                                                      \
        insn " x12, x10\n\t"                                                    \
        "mv %[out], x12\n\t"                                                    \
        : [out] "=r"(out)                                                       \
        : [a] "r"(a)                                                            \
        : "x10", "x12")

static uint32_t edge_op(unsigned op, uint32_t a, uint32_t b) {
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
    __asm__ volatile(".word 0x0600000B" ::: "memory");   /* maskirq x0, 0 */
    (void)main();
    for (;;) ;
}

int main(void) {
    unsigned i;
    for (i = 0; i < edge_count; i++)
        results[i] = edge_op(edge_vectors[i].op, edge_vectors[i].a, edge_vectors[i].b);

    *(volatile uint32_t *)TEST_MAGIC_ADDR = TEST_MAGIC_EDGE;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;
    return 0;
}
