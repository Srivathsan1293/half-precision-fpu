# Half-Precision FPU — A Fast, IEEE 754-Compliant Float16 Unit

A high-performance, IEEE 754-compliant half-precision (`binary16`) floating-point
unit with add/subtract, multiply, and divide, synthesized for the SkyWater 130 nm
open-source PDK and verified end-to-end in a PicoRV32 RISC-V SoC.

**Headline results (Sky130, tt / 1.8 V / 25 °C):**

- **Correct:** 17.2B IEEE-754 checks, 0 failures; 6/6 exhaustive test stages pass.
- **Fast:** timing-closed at 100–143 MHz, 6.4–8.4 ns critical paths, 12–64 pJ/op.
- **Fast at system level:** 10.8–19.0x speedup over software soft-float across
  real workloads (matrix multiply, FIR filter, vector divide).
- **Area-efficient:** 1.5–5.5 kGE per datapath unit (8.8 kGE for the full wrapper).

## Project Goals

1. **IEEE 754 compliance** — correct rounding (RNE), NaN/Inf handling, signed
   zeros, and subnormal support, verified exhaustively.
2. **High performance** — minimize combinational critical-path delay and maximize
   throughput (Fmax) at 130 nm.
3. **Pipelining** — split datapaths to beat the combinational timing limit and
   raise Fmax.
4. **Low power** — attack the dominant power sinks (FDIV correction logic).
5. **CPU integration** — plug into a RISC-V core as a Pico Co-Processor (PCPI)
   and prove it with a real soft-float-vs-hardware benchmark on a PicoRV32 SoC,
   including a head-to-head against the third-party **FPNew** unit.

## Architecture

| Module | Description |
|--------|-------------|
| `src/fpu_FMUL.sv` | Booth-encoded Wallace-tree multiplier |
| `src/fpu_FADDSUB.sv` | IEEE 754 add/subtract (shared datapath, sign-inversion for FSUB) |
| `src/fpu_FDIV.sv` | Iterative division via reciprocal ROM + back-multiply quotient correction |
| `src/fpu_modules.sv` | Packed module definitions (special-case flag logic) |
| `src/fpu_test.sv` | Combined top: FMUL + FADDSUB + FDIV with aligned pipelines |
| `src/fpu_pcpi.sv` | PicoRV32 PCPI wrapper: 3-state FSM (idle/compute/done) + `fpu_test` |
| `src/fpnew_pcpi_adapter.sv` | PCPI adapter that maps the same bus onto FPNew (reference unit) |

FP16 format: 1 sign + 5 exponent + 10 fraction. Exponent `0x1f` selects Inf/NaN,
exponent `0x00` selects zero/subnormals, and `0x01`–`0x1e` are normal values.
All arithmetic uses round-to-nearest-even (RNE), with subnormal support in every
operation.

### FDIV algorithm

FDIV uses a reciprocal-ROM approach: look up `1/B` from a 1,024-entry ROM,
multiply by `A`, then back-multiply the trial quotient (`q_trial * B`) and compare
against the shifted dividend to correct the quotient. The critical path is the
three chained combinational operations (14×11 multiply, 15×11 multiply + 26-bit
subtract, cascaded comparator tree).

## IEEE 754 Compliance & Verification

The FPU was verified exhaustively against an independent golden model built on C
`_Float16`:

- **Per-module 2^32 (4.29B) operand-combination sweeps** for FADD/FSUB/FMUL/FDIV
  — 0 mismatches vs the golden model across every NaN/Inf/zero/subnormal/normal
  category.
- **Combined `fpu_test` exhaustive check** — all 4 operations over every `(a,b)`
  input pair with NaN-tolerant comparisons: **0 failures across 17.2B checks**.
- **CPU + PCPI suites** — IEEE special-vector suite (1,296 vectors), numeric
  sweep + back-to-back ops + accumulation loop, the 17-op asm-all golden check,
  109 edge-case vectors (sNaN payloads, signed zeros, Inf/NaN FCVT saturation,
  every FCLASS category), and the unsupported-op halt probe: **all pass**.
- **6-stage exhaustive pipeline** (`run_exhaustive_tests.sh`): **6 passed, 0
  failed** (see `testing_results/fpu_exhaustive_log.txt`).

Two cross-cycle contamination bugs in the pre-existing FADDSUB/FMUL datapaths were
fixed during pipelining so the exhaustive check genuinely passes:

