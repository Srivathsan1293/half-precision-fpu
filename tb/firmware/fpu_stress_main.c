/* tb/firmware/fpu_stress_main.c
 *
 * Exhaustive PCPI-FPU stress test: a numeric edge-case sweep plus CPU-side
 * edge cases (back-to-back issue, register hazards, rd==rs accumulators,
 * discard to x0, and a loop). Results are stored to a fixed region and the
 * harness (tb/tb_fpu_pcpi.cpp) compares them against an IEEE-754 golden model.
 *
 * Result layout (each a 32-bit word, low 16 bits are the binary16 result):
 *   [0, n)              A: numeric sweep over fpu_vectors
 *   [n, n+8)            B: back-to-back FADD
 *   [n+8, n+16)         C: accumulator, rd == rs1 == x1
 *   [n+16, n+24)        D: accumulator, rd == x1, rs2 == x1
 *   [n+24, n+40)        E: loop, 4 iters x 4 ops
 *   [n+40]              F: discard (rd=x0) then a fresh FADD
 *
 * Build with FPU_TEST=stress (see Makefile / run_cpu_test.sh stress).
 */

#include <stdint.h>

#include "fpu_macros.h"
#include "fpu_vectors.h"

#define RESULTS_BASE     0x1000u
#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu
#define TEST_MAGIC_STRESS 0x5F50555Cu

volatile uint32_t *const results = (volatile uint32_t *)RESULTS_BASE;

static uint32_t fpu_op(unsigned op, uint32_t a, uint32_t b) {
    switch (op) {
        case 0: return fadd_half(a, b);
        case 1: return fsub_half(a, b);
        case 2: return fmul_half(a, b);
        default: return fdiv_half(a, b);
    }
}

/* B: issue STRESS_B2B_COUNT FADD (rd=x1) back-to-back with only a mv between. */
static void stress_b2b(uint32_t a, uint32_t b, uint32_t *out) {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    __asm__ volatile(
        "mv x10, %[a]\n\t"
        "mv x11, %[b]\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r0], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r1], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r2], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r3], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r4], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r5], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r6], x1\n\t"
        ".word 0x0cb5008b\n\t" "mv %[r7], x1\n\t"
        : [r0] "=r"(r0), [r1] "=r"(r1), [r2] "=r"(r2), [r3] "=r"(r3),
          [r4] "=r"(r4), [r5] "=r"(r5), [r6] "=r"(r6), [r7] "=r"(r7)
        : [a] "r"(a), [b] "r"(b)
        : "x10", "x11", "x1");
    out[0] = r0; out[1] = r1; out[2] = r2; out[3] = r3;
    out[4] = r4; out[5] = r5; out[6] = r6; out[7] = r7;
}

/* C: accumulator with rd == rs1 == x1: x1 = FADD(x1, x11). */
static void stress_acc1(uint32_t init, uint32_t b, uint32_t *out) {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    __asm__ volatile(
        "mv x1, %[init]\n\t"
        "mv x11, %[b]\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r0], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r1], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r2], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r3], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r4], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r5], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r6], x1\n\t"
        ".word 0x0cb0808b\n\t" "mv %[r7], x1\n\t"
        : [r0] "=r"(r0), [r1] "=r"(r1), [r2] "=r"(r2), [r3] "=r"(r3),
          [r4] "=r"(r4), [r5] "=r"(r5), [r6] "=r"(r6), [r7] "=r"(r7)
        : [init] "r"(init), [b] "r"(b)
        : "x1", "x11");
    out[0] = r0; out[1] = r1; out[2] = r2; out[3] = r3;
    out[4] = r4; out[5] = r5; out[6] = r6; out[7] = r7;
}

/* D: accumulator with rd == x1, rs2 == x1: x1 = FADD(x10, x1). */
static void stress_acc2(uint32_t init, uint32_t a, uint32_t *out) {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    __asm__ volatile(
        "mv x1, %[init]\n\t"
        "mv x10, %[a]\n\t"
        ".word 0x0c15008b\n\t" "mv %[r0], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r1], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r2], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r3], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r4], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r5], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r6], x1\n\t"
        ".word 0x0c15008b\n\t" "mv %[r7], x1\n\t"
        : [r0] "=r"(r0), [r1] "=r"(r1), [r2] "=r"(r2), [r3] "=r"(r3),
          [r4] "=r"(r4), [r5] "=r"(r5), [r6] "=r"(r6), [r7] "=r"(r7)
        : [init] "r"(init), [a] "r"(a)
        : "x1", "x10");
    out[0] = r0; out[1] = r1; out[2] = r2; out[3] = r3;
    out[4] = r4; out[5] = r5; out[6] = r6; out[7] = r7;
}

/* F: run one FADD with rd == x0 (result discarded), then return a fresh FADD. */
static uint32_t stress_discard(uint32_t a, uint32_t b) {
    __asm__ volatile(
        "mv x10, %[a]\n\t"
        "mv x11, %[b]\n\t"
        ".word 0x0cb5000b\n\t"
        :: [a] "r"(a), [b] "r"(b) : "x10", "x11");
    return fadd_half(a, b);
}

void _start(void) __attribute__((section(".text._start"), noreturn));
int main(void);
void _start(void) {
    (void)main();
    for (;;) ;
}

int main(void) {
    unsigned i = 0;

    /* A: numeric sweep through the shared table. */
    {
        unsigned v;
        for (v = 0; v < fpu_sweep_count; v++)
            results[i++] = fpu_op(fpu_sweep_vectors[v].op,
                                  fpu_sweep_vectors[v].a, fpu_sweep_vectors[v].b);
    }

    /* B: back-to-back FADD x8. */
    {
        uint32_t r[STRESS_B2B_COUNT];
        unsigned k;
        stress_b2b(STRESS_B2B_A, STRESS_B2B_B, r);
        for (k = 0; k < STRESS_B2B_COUNT; k++) results[i++] = r[k];
    }

    /* C: accumulator rd == rs1 == x1, x1 += 0.5. */
    {
        uint32_t r[STRESS_ACC1_COUNT];
        unsigned k;
        stress_acc1(STRESS_ACC1_INIT, STRESS_ACC1_B, r);
        for (k = 0; k < STRESS_ACC1_COUNT; k++) results[i++] = r[k];
    }

    /* D: accumulator rd == x1, rs2 == x1, x1 += 2.0. */
    {
        uint32_t r[STRESS_ACC2_COUNT];
        unsigned k;
        stress_acc2(STRESS_ACC2_INIT, STRESS_ACC2_A, r);
        for (k = 0; k < STRESS_ACC2_COUNT; k++) results[i++] = r[k];
    }

    /* E: loop, 4 iterations x 4 ops on the first vectors. */
    {
        unsigned it, op;
        for (it = 0; it < STRESS_LOOP_ITERS; it++) {
            uint32_t a = fpu_sweep_vectors[it].a;
            uint32_t b = fpu_sweep_vectors[it].b;
            for (op = 0; op < STRESS_LOOP_OPS; op++)
                results[i++] = fpu_op(op, a, b);
        }
    }

    /* F: discard (rd=x0) then re-check state integrity. */
    results[i++] = stress_discard(fpu_sweep_vectors[0].a, fpu_sweep_vectors[0].b);

    *(volatile uint32_t *)TEST_MAGIC_ADDR = TEST_MAGIC_STRESS;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
