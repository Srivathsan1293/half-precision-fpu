/* tb/firmware/fpu_bench_div_main.c
 *
 * Week 2 SW-vs-HW cycle-count benchmark: fp16 vector divide (y[i] = x[i]/d[i]).
 * Software division is the costliest soft-float routine (restore long division,
 * ~150+ cycles/op), so this is the workload where the hardware FDIV block shows
 * its honest, largest advantage. The identical kernel runs twice on the RV32I
 * core:
 *   phase SW : soft_half.h pure-integer soft float (no M ext, no FPU)
 *   phase HW : FPU PCPI custom instructions (fpu_macros.h)
 *
 * Result layout (32-bit words, low 16 bits are fp16), N = DIV_N_SAMPLES:
 *   [0, N)   SW output window (last pass)
 *   [N, 2N)  HW output window (last pass)
 *   [2N]     SW checksum (fp16 sum of all outputs over all passes)
 *   [2N+1]   HW checksum
 *
 * Cycle markers from fpu_bench.h: 0x1C20 SW start, 0x1C24 SW end, 0x1C28
 * HW start; DONE_MAGIC at 0x1C04 ends the run. Build with FPU_TEST=benchdiv
 * (see Makefile / run_cpu_test.sh benchdiv).
 */

#include <stdint.h>

#include "fpu_bench.h"
#include "fpu_macros.h"
#include "soft_half.h"

#define DIV_N_SAMPLES 16u
#define DIV_N_PASSES  4u

#define DIV_MAGIC 0x5F505561u

#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu

/* Normal-range numerators and power-of-two denominators so every quotient is
 * exact and no subnormal/overflow edges are exercised. */
static const uint16_t dx[DIV_N_SAMPLES] = {
    0x4C00u, 0x4D00u, 0x4D80u, 0x5000u,   /* 16, 20, 24, 32 */
    0x5100u, 0x5240u, 0x5400u, 0x4800u,   /* 40, 48, 64, 8  */
    0x4A00u, 0x4C00u, 0x4D00u, 0x4D80u,   /* 12, 16, 20, 24 */
    0x5000u, 0x5100u, 0x5240u, 0x5400u,   /* 32, 40, 48, 64 */
};
static const uint16_t dd[DIV_N_SAMPLES] = {
    0x4400u, 0x4400u, 0x4400u, 0x4800u,   /* 4, 4, 4, 8 */
    0x4800u, 0x4800u, 0x4800u, 0x4400u,   /* 8, 8, 8, 4 */
    0x4400u, 0x4400u, 0x4400u, 0x4400u,   /* 4, 4, 4, 4 */
    0x4800u, 0x4800u, 0x4800u, 0x4800u,   /* 8, 8, 8, 8 */
};

volatile uint32_t *const results = (volatile uint32_t *)BENCH_RESULTS_BASE;

/* Single-copy noinline wrappers for the soft-float routines: the fully-inlined
 * soft_half.h bodies would otherwise blow up .text past the 0x800 limit. */
static uint16_t sw_fdiv(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fadd(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fdiv(uint16_t a, uint16_t b) { return soft_fdiv(a, b); }
static uint16_t sw_fadd(uint16_t a, uint16_t b) { return soft_fadd(a, b); }

static uint16_t div_sw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned i, p;
    for (p = 0; p < DIV_N_PASSES; p++) {
        for (i = 0; i < DIV_N_SAMPLES; i++) {
            uint16_t y = sw_fdiv(dx[i], dd[i]);
            out[i] = y;
            acc = sw_fadd(acc, y);
        }
    }
    return acc;
}

static uint16_t div_hw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned i, p;
    for (p = 0; p < DIV_N_PASSES; p++) {
        for (i = 0; i < DIV_N_SAMPLES; i++) {
            uint16_t y = (uint16_t)fdiv_half(dx[i], dd[i]);
            out[i] = y;
            acc = (uint16_t)fadd_half(acc, y);
        }
    }
    return acc;
}

void _start(void) __attribute__((section(".text._start"), noreturn));
int main(void);
void _start(void) {
    (void)main();
    for (;;) ;
}

int main(void) {
    unsigned i;
    uint16_t out_sw[DIV_N_SAMPLES];
    uint16_t out_hw[DIV_N_SAMPLES];

    *(volatile uint32_t *)BENCH_SW_START_ADDR = BENCH_SW_START_MAGIC;
    results[2 * DIV_N_SAMPLES] = div_sw(out_sw);
    for (i = 0; i < DIV_N_SAMPLES; i++) results[i] = out_sw[i];
    *(volatile uint32_t *)BENCH_SW_END_ADDR = BENCH_SW_END_MAGIC;

    *(volatile uint32_t *)BENCH_HW_START_ADDR = BENCH_HW_START_MAGIC;
    results[2 * DIV_N_SAMPLES + 1] = div_hw(out_hw);
    for (i = 0; i < DIV_N_SAMPLES; i++) results[DIV_N_SAMPLES + i] = out_hw[i];

    *(volatile uint32_t *)TEST_MAGIC_ADDR = DIV_MAGIC;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
