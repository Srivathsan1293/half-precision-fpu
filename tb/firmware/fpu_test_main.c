/* tb/firmware/fpu_test_main.c
 *
 * FPU coprocessor test for the PicoRV32 SoC. Runs a set of half-precision
 * (binary16) floating-point operations through the PCPI custom instructions
 * defined in fpu_macros.h and stores the 16-bit results to RAM.
 *
 * Build with FPU_TEST=1 (see Makefile). This produces meaningful results
 * only when the SoC top is built with the PCPI FPU wrapper connected
 * (define HAS_FPU_PCPI — see tb/soc_fpu_top.sv); otherwise the custom
 * instructions trap after the PCPI timeout and the core halts.
 */

#include <stdint.h>

#include "fpu_macros.h"

#define RESULTS_BASE     0x1000u
#define TEST_MAGIC_ADDR  0x1C00u
#define DONE_ADDR        0x1C04u
#define DONE_MAGIC       0xDEADBEEFu
#define TEST_MAGIC_FPU   0x5F50555Au

volatile uint32_t *const results = (volatile uint32_t *)RESULTS_BASE;

int main(void);

void _start(void) __attribute__((section(".text._start"), noreturn));
void _start(void) {
    (void)main();
    for (;;) ;
}

int main(void) {
    uint32_t i = 0;

    /* binary16 bit patterns held in the low 16 bits */
    const uint32_t ONE   = 0x00003C00u;  /*  1.0  */
    const uint32_t TWO   = 0x00004000u;  /*  2.0  */
    const uint32_t THREE = 0x00004200u;  /*  3.0  */
    const uint32_t HALF  = 0x00003800u;  /*  0.5  */
    const uint32_t NEG2  = 0x0000C000u;  /* -2.0  */
    const uint32_t ZERO  = 0x00000000u;  /* +0.0  */
    const uint32_t INF   = 0x00007C00u;  /* +inf  */
    const uint32_t QNAN  = 0x00007E00u;  /* qNaN  */

    /* subnormal (binary16 denormal) bit patterns */
    const uint32_t MINSUB  = 0x00000001u;  /*  2^-24  (smallest subnormal) */
    const uint32_t SUB2    = 0x00000002u;  /*  2^-23  */
    const uint32_t SUB2_15 = 0x00000200u;  /*  2^-15  (subnormal) */
    const uint32_t MAXSUB  = 0x000003FFu;  /*  largest subnormal */
    const uint32_t NEGSUB  = 0x00008001u;  /* -2^-24  */
    const uint32_t MINNRM  = 0x00000400u;  /*  2^-14  (smallest normal) */

    results[i++] = fadd_half(ONE, TWO);     /* 1.0 + 2.0  =  3.0  */
    results[i++] = fsub_half(TWO, ONE);     /* 2.0 - 1.0  =  1.0  */
    results[i++] = fmul_half(THREE, TWO);   /* 3.0 * 2.0  =  6.0  */
    results[i++] = fdiv_half(ONE, TWO);     /* 1.0 / 2.0  =  0.5  */
    results[i++] = fadd_half(ONE, INF);     /* 1.0 + inf  =  inf  */
    results[i++] = fsub_half(INF, INF);     /* inf - inf  =  NaN  */
    results[i++] = fmul_half(ZERO, INF);    /* 0.0 * inf  =  NaN  */
    results[i++] = fdiv_half(ONE, ZERO);    /* 1.0 / 0.0  =  inf  */
    results[i++] = fdiv_half(ZERO, ZERO);   /* 0.0 / 0.0  =  NaN  */
    results[i++] = fadd_half(ONE, NEG2);    /* 1.0 - 2.0  = -1.0  */
    results[i++] = fmul_half(NEG2, THREE);  /* -2.0 * 3.0 = -6.0  */
    results[i++] = fsub_half(HALF, ONE);    /* 0.5 - 1.0  = -0.5  */
    results[i++] = fdiv_half(THREE, TWO);   /* 3.0 / 2.0  =  1.5  */
    results[i++] = fadd_half(QNAN, ONE);    /* NaN + 1.0  =  NaN  */
    results[i++] = fdiv_half(ONE, QNAN);    /* 1.0 / NaN  =  NaN  */

    /* Subnormal (denormal) corner cases */
    results[i++] = fadd_half(MINSUB, MINSUB);   /* 2^-24 + 2^-24   = 2^-23        (subnormal out) */
    results[i++] = fadd_half(MINSUB, MAXSUB);   /* 2^-24 + maxsub  = 2^-14        (gradual to normal) */
    results[i++] = fadd_half(NEGSUB, MINSUB);   /* -2^-24 + 2^-24  = +0.0         */
    results[i++] = fsub_half(SUB2, MINSUB);     /* 2^-23 - 2^-24   = 2^-24        (subnormal out) */
    results[i++] = fsub_half(ZERO, MINSUB);     /* 0.0 - 2^-24     = -2^-24       (negative subnormal) */
    results[i++] = fsub_half(MINSUB, SUB2);     /* 2^-24 - 2^-23   = -2^-24       (negative subnormal) */
    results[i++] = fmul_half(SUB2, SUB2_15);    /* 2^-23 * 2^-15   = 0.0          (underflow) */
    results[i++] = fmul_half(MINSUB, MINNRM);   /* 2^-24 * 2^-14   = 0.0          (underflow) */
    results[i++] = fmul_half(SUB2_15, SUB2_15); /* 2^-15 * 2^-15   = 0.0          (underflow) */
    results[i++] = fdiv_half(SUB2, MINSUB);     /* 2^-23 / 2^-24   = 2.0          */
    results[i++] = fdiv_half(MINSUB, MINNRM);   /* 2^-24 / 2^-14   = 2^-10        */
    results[i++] = fdiv_half(MINNRM, MINSUB);   /* 2^-14 / 2^-24   = 2^10         */
    results[i++] = fdiv_half(MINSUB, NEGSUB);   /* 2^-24 / -2^-24  = -1.0         */

    *(volatile uint32_t *)TEST_MAGIC_ADDR = TEST_MAGIC_FPU;
    *(volatile uint32_t *)DONE_ADDR = DONE_MAGIC;

    return 0;
}
