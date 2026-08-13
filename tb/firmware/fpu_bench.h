/* tb/firmware/fpu_bench.h
 *
 * Shared constants between the Week 2 SW-vs-HW cycle-count benchmark firmware
 * (fpu_bench_main.c) and the Verilator harness (tb/tb_fpu_pcpi.cpp).
 *
 * The benchmark runs an identical N_TAPS-tap FIR filter over an identical
 * input window twice: once with pure-integer software floating point
 * (soft_half.h) and once with the hardware FPU PCPI custom instructions
 * (fpu_macros.h). The firmware stamps cycle markers into the RAM mmio mirror
 * so the harness can time each phase, and writes the two output windows plus
 * checksums to the results region so the harness can prove they agree.
 *
 * Cycle markers (all in 0x1C00..0x1C2C, clear of the unsup-probe region
 * 0x1C08..0x1C10 and magic/done at 0x1C00/0x1C04):
 *   0x1C20  SW start   0x1C24 SW end    0x1C28 HW start
 * There is no HW-end marker: the DONE_MAGIC write at 0x1C04 ends the run.
 */

#ifndef FPU_BENCH_H
#define FPU_BENCH_H

#define BENCH_N_SAMPLES      16u
#define BENCH_N_TAPS          4u
#define BENCH_N_PASSES        1u

#define BENCH_RESULTS_BASE      0x1000u
#define BENCH_TOTAL_WORDS       (2 * BENCH_N_SAMPLES + 2)

#define BENCH_MAGIC             0x5F50555Eu

#define BENCH_SW_END_ADDR       0x1C24u
#define BENCH_SW_END_MAGIC      0x5F535744u   /* "SWD_" */
#define BENCH_SW_START_ADDR     0x1C20u
#define BENCH_SW_START_MAGIC    0x5F534744u   /* "SJD_" */
#define BENCH_HW_START_ADDR     0x1C28u
#define BENCH_HW_START_MAGIC    0x5F484744u   /* "HJD_" */

#endif /* FPU_BENCH_H */