/* tb/firmware/fpu_bench_dig_main.c
 *
 * Week 2 SW-vs-HW cycle-count benchmark: 5-tap half-precision digital FIR
 * filter. y[n] = sum_k x[n-k] * coef[k], every multiply and accumulate in
 * IEEE-754 binary16. The identical kernel runs twice on the RV32I core:
 *   phase SW : soft_half.h pure-integer soft float (no M ext, no FPU)
 *   phase HW : FPU PCPI custom instructions (fpu_macros.h)
 *
 * Result layout (32-bit words, low 16 bits are fp16), N = DIG_N_SAMPLES:
 *   [0, N)   SW output window
 *   [N, 2N)  HW output window
 *   [2N]     SW checksum (fp16 sum of all outputs)
 *   [2N+1]   HW checksum
 *
 * Cycle markers from fpu_bench.h: 0x1C20 SW start, 0x1C24 SW end, 0x1C28
 * HW start; DONE_MAGIC at 0x1C04 ends the run. Build with FPU_TEST=benchdig
 * (see Makefile / run_cpu_test.sh benchdig).
 */

#include <stdint.h>

#include "fpu_bench.h"
#include "fpu_macros.h"
#include "soft_half.h"

#define DIG_N_TAPS    5u
#define DIG_N_SAMPLES 16u

#define DIG_MAGIC 0x5F505560u

#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu

/* 5-tap low-pass-ish coefficients and a 16-sample normal-range input signal. */
static const uint16_t dcoef[DIG_N_TAPS] = {
    0x3800u, 0x3400u, 0x3000u, 0x2C00u, 0x2800u,   /* 0.5, 0.25, 0.125, 0.0625, 0.03125 */
};
static const uint16_t dxin[DIG_N_SAMPLES] = {
    0x3C00u, 0x3E00u, 0x4000u, 0x3800u,   /* 1.0, 1.5, 2.0, 0.5 */
    0x4000u, 0x3C00u, 0x3800u, 0x3E00u,   /* 2.0, 1.0, 0.5, 1.5 */
    0x4200u, 0x4400u, 0x3C00u, 0x4000u,   /* 3.0, 4.0, 1.0, 2.0 */
    0x3800u, 0x3C00u, 0x3E00u, 0x3400u,   /* 0.5, 1.0, 1.5, 0.25 */
};

volatile uint32_t *const results = (volatile uint32_t *)BENCH_RESULTS_BASE;

/* Single-copy noinline wrappers for the soft-float routines: the fully-inlined
 * soft_half.h bodies would otherwise blow up .text past the 0x800 limit. */
static uint16_t sw_fadd(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fmul(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fadd(uint16_t a, uint16_t b) { return soft_fadd(a, b); }
static uint16_t sw_fmul(uint16_t a, uint16_t b) { return soft_fmul(a, b); }

static uint16_t dig_sw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned n, k;
    for (n = 0; n < DIG_N_SAMPLES; n++) {
        uint16_t y = 0x0000u;
        for (k = 0; k < DIG_N_TAPS; k++) {
            int idx = (int)n - (int)k;
            if (idx >= 0)
                y = sw_fadd(y, sw_fmul(dxin[idx], dcoef[k]));
        }
        out[n] = y;
        acc = sw_fadd(acc, y);
    }
    return acc;
}

static uint16_t dig_hw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned n, k;
    for (n = 0; n < DIG_N_SAMPLES; n++) {
        uint16_t y = 0x0000u;
        for (k = 0; k < DIG_N_TAPS; k++) {
            int idx = (int)n - (int)k;
            if (idx >= 0)
                y = (uint16_t)fadd_half(y, fmul_half(dxin[idx], dcoef[k]));
        }
        out[n] = y;
        acc = (uint16_t)fadd_half(acc, y);
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
    uint16_t out_sw[DIG_N_SAMPLES];
    uint16_t out_hw[DIG_N_SAMPLES];

    *(volatile uint32_t *)BENCH_SW_START_ADDR = BENCH_SW_START_MAGIC;
    results[2 * DIG_N_SAMPLES] = dig_sw(out_sw);
    for (i = 0; i < DIG_N_SAMPLES; i++) results[i] = out_sw[i];
    *(volatile uint32_t *)BENCH_SW_END_ADDR = BENCH_SW_END_MAGIC;

    *(volatile uint32_t *)BENCH_HW_START_ADDR = BENCH_HW_START_MAGIC;
    results[2 * DIG_N_SAMPLES + 1] = dig_hw(out_hw);
    for (i = 0; i < DIG_N_SAMPLES; i++) results[DIG_N_SAMPLES + i] = out_hw[i];

    *(volatile uint32_t *)TEST_MAGIC_ADDR = DIG_MAGIC;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