- **FADDSUB**: normalization combined the *registered* mantissa of one input set
  with the *unregistered* exponent/special flags of the next set; the flags are
  now registered with the mantissa so every pipeline stage uses one consistent
  input set.
- **FMUL**: the result sign was stamped from the *current* input while the
  product came from the registered stage; the sign now uses the registered
  `sign_bit_reg` (also fixes sign-of-zero on underflow).

## PPA — Area / Timing / Power (Sky130 @ 25 °C, 1.8 V)

Synthesis: Yosys + ABC (Sky130, mfs2 mapping flow). Timing/power: OpenSTA.
Normalization: 1 GE = 3.7536 µm² (`nand2_1`), FO4 = 37.2 ps. Power at 0.1 activity.

Authoritative measurement (run `testing_results/bench_20260813_174140/bench_report.md`):

| Module | Cells | Area (µm²) | Area (kGE) | Critical path (LTP) | Fmax (1/LTP) |
|--------|------:|-----------:|-----------:|--------------------:|-------------:|
| FMUL | 1,387 | 7,616 | 2.0 | 8.02 ns | 124.8 MHz |
| FADDSUB | 1,061 | 5,686 | 1.5 | 7.33 ns | 136.5 MHz |
| FDIV | 3,539 | 20,709 | 5.5 | 8.40 ns | 119.0 MHz |
| **`fpu_test` (combined)** | 5,819 | 33,208 | 8.8 | 10.45 ns | 95.7 MHz |
| **`fpu_pcpi` (wrapper + FSM)** | 5,786 | 33,093 | 8.8 | 11.31 ns | 88.4 MHz |

> The combined `fpu_test`/`fpu_pcpi` rows are the current authoritative
> measurements (current combined-top alignment/ABC mapping); earlier README
> numbers for these two tops are superseded. Standalone FMUL/FADDSUB/DIV
> reconcile to within 0.1% of their dedicated rows.

Timing-closed Fmax (largest clock that satisfies setup) and energy:

| Module | Timing-closed Fmax | At (dly ps, clk ns) | Total power | Energy |
|--------|-------------------:|--------------------:|------------:|-------:|
| FADDSUB | 142.9 MHz | 4000, 7 | 1.96 mW | 12.6 pJ/op |
| FMUL | 125.0 MHz | 4000, 8 | 1.84 mW | 13.3 pJ/op |
| FDIV | 111.1 MHz | 4000, 9 | 7.6 mW | 63.9 pJ/op |
| `fpu_test` | 100.0 MHz | 4000, 10 | 9.67 mW | 86.2 pJ/op |
| `fpu_pcpi` | 111.1 MHz | 4000, 9 | 11.3 mW | 94.2 pJ/op |

Observations from the sweep:

- **FDIV is the cost center** — roughly half the combined die (20.7 kµm²) and
  ~6-9x the per-op energy of FMUL/FADDSUB. It is the clear Pareto-gap item for
  any next revision (Goldschmidt/SRT, more stages, faster ROM).
- The combined top flattens and shares decode/glue, so its area lands within
  ~2.4% of the sum of the three datapaths (`FMUL+FADDSUB+FDIV = 34.0 kµm²`).
- 119–143 MHz (LTP bound) on a *wider* half-precision datapath is consistent
  with published Sky130 datapoints (PicoRV32-class cores ~100–200 MHz; fp4/8
  units ~250 MHz on smaller/shallower datatypes).

## FLOPS — Peak vs Realized (fp16)

FLOPS is measured at the **`fpu_pcpi` wrapper** (the module that talks to the
PicoRV32 CPU), not the raw datapath. It is a *single-lane scalar* FP16 unit, so
the numbers are fp16 FLOPs — always quote the precision. Reproduce with:

```
./run_flops.sh     # synth fpu_pcpi + STA sweep + workload sims -> flops_report.md
```

From `testing_results/flops_20260814_161745/flops_report.md` (ABC `-D 4000`):

| Metric | Value |
|--------|-------:|
| Area | 34.4 kµm² (9.2 kGE, 6,006 cells / 228 flops) |
| LTP | 8.33 ns (224 FO4) |
| Fmax = 1/LTP | 120 MHz |
| Timing-closed Fmax | 111.1 MHz (clk 9 ns) |
| Energy/op @ closed | 101.7 pJ |

**Peak FLOPS (fp16).** The datapath (`fpu_test`) is a fully pipelined 1-op/cycle
unit, but the PCPI wrapper *serializes* it — its FSM retires one op per latency
(FADD/FSUB/FMUL = 1 cycle, FDIV = 4 cycles):

