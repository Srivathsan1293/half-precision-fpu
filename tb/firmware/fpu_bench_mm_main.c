/* tb/firmware/fpu_bench_mm_main.c
 *
 * Week 2 SW-vs-HW cycle-count benchmark: 4x4 half-precision matrix multiply.
 * C[i][j] = sum_k A[i][k] * B[k][j], every multiply and accumulate in
 * IEEE-754 binary16. The identical kernel runs twice on the RV32I core:
 *   phase SW : soft_half.h pure-integer soft float (no M ext, no FPU)
 *   phase HW : FPU PCPI custom instructions (fpu_macros.h)
 *
 * Result layout (32-bit words, low 16 bits are fp16), N = MM_DIM*MM_DIM:
 *   [0, N)   SW C matrix (row-major)
 *   [N, 2N)  HW C matrix (row-major)
 *   [2N]     SW checksum (fp16 sum of all C elements)
 *   [2N+1]   HW checksum
 *
 * Cycle markers from fpu_bench.h: 0x1C20 SW start, 0x1C24 SW end, 0x1C28
 * HW start; DONE_MAGIC at 0x1C04 ends the run. Build with FPU_TEST=benchmm
 * (see Makefile / run_cpu_test.sh benchmm).
 */

#include <stdint.h>

#include "fpu_bench.h"
#include "fpu_macros.h"
#include "soft_half.h"

#define MM_DIM 4u
#define MM_N   (MM_DIM * MM_DIM)

#define MM_MAGIC 0x5F50555Fu

#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu

/* A[i][j] = fp16(i+1), B[k][j] = fp16(j+1): normal-range integers so every
 * dot product stays exact and subnormal/overflow edges are not exercised. */
static const uint16_t matA[MM_N] = {
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u,   /* 1 1 1 1 */
    0x4000u, 0x4000u, 0x4000u, 0x4000u,   /* 2 2 2 2 */
    0x4200u, 0x4200u, 0x4200u, 0x4200u,   /* 3 3 3 3 */
    0x4400u, 0x4400u, 0x4400u, 0x4400u,   /* 4 4 4 4 */
};
static const uint16_t matB[MM_N] = {
    0x3C00u, 0x4000u, 0x4200u, 0x4400u,   /* 1 2 3 4 */
    0x3C00u, 0x4000u, 0x4200u, 0x4400u,
    0x3C00u, 0x4000u, 0x4200u, 0x4400u,
    0x3C00u, 0x4000u, 0x4200u, 0x4400u,
};

volatile uint32_t *const results = (volatile uint32_t *)BENCH_RESULTS_BASE;

/* Single-copy noinline wrappers for the soft-float routines: the fully-inlined
 * soft_half.h bodies would otherwise blow up .text past the 0x800 limit. */
static uint16_t sw_fadd(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fmul(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fadd(uint16_t a, uint16_t b) { return soft_fadd(a, b); }
static uint16_t sw_fmul(uint16_t a, uint16_t b) { return soft_fmul(a, b); }

static uint16_t mm_sw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned i, j, k;
    for (i = 0; i < MM_DIM; i++) {
        for (j = 0; j < MM_DIM; j++) {
            uint16_t s = 0x0000u;
            for (k = 0; k < MM_DIM; k++)
                s = sw_fadd(s, sw_fmul(matA[i * MM_DIM + k], matB[k * MM_DIM + j]));
            out[i * MM_DIM + j] = s;
            acc = sw_fadd(acc, s);
        }
    }
    return acc;
}

static uint16_t mm_hw(uint16_t *out) {
    uint16_t acc = 0x0000u;
    unsigned i, j, k;
    for (i = 0; i < MM_DIM; i++) {
        for (j = 0; j < MM_DIM; j++) {
            uint16_t s = 0x0000u;
            for (k = 0; k < MM_DIM; k++)
                s = (uint16_t)fadd_half(s, fmul_half(matA[i * MM_DIM + k], matB[k * MM_DIM + j]));
            out[i * MM_DIM + j] = s;
            acc = (uint16_t)fadd_half(acc, s);
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
    uint16_t out_sw[MM_N];
    uint16_t out_hw[MM_N];

    *(volatile uint32_t *)BENCH_SW_START_ADDR = BENCH_SW_START_MAGIC;
    results[2 * MM_N] = mm_sw(out_sw);
    for (i = 0; i < MM_N; i++) results[i] = out_sw[i];
    *(volatile uint32_t *)BENCH_SW_END_ADDR = BENCH_SW_END_MAGIC;

    *(volatile uint32_t *)BENCH_HW_START_ADDR = BENCH_HW_START_MAGIC;
    results[2 * MM_N + 1] = mm_hw(out_hw);
    for (i = 0; i < MM_N; i++) results[MM_N + i] = out_hw[i];

    *(volatile uint32_t *)TEST_MAGIC_ADDR = MM_MAGIC;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
