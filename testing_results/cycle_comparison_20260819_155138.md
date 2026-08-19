# Cycle-Count Comparison Report

Workloads: benchmm benchdig benchdiv benchai

| Workload | SW cycles | Custom (FPU PCPI) cycles | FPNEW cycles | Speedup (PCPI vs SW) | Speedup (FPNEW vs SW) | PCPI vs FPNEW |
|---|---:|---:|---:|---:|---:|---:|
| benchmm | 64654 | 4717 | 4861 | 13.707x | 13.301x | 0.970x |
| benchdig | 70410 | 6538 | 6694 | 10.769x | 10.518x | 0.977x |
| benchdiv | 88651 | 5170 | 4978 | 17.147x | 17.809x | 1.039x |
| benchai | 37405 | 2252 | 2308 | 16.610x | 16.207x | 0.976x |

Generated: 2026-08-19 15:53:21

Notes:
- Each workload run builds the firmware (tb/firmware) and runs a Verilator simulation.
- The firmware contains both SW and HW phases; the SW phase (soft_half.h) is the same across hardware variants and serves as the baseline.
- The script treats the existing FPU PCPI wrapper (run_cpu_test.sh) as the "custom" hardware implementation.
- FPNEW support is best-effort: the script attempts an FPNEW-based Verilator build only if third_party/fpnew exists; integration may require additional source files.