| Variant | LTP-bound | Timing-closed |
|---------|----------:|--------------:|
| Datapath peak (any op) = Fmax | 120.0 MFLOP/s | 111.1 MFLOP/s |
| Interface peak FADD/FSUB/FMUL = Fmax | 120.0 MFLOP/s | 111.1 MFLOP/s |
| Interface peak FDIV = Fmax/4 | 30.0 MFLOP/s | 27.8 MFLOP/s |

**Realized FLOPS (measured, SoC + firmware included).** Useful FP ops ×
timing-closed Fmax ÷ HW-phase cycles, from the same `benchmm/benchdig/benchdiv`
firmware workloads:

| Workload | FP ops | HW cycles | Realized |
|----------|-------:|----------:|---------:|
| 4x4 fp16 matmul | 144 | 4,717 | 3.39 MFLOP/s |
| 5-tap FIR | 156 | 6,538 | 2.65 MFLOP/s |
| fp16 vector divide | 128 | 4,658 | 3.05 MFLOP/s |

The gap to peak is **not an FPU defect**: each custom op costs ~30 extra cycles
of CPU/firmware overhead (the 4-instruction `fpu_macros.h` wrapper, operand
loads/stores, and serial accumulator dependencies on a single-issue PicoRV32).
The FPU itself spends just 1 (add/mul) or 4 (div) of those cycles. Compare **peak
FLOPS** against other FPU designs; compare **realized FLOPS** only against other
whole systems (the `run_cycle_compare.sh` table is the fair system-level head-to-
head, where the custom unit already beats FPNew by 2–6%).

**Cross-node comparison (process-node-independent).** Raw FLOPS/MHz are not
portable across process nodes. Normalized FOMs measured separately per axis:

| FOM | Value |
|-----|------:|
| ops/FO4 (= 1/LTP_fo4) | 0.0045 |
| FLOPS/GE (peak, LTP-bound) | 13.1 kFLOP/s/GE |
| FLOPS/W (peak, closed) | 1.09 GFLOPS/W |
| mW/MHz | 0.10 |
| ADP | 287 kµm²·ns |

Compare designs with **ops/FO4** (speed), **kGE** (area) and **pJ/op** (energy),
never raw GFLOPS or MHz.

## Performance — Software vs Hardware (cycle counts)

Real-world workloads run twice in firmware: a pure-software soft-float phase
(`tb/firmware/soft_half.h`) and a hardware phase driving the FPU through
custom0 instructions. The same binary is measured against the **custom FPU PCPI
wrapper** and the third-party **FPNew** unit (via `src/fpnew_pcpi_adapter.sv`).
Each run is checked against the golden model (all results match bit-for-bit).

From `testing_results/cycle_comparison_20260814_132049.md`:

| Workload | SW cycles | Custom FPU PCPI | FPNew | Speedup (PCPI vs SW) | Speedup (FPNew vs SW) |
|----------|----------:|----------------:|------:|---------------------:|----------------------:|
| 4x4 fp16 matrix multiply (`benchmm`) | 64,654 | 4,717 | 4,861 | **13.7x** | 13.3x |
| 5-tap FIR filter (`benchdig`) | 70,410 | 6,538 | 6,694 | **10.8x** | 10.5x |
| fp16 vector divide (`benchdiv`) | 88,651 | 4,658 | 4,978 | **19.0x** | 17.8x |

The custom unit is 2–6% faster than FPNew on every workload while using a
single shared datapath, and both are an order of magnitude faster than
software emulation. Reproduce with:

```
./run_cycle_compare.sh     # builds + runs benchmm/benchdig/benchdiv, writes a .md report
```

## CPU Integration (PicoRV32 + PCPI)

A PicoRV32 SoC attaches the FPU as a Pico Co-Processor (PCPI) driven by RISC-V
`custom0` instructions (opcode `0001011`, R-type, funct3=000). `funct7` selects
the operation; operands are fp16 bit patterns in the low 16 bits of `rs1`/`rs2`,
and the 16-bit result is zero-extended into `rd`.

| funct7 | mnemonic | Latency (cycles) |
|--------|----------|:----------------:|
| `0000110` | FADD.H | 2 |
| `0000111` | FSUB.H | 2 |
| `0001000` | FMUL.H | 2 |
| `0001001` | FDIV.H | 4 |

