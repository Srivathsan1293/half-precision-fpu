# FPGA/ASIC FPU Benchmark Results — Week 2 (SW vs HW)

**Date:** 2026-08-13  
**Platform:** PicoRV32 (RV32IM, 32-bit) + FPU PCPI coprocessor, Verilator cycle-accurate RTL simulation  
**Baseline:** software soft-float (`soft_half.h`) compiled as generic rv32im soft float — one
hardware integer `mul` per significand product, CLZ-driven single-shot normalization
(no bit-by-bit loops), matching what a normal embedded soft-float library does on a
real rv32im core.

---

## Executive Summary

Each benchmark runs an **identical kernel** twice on the same core: once through
generic software floating-point and once through the hardware FPU PCPI custom
instructions. All outputs and checksums are **bit-identical** (0 mismatches) between
software and hardware in every run.

| Workload | SW cycles | HW cycles | Speedup |
|----------|----------:|----------:|--------:|
| FIR (4-tap, 128 ops)       | 39,759 | 4,121 | **9.65×** |
| Matmul (4×4, 128 ops)      | 64,654 | 5,149 | **12.56×** |
| Digital FIR (5-tap, 160 ops) | 70,410 | 7,006 | **10.05×** |
| Vector divide (128 ops)    | 88,651 | 4,978 | **17.81×** |

**Takeaway.** The FPU PCPI coprocessor delivers **~10–13× fewer cycles** than a
generic rv32im soft-float baseline for multiply/add-heavy DSP kernels (FIR, matmul,
digital FIR) and **~18×** for division. Software division is dominated by its
long-division bit loop, so it is where the hardware FDIV shows its real advantage.

> **Note on "cycles/op":** the software figure includes loop overhead plus the
> soft-FP routines; the hardware figure includes the CPU instruction fetch / store
> of every custom op and the PCPI handshake. This is a *system-level* comparison
> (CPU+FPU), not raw FPU-unit throughput or Hz. The FPU units themselves are
> 1-cycle-throughput (see PPA below).

---

## How It Was Measured

The firmware (e.g. `tb/firmware/fpu_bench_main.c`) runs both phases back-to-back and
stamps cycle markers into the RAM mmio mirror (`fpu_bench.h`):

| Marker | Address | Meaning |
|--------|---------|---------|
| SW start | 0x1C20 | just before the soft-float loop |
| SW end | 0x1C24 | just after the last soft op |
| HW start | 0x1C28 | just before the hardware loop |
| DONE | 0x1C04 | end of run (hardware phase end) |

The harness `tb/tb_fpu_pcpi.cpp` latches the cycle count on each marker, compares
every output window and checksum bit-for-bit, and reports e.g.:

```
PicoRV32 + FPU-PCPI integration test
  acc(final) sw=0x4b80 hw=0x4b80 match
  cycles: SW soft-float phase = 39759  HW PCPI phase = 4121  speedup = 9.6479x
STATUS: PASS
```

Reproduce:

```bash
./run_cpu_test.sh bench          # FIR
./run_cpu_test.sh benchmm        # 4x4 fp16 matrix multiply
./run_cpu_test.sh benchdig       # 5-tap fp16 digital FIR
./run_cpu_test.sh benchdiv       # fp16 vector divide
```

The bench firmware builds `-march=rv32im` (Makefile) and the core has the integer M
extension enabled (`soc_fpu_top.sv`), so the SW phase is a generic rv32im soft-float
baseline.

---

## Workload Description

### 1. FIR filter (4-tap, 16 samples) — 128 FP ops
`accᵢ = accᵢ₋₁ + Σ_k x[n]·coef[k]`, coefficients 0.5/0.25/0.125/0.0625, samples 1.0.
Mixes multiplies and dependent adds.

### 2. 4×4 fp16 matrix multiply — 128 FP ops
`C[i][j] = Σ_k A[i][k]·B[k][j]`, normal-range fixed data. Serial accumulation chain
shows the FPU at its worst (handshake latency per op dominates).

### 3. 5-tap digital FIR — 160 FP ops
Same structure as 1 but 5 taps: `y[n] = Σ_k x[n]·coef[k]`. Larger tap count, more
pure-FP work per output.

### 4. fp16 vector divide — 128 FP ops
`y[i] = x[i] / d[i]`, power-of-two denominators so every quotient is exact. Software
division is the costliest soft routine (restore long division, ~10× the soft
multiply), so this is the FPU's strongest, most honest case.

All data is normal-range with exact quotients/accumulations, so no subnormal or
overflow edges are exercised — the benchmark measures typical computation, not
exception handling.

---

## PPA (from plan.md, standalone synthesis)

Synthesis numbers are for the standalone FPU modules on Sky130; they are
throughput/area analysis, not part of the measured cycle counts above.

| Module | Area (µm²) | Cells | LTP | Fmax | Power (mW) |
|--------|------------|-------|-----|------|------------|
| FMUL | 7,616 | 2,029 GE | 8.02 ns | 124.8 MHz | 1.51 |
| FADDSUB | 5,685 | 1,515 GE | 7.33 ns | 136.5 MHz | 1.38 |
| FDIV | 20,709 | 5,517 GE | 8.40 ns | 119.0 MHz | 6.81 |
| **fpu_test** (combined) | 33,208 | 8,847 GE | 7.91 ns | 126.5 MHz | 9.47 |
| **fpu_pcpi** (wrapper + FSM) | 33,421 | 8,904 GE | 6.68 ns | 149.8 MHz | 10.8 |

---

## Correctness Verification (prerequisite)

- **Soft-float reference (`soft_half.h`):** validated vs host `_Float16` RNE —
  300,000 random operand-pair + special-case checks, **0 failures** on all
  normal-range results (971,753 normal-range results checked across
  add/sub/mul/div).
- **HW outputs match SW bit-for-bit** on every sample/accumulator in all four benches.
- Earlier standalone module checks (IEEE-754 RNE, zero/inf/NaN/subnormal handling)
  recorded 0 failures.

---

## What This Means

On a real RV32IM core the FPU PCPI coprocessor delivers **~10–18× fewer cycles**
than a generic software soft-float baseline across four representative DSP/control
kernels, while producing **bit-identical** IEEE-754 results. The remaining HW latency
is the CPU↔PCPI handshake and per-op register moves — the FPU units themselves are
1-cycle-throughput (see PPA Fmax), so larger blocks amortize that overhead further:
a soft-float layer that takes several hundred cycles per FP op in a loop is replaced
by ~23–44 cycles per op including CPU overhead.

---

## References

- [`fpu_bench_main.c`](../../tb/firmware/fpu_bench_main.c): FIR benchmark firmware
- [`fpu_bench_mm_main.c`](../../tb/firmware/fpu_bench_mm_main.c): matmul firmware
- [`fpu_bench_dig_main.c`](../../tb/firmware/fpu_bench_dig_main.c): digital-FIR firmware
- [`fpu_bench_div_main.c`](../../tb/firmware/fpu_bench_div_main.c): vector-divide firmware
- [`fpu_bench.h`](../../tb/firmware/fpu_bench.h): shared markers/layout
- [`soft_half.h`](../../tb/firmware/soft_half.h): software floating-point baseline
- [`fpu_macros.h`](../../tb/firmware/fpu_macros.h): PCPI custom-op wrappers
- [`tb_fpu_pcpi.cpp`](../../tb/tb_fpu_pcpi.cpp): harness (cycle capture + golden check)
- [`run_cpu_test.sh`](../../run_cpu_test.sh): `./run_cpu_test.sh bench`
