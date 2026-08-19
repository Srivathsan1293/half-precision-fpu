/* tb/firmware/fpu_bench_ai_layer_main.c
 *
 * Week 3 SW-vs-HW cycle-count benchmark: Micro AI Dense Layer + Normalization.
 * Computes Y = W * X + B, followed by Y_norm = Y / sum(Y).
 * This mimics the core math of a neural network fully-connected layer
 * and a Softmax/Attention normalization step, exercising FADD, FMUL, and FDIV.
 *
 * Result layout (32-bit words, low 16 bits are fp16), N = OUT_DIM:
 *   [0, N)   SW output vector
 *   [N, 2N)  HW output vector
 *   [2N]     SW checksum (fp16 sum of all elements)
 *   [2N+1]   HW checksum
 *
 * Cycle markers from fpu_bench.h: 0x1C20 SW start, 0x1C24 SW end, 0x1C28
 * HW start; DONE_MAGIC at 0x1C04 ends the run.[cite: 8]
 */

#include <stdint.h>

#include "fpu_bench.h"
#include "fpu_macros.h"
#include "soft_half.h"

#define IN_DIM  8u
#define OUT_DIM 4u

#define AI_MAGIC 0x41494E4Eu /* "AINN" */

#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu

/* Toy weights and activations in fp16 (normal-range integers) */
static const uint16_t vecX[IN_DIM] = {
    0x3C00u, 0x4000u, 0x4200u, 0x4400u, /* 1, 2, 3, 4 */
    0x3C00u, 0x4000u, 0x4200u, 0x4400u  /* 1, 2, 3, 4 */
};

static const uint16_t matW[OUT_DIM * IN_DIM] = {
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u, /* Row 0: all 1s */
    0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u, /* Row 1: all 2s */
    0x4200u, 0x4200u, 0x4200u, 0x4200u, 0x4200u, 0x4200u, 0x4200u, 0x4200u, /* Row 2: all 3s */
    0x3800u, 0x3800u, 0x3800u, 0x3800u, 0x3800u, 0x3800u, 0x3800u, 0x3800u  /* Row 3: all 0.5s */
};

static const uint16_t vecB[OUT_DIM] = {
    0x3C00u, 0x3C00u, 0x3C00u, 0x3C00u /* Biases: 1, 1, 1, 1 */
};

volatile uint32_t *const results = (volatile uint32_t *)BENCH_RESULTS_BASE;

/* Single-copy noinline wrappers for the soft-float routines[cite: 8] */
static uint16_t sw_fadd(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fmul(uint16_t a, uint16_t b) __attribute__((noinline));
static uint16_t sw_fdiv(uint16_t a, uint16_t b) __attribute__((noinline));

static uint16_t sw_fadd(uint16_t a, uint16_t b) { return soft_fadd(a, b); }
static uint16_t sw_fmul(uint16_t a, uint16_t b) { return soft_fmul(a, b); }
static uint16_t sw_fdiv(uint16_t a, uint16_t b) { return soft_fdiv(a, b); }

static uint16_t ai_sw(uint16_t *out) {
    uint16_t sum = 0x0000u; /* fp16 0.0 */
    uint16_t chk = 0x0000u;
    unsigned i, j;

    /* Step 1: Dense Layer (Vector-Matrix Multiply + Bias) */
    for (i = 0; i < OUT_DIM; i++) {
        uint16_t acc = vecB[i];
        for (j = 0; j < IN_DIM; j++) {
            acc = sw_fadd(acc, sw_fmul(matW[i * IN_DIM + j], vecX[j]));
        }
        out[i] = acc;
        sum = sw_fadd(sum, acc);
    }

    /* Step 2: Normalization (Divides each element by the total sum) */
    for (i = 0; i < OUT_DIM; i++) {
        out[i] = sw_fdiv(out[i], sum);
        chk = sw_fadd(chk, out[i]);
    }
    return chk;
}

static uint16_t ai_hw(uint16_t *out) {
    uint16_t sum = 0x0000u; /* fp16 0.0 */
    uint16_t chk = 0x0000u;
    unsigned i, j;

    /* Step 1: Dense Layer (Vector-Matrix Multiply + Bias) using HW FPU */
    for (i = 0; i < OUT_DIM; i++) {
        uint16_t acc = vecB[i];
        for (j = 0; j < IN_DIM; j++) {
            acc = (uint16_t)fadd_half(acc, fmul_half(matW[i * IN_DIM + j], vecX[j]));
        }
        out[i] = acc;
        sum = (uint16_t)fadd_half(sum, acc);
    }

    /* Step 2: Normalization using HW FPU Division */
    for (i = 0; i < OUT_DIM; i++) {
        out[i] = (uint16_t)fdiv_half(out[i], sum);
        chk = (uint16_t)fadd_half(chk, out[i]);
    }
    return chk;
}

void _start(void) __attribute__((section(".text._start"), noreturn));
int main(void);
void _start(void) {
    (void)main();
    for (;;) ;
}

int main(void) {
    unsigned i;
    uint16_t out_sw[OUT_DIM];
    uint16_t out_hw[OUT_DIM];

    *(volatile uint32_t *)BENCH_SW_START_ADDR = BENCH_SW_START_MAGIC;
    results[2 * OUT_DIM] = ai_sw(out_sw);
    for (i = 0; i < OUT_DIM; i++) results[i] = out_sw[i];
    *(volatile uint32_t *)BENCH_SW_END_ADDR = BENCH_SW_END_MAGIC;

    *(volatile uint32_t *)BENCH_HW_START_ADDR = BENCH_HW_START_MAGIC;
    results[2 * OUT_DIM + 1] = ai_hw(out_hw);
    for (i = 0; i < OUT_DIM; i++) results[OUT_DIM + i] = out_hw[i];

    *(volatile uint32_t *)TEST_MAGIC_ADDR = AI_MAGIC;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