(The `0000000`–`0000101` funct7 values are reserved by PicoRV32's own IRQ custom
instructions.) The FSM tracks a per-op latency counter and never fires early, so
the PCPI handshake protocol is never violated.

The SoC also implements the wider Zhinx feature set in software (emulator at the
`0x800` IRQ handler): FEQ/FLT/FLE compares, FSGNJ/FSGNJN/FSGNJX sign injection,
FMIN/FMAX, FCLASS, and FCVT conversions with RTZ/RNE/RDN/RUP/RMM rounding —
hardware handles FADD/FSUB/FMUL/FDIV, the emulator handles the rest.

## Quick Start

Dependencies: `verilator`, `yosys`, `opensta`, `clang`/`llvm-ldd` (or
`riscv64-unknown-elf-gcc`), Python 3.

```
./run_cpu_test.sh fpu            # FPU PCPI test (IEEE vectors, CPU + wrapper)
./run_cpu_test.sh stress         # numeric sweep + back-to-back ops + accumulation
./run_cpu_test.sh zhinx          # full Zhinx feature set (hw + emulator)
./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000   # edge-case sweep
./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000   # unsupported-op probe

./run_fsm.sh                     # PCPI FSM timing verification (wrapper only)
./run_pcpi_handshake.sh          # PCPI wait/ready handshake protocol verification

./run_exhaustive_tests.sh        # 6-stage build-only exhaustive pipeline
./run_exhaustive_tests.sh --run  # actually execute all stages (hours)

./run_cycle_compare.sh           # SW vs HW cycle-count comparison (custom + FPNew)

./run_flops.sh                   # FLOPS of fpu_pcpi (peak + realized) + cross-node FOMs
```

## Project Structure

```
.
├── src/                          # RTL
│   ├── fpu_FMUL.sv               # Booth-encoded Wallace-tree multiplier
│   ├── fpu_FADDSUB.sv            # IEEE 754 add/subtract
│   ├── fpu_FDIV.sv               # Iterative division via reciprocal ROM
│   ├── fpu_modules.sv            # Packed module definitions
│   ├── fpu_test.sv               # Combined top: FMUL + FADDSUB + FDIV
│   ├── fpu_pcpi.sv               # PicoRV32 PCPI wrapper (3-state FSM)
│   └── fpnew_pcpi_adapter.sv     # PCPI adapter for the FPNew reference unit
├── third_party/
│   ├── picorv32.v                # PicoRV32 RISC-V core (vendored)
│   └── fpnew/                    # FPNew floating-point unit (reference)
├── tb/                           # Testbenches + SoC + firmware
│   ├── soc_fpu_top.sv            # PicoRV32 + RAM + PCPI SoC top
│   ├── tb_fpu_pcpi.cpp           # SoC harness (SW/HW cycle counters, golden checks)
│   ├── tb_fpu.cpp / tb_fADDSUB.cpp / tb_fMUL.cpp / tb_fDIV.cpp   # exhaustive
│   └── firmware/                 # Bare-metal firmware (C, RV32I) + soft_half.h
├── synth_scripts/                # Yosys (.ys) + OpenSTA (.tcl) PPA flows
├── tools/                        # sv2v conversion, PPA metric extraction
├── run_cpu_test.sh               # One-shot build + run (SoC tests)
├── run_fsm.sh                    # PCPI FSM timing verification
├── run_pcpi_handshake.sh         # PCPI handshake verification
├── run_exhaustive_tests.sh       # 6-stage exhaustive pipeline
├── run_cycle_compare.sh          # SW vs HW cycle-count comparison (custom + FPNew)
├── run_flops.sh                  # FLOPS of fpu_pcpi: peak (interface/datapath) + realized + cross-node FOMs
└── testing_results/              # Logs, PPA reports, cycle-comparison reports
```

## Tools

- **RTL**: SystemVerilog
- **Synthesis**: Yosys + ABC (Sky130 cell library)
- **Timing/Power**: OpenSTA (`STA_BIN` env var overrides the STA binary)
- **Simulation**: Verilator (converted FPNew sources via sv2v where needed)
- **PDK**: SkyWater 130 nm (`sky130_fd_sc_hd`)
- **CPU firmware**: clang/LLD or `riscv64-unknown-elf-gcc` (RV32I, bare-metal)

## License

Third-party components: PicoRV32 (ISC) and FPNew (Solderpad / Apache 2.0) — see
their directories for the exact terms. Project sources are original unless noted.
