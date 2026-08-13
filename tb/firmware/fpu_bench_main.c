/* tb/firmware/fpu_bench_main.c
 *
 * Week 2 SW-vs-HW cycle-count benchmark. Runs an identical 8-tap FIR filter
 * over an identical input window twice on the RV32I PicoRV32 core:
 *
 *   phase SW  : every multiply / accumulate through soft_half.h pure-integer
 *               IEEE-754 binary16 routines (no M extension, no FPU)
 *   phase HW  : every multiply / accumulate through the FPU PCPI custom
 *               instructions (fpu_macros.h)
 *
 * The two result windows are stored to the results region and MUST be
 * bit-identical (the soft model is the golden reference for the hardware).
 * The harness times each phase using the cycle markers below.
 *
 * Cycle markers (in the 0x1C00 mmio mirror, see fpu_bench.h):
 *   0x1C10 SW start   0x1C08 SW end    0x1C14 HW start
 * DONE_MAGIC at 0x1C04 ends the whole run.
 *
 * Result layout (32-bit words, low 16 bits are fp16):
 *   [0, N)              SW FIR output window (N = BENCH_N_SAMPLES)
 *   [N, 2N)             HW FIR output window
 *   [2N]                SW final accumulator
 *   [2N+1]              HW final accumulator
 *
 * Build with FPU_TEST=bench (see Makefile / run_cpu_test.sh bench).
 */

#include <stdint.h>

#include "fpu_bench.h"
#include "fpu_macros.h"
#include "soft_half.h"

#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu

/* Fixed-point-ish half-precision coefficients and samples (normal range). */
static const uint16_t coef[BENCH_N_TAPS] = {
    0x3800u, 0x3400u, 0x3000u, 0x2C00u,   /* 0.5, 0.25, 0.125, 0.0625 */
};

static const uint16_t xwin[BENCH_N_SAMPLES] = {
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u,
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u,
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u,
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u,
};

volatile uint32_t *const results = (volatile uint32_t *)BENCH_RESULTS_BASE;

/* Software FIR. Accumulates xwin[i]*coef[k] into acc and stores each output
 * sample into out[i]. */
static uint16_t fir_sw(uint16_t acc, uint16_t *out) {
    unsigned i, k;
    for (i = 0; i < BENCH_N_SAMPLES; i++) {
        for (k = 0; k < BENCH_N_TAPS; k++) {
            uint16_t p = soft_fmul(xwin[i], coef[k]);
            acc = soft_fadd(acc, p);
        }
        out[i] = acc;
    }
    return acc;
}

/* Hardware FIR. Deliberately structurally identical to fir_sw (same loop
 * nest, same op order) except the FP ops go to the PCPI coprocessor. */
static uint16_t fir_hw(uint16_t acc, uint16_t *out) {
    unsigned i, k;
    for (i = 0; i < BENCH_N_SAMPLES; i++) {
        for (k = 0; k < BENCH_N_TAPS; k++) {
            uint16_t p = (uint16_t)fmul_half(xwin[i], coef[k]);
            acc = (uint16_t)fadd_half(acc, p);
        }
        out[i] = acc;
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
    uint16_t out_sw[BENCH_N_SAMPLES];
    uint16_t out_hw[BENCH_N_SAMPLES];

    /* Phase SW: software soft-float FIR. */
    *(volatile uint32_t *)BENCH_SW_START_ADDR = BENCH_SW_START_MAGIC;
    {
        uint16_t acc = 0x0000u;
        for (i = 0; i < BENCH_N_PASSES; i++)
            acc = fir_sw(acc, out_sw);
        results[2 * BENCH_N_SAMPLES] = acc;             /* SW checksum [2N] */
        for (i = 0; i < BENCH_N_SAMPLES; i++)
            results[i] = out_sw[i];
    }
    *(volatile uint32_t *)BENCH_SW_END_ADDR = BENCH_SW_END_MAGIC;

    /* Phase HW: hardware PCPI FIR. Same op count/order as SW. */
    *(volatile uint32_t *)BENCH_HW_START_ADDR = BENCH_HW_START_MAGIC;
    {
        uint16_t acc = 0x0000u;
        for (i = 0; i < BENCH_N_PASSES; i++)
            acc = fir_hw(acc, out_hw);
        results[2 * BENCH_N_SAMPLES + 1] = acc;         /* HW checksum */
        for (i = 0; i < BENCH_N_SAMPLES; i++)
            results[BENCH_N_SAMPLES + i] = out_hw[i];
    }

    *(volatile uint32_t *)TEST_MAGIC_ADDR = BENCH_MAGIC;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}