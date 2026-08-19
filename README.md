# Half-Precision FPU — A Fast, IEEE 754-Compliant Float16 Unit

A high-performance, IEEE 754-compliant half-precision (`binary16`) floating-point
unit with add/subtract, multiply, and divide, synthesized for the SkyWater 130 nm
open-source PDK and verified end-to-end in a PicoRV32 RISC-V SoC.

**Headline results (Sky130, tt / 1.8 V / 25 °C):**

- **Correct:** 17.2B IEEE-754 checks, 0 failures; 6/6 exhaustive test stages pass.
- **Physically implemented:** full OpenLane PnR signoff at 100 MHz (10 ns target,
  tt WNS **+1.02 ns**), clean **0 DRC / 0 LVS**, 31.3 kµm² stdcell (~8.3 kGE),
  **8.19 mW**, with a **fixed 12-cycle deterministic FDIV**.
- **Fast at system level:** 10.8–17.1x speedup over software soft-float across
  real workloads (matrix multiply, FIR filter, vector divide).
- **Area-efficient:** 4,137 stdcell instances / ~8.3 kGE full wrapper post-PnR
  (pre-route datapath units 1.5–2.3 kGE).

## Project Goals

1. **IEEE 754 compliance** — correct rounding (RNE), NaN/Inf handling, signed
   zeros, and subnormal support, verified exhaustively.
2. **High performance** — minimize combinational critical-path delay and maximize
   throughput (Fmax) at 130 nm.
3. **Pipelining** — split datapaths to beat the combinational timing limit and
   raise Fmax.
4. **Low power** — rewrite the FDIV cost center as a compact sequential SRT core
   (2.3 kGE, was 5.5 kGE) with a deterministic fixed-latency schedule.
5. **CPU integration** — plug into a RISC-V core as a Pico Co-Processor (PCPI)
   and prove it with a real soft-float-vs-hardware benchmark on a PicoRV32 SoC,
   including a head-to-head against the third-party **FPNew** unit.

## Architecture

| Module | Description |
|--------|-------------|
| `src/fpu_FMUL.sv` | Booth-encoded Wallace-tree multiplier |
| `src/fpu_FADDSUB.sv` | IEEE 754 add/subtract (shared datapath, sign-inversion for FSUB) |
| `src/fpu_FDIV.sv` | Sequential radix-4 SRT divider, start-gated fixed 11-cycle schedule (12 cycles end-to-end) |
| `src/fpu_modules.sv` | Packed module definitions (special-case flag logic) |
| `src/fpu_test.sv` | Combined top: FMUL + FADDSUB + FDIV with aligned pipelines |
| `src/fpu_pcpi.sv` | PicoRV32 PCPI wrapper: 3-state FSM (idle/compute/done) + `fpu_test` |
| `src/fpnew_pcpi_adapter.sv` | PCPI adapter that maps the same bus onto FPNew (reference unit) |

FP16 format: 1 sign + 5 exponent + 10 fraction. Exponent `0x1f` selects Inf/NaN,
exponent `0x00` selects zero/subnormals, and `0x01`–`0x1e` are normal values.
All arithmetic uses round-to-nearest-even (RNE), with subnormal support in every
operation.

### FDIV algorithm

FDIV is a **sequential radix-4 SRT division core** (`srt` in
`fdiv_datapath_blocks.sv`). It is start-gated with a fixed 11-cycle schedule
(reload → 8 radix-4 iterations → emit → done): operands are latched into the
stage-1 registers on a `start` pulse and the completed quotient is captured on
the `srt_done` pulse 11 cycles later, giving a **deterministic, non-pipelined
12-cycle divider** (one result every 12 cycles after `start`, quotient stable on
the following cycle). Rounding and normalization are combinational from the
captured quotient. The deterministic latency is what guarantees the real-time
FOC deadline analysis (see `SWaP_C_conclusion.md`).

## Verification & Tests

### IEEE 754 compliance (exhaustive, datapath-level)

The FPU was verified exhaustively against an independent golden model built on C
`_Float16`:

- **Per-module 2^32 (4.29B) operand-combination sweeps** for FADD/FSUB/FMUL/FDIV
  — 0 mismatches vs the golden model across every NaN/Inf/zero/subnormal/normal
  category.
- **Combined `fpu_test` exhaustive check** — all 4 operations over every `(a,b)`
  input pair with NaN-tolerant comparisons: **0 failures across 17.2B checks**.
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

### SoC + PCPI integration (CPU in the loop)

- **IEEE special-vector suite** — 1,296 vectors.
- **Numeric sweep + back-to-back ops + accumulation loop**.
- **17-op asm-all golden check** (bit-for-bit vs the golden model).
- **109 edge-case vectors** — sNaN payloads, signed zeros, Inf/NaN FCVT
  saturation, and every FCLASS category.
- **Unsupported-op halt probe** — verifies the core halts on a reserved op.
- **Zhinx feature set** — FMIN/FMAX, FCLASS, compares, sign-injection and the
  FCVT rounding modes run through the software emulator at the `0x800` IRQ
  handler. **All pass.**

### PCPI protocol (wrapper-only, exact timing)

- `run_fsm.sh` — **29/29 checks**: exact ready-cycle counts (FADD/FSUB/FMUL at
  cycle 1, FDIV at fixed cycle 12), no early-fire, a single
  `pcpi_ready`/`pcpi_wr` pulse, and no re-trigger while `pcpi_valid` is held.
- `run_pcpi_handshake.sh` — **22/22 checks**: the `pcpi_wait`/`pcpi_ready`
  window, ready-within-bound, and result write-back correctness.

### System-level benchmarks

- `run_cycle_compare.sh` — benchmm / benchdig / benchdiv / benchai, software vs
  hardware,
  custom FPU vs FPNew (see Performance section above).
- `run_flops.sh` — peak + realized FLOPS and cross-node FOMs (see FLOPS section).

### Physical signoff (OpenLane PnR)

- **0 DRC** (KLayout + Magic), **0 LVS** errors, and 0 flow errors across run 15
  and all five Pareto points.
- **Timing-closed** at the 10 ns target (tt WNS +1.02 ns); hold clean (0
  violations).

## PPA — Area / Timing / Power (Sky130 @ 25 °C, 1.8 V)

Normalization: 1 GE = 3.7536 µm² (`nand2_1`), FO4 = 37.2 ps.

### Post-route (OpenLane PnR) — authoritative

The full `fpu_pcpi` SoC block was carried through complete OpenLane 2.3.10
physical design (synthesis → floorplan → placement → CTS → routing → DRC/LVS)
for the Sky130A `sky130_fd_sc_hd` library and signed off at a 10 ns clock
(run `designs/fpu_pcpi/runs/fpu_pcpi_pnr_run15/`):

| Metric | Value |
|--------|-------:|
| Stdcell instances | 4,137 |
| Stdcell area | 31,290 µm² (~8.3 kGE) |
| Die area / utilization | 64,026 µm² (247.7 × 258.5 µm) / 55.9 % |
| Timing | 10 ns target closed — tt WNS **+1.02 ns** |
| Power (OpenROAD, max_tt, default activity) | **8.19 mW** (leakage ≪ 0.1 mW) |
| Fmax (timing-closed) | **100 MHz** |
| Signoff | **0 DRC** (KLayout + Magic), **0 LVS** violations, 0 flow errors |

**5-point Pareto sweep** (same RTL, clock target 10–30 ns; run trees
`designs/fpu_pcpi/pareto/runs/pareto_{10,13,15,20,30}ns/`):

| Clock | Fmax | Instances | Area | kGE | Power | tt WNS |
|------:|-----:|----------:|-----:|----:|------:|-------:|
| 10 ns | 100 MHz | 4,137 | 31,290 µm² | 8.3 | 8.19 mW | +1.02 |
| 13 ns | 77 MHz | 4,166 | 30,950 µm² | 8.2 | 6.18 mW | +3.95 |
| 15 ns | 67 MHz | 4,105 | 30,206 µm² | 8.0 | 5.32 mW | +5.98 |
| 20 ns | 50 MHz | 4,007 | 28,306 µm² | 7.5 | 3.64 mW | +9.55 |
| 30 ns | 33 MHz | 4,006 | 28,267 µm² | 7.5 | 2.43 mW | +19.40 |

All five points are signoff-clean (0 DRC / 0 LVS / timing met). The classic
"lazy-synthesis" trade-off is explicit: tightening the clock 30 → 10 ns grows
area +11 % and power ×3.4. Plot + data:
`testing_results/pareto_curve/pareto_curve.png` and `pareto_data.csv`
(regenerate with `python3 tools/plot_pareto.py`).

Silicon visuals from run 15: `.../final/die.png` (full routed die),
`.../final/cts.png` (clock-tree routing), and
`.../final/density/placement_density.png` (placement density map).

### Pre-route datapath breakdown (Yosys + OpenSTA, reference)

Per-datapath numbers before physical design, from the current RTL
(`testing_results/bench_20260819_124736/bench_report.md`). Post-PnR then adds
fill cells, tap cells, antenna diodes and timing-repair buffers, so the physical
instance count/area (4,137 cells / 31,290 µm²) is larger than the pre-route
netlist:

| Module | Cells | Area (µm²) | Area (kGE) | LTP | Fmax (1/LTP) |
|--------|------:|-----------:|-----------:|----:|-------------:|
| FMUL | 1,387 | 7,616 | 2.0 | 8.02 ns | 124.8 MHz |
| FADDSUB | 1,056 | 5,638 | 1.5 | 6.17 ns | 162.2 MHz |
| FDIV (12-cycle SRT) | 1,118 | 8,741 | 2.3 | 5.56 ns | 179.9 MHz |
| **`fpu_test` (combined)** | 3,535 | 21,782 | 5.8 | 8.22 ns | 121.6 MHz |
| **`fpu_pcpi` (wrapper + FSM)** | 3,587 | 22,291 | 5.9 | 8.76 ns | 114.2 MHz |

Observations:

- **FDIV is the cost center** of the datapaths, but the start-gated SRT rewrite
  shrank it to 2.3 kGE (was 5.5 kGE with the reciprocal-ROM design) while making
  the latency a fixed, deterministic 12 cycles.
- `fpu_test` flattens and ABC shares/merges logic across the three datapaths, so
  the combined area lands within ~2.4 % of the sum of the parts
  (`FMUL+FADDSUB+FDIV = 21,995 µm²`).
- Pre-route Fmax of 114–180 MHz (LTP bound) is consistent with published Sky130
  datapoints (PicoRV32-class cores ~100–200 MHz). The post-route signoff is the
  100 MHz figure above.

## FLOPS — Peak vs Realized (fp16)

FLOPS is measured at the **`fpu_pcpi` wrapper** (the module that talks to the
PicoRV32 CPU), not the raw datapath. It is a *single-lane scalar* FP16 unit, so
the numbers are fp16 FLOPs — always quote the precision. Reproduce with:

```
./run_flops.sh     # synth fpu_pcpi + STA sweep + workload sims -> flops_report.md
```

From `testing_results/flops_20260819_125114/flops_report.md` (ABC `-D 4000`):

| Metric | Value |
|--------|-------:|
| Area | 23.0 kµm² (6.13 kGE, 3,692 cells / 92 flops) |
| LTP | 7.31 ns (197 FO4) |
| Fmax = 1/LTP | 136.8 MHz |
| Timing-closed Fmax | 125.0 MHz (clk 8 ns) |
| Total power @ closed | 5.63 mW |
| Energy/op @ closed | 45.0 pJ |

**Peak FLOPS (fp16).** The datapath (`fpu_test`) is a fully pipelined 1-op/cycle
unit, but the PCPI wrapper *serializes* it — its FSM retires one op per latency
(FADD/FSUB/FMUL = 1 cycle, FDIV = **fixed 12 cycles**, deterministic start-gated
radix-4 SRT core):

| Variant | LTP-bound | Timing-closed |
|---------|----------:|--------------:|
| Datapath peak (any op) = Fmax | 136.8 MFLOP/s | 125.0 MFLOP/s |
| Interface peak FADD/FSUB/FMUL = Fmax | 136.8 MFLOP/s | 125.0 MFLOP/s |
| Interface peak FDIV ≈ Fmax/12 (core-bound) | 11.4 MFLOP/s | 10.4 MFLOP/s |

**Realized FLOPS (measured, SoC + firmware included).** Useful FP ops ×
timing-closed Fmax ÷ HW-phase cycles, from the same
`benchmm/benchdig/benchdiv/benchai` firmware workloads:

| Workload | FP ops | HW cycles | Realized |
|----------|-------:|----------:|---------:|
| 4x4 fp16 matmul | 144 | 4,717 | 3.82 MFLOP/s |
| 5-tap FIR | 156 | 6,538 | 2.98 MFLOP/s |
| fp16 vector divide | 128 | 5,170 | 3.09 MFLOP/s |
| Micro AI dense + normalize | 76 | 2,252 | **4.22 MFLOP/s** |

The gap to peak is **not an FPU defect**: each custom op costs ~30 extra cycles
of CPU/firmware overhead (the 4-instruction `fpu_macros.h` wrapper, operand
loads/stores, and serial accumulator dependencies on a single-issue PicoRV32).
The FPU itself spends just 1 (add/mul) or 12 (div) of those cycles. Compare **peak
FLOPS** against other FPU designs; compare **realized FLOPS** only against other
whole systems (the `run_cycle_compare.sh` table is the fair system-level head-to-
head, where the custom unit beats FPNew by 2–3% on matmul/FIR/AI and trails ~4%
only on the divide-heavy loop).

**Cross-node comparison (process-node-independent).** Raw FLOPS/MHz are not
portable across process nodes. Normalized FOMs measured separately per axis:

| FOM | Value |
|-----|------:|
| ops/FO4 (= 1/LTP_fo4) | 0.0051 |
| FLOPS/GE (peak, LTP-bound) | 22.3 kFLOP/s/GE |
| FLOPS/W (peak, closed) | 2.78 GFLOPS/W |
| mW/MHz | 0.045 |
| ADP | 168 kµm²·ns |

Compare designs with **ops/FO4** (speed), **kGE** (area) and **pJ/op** (energy),
never raw GFLOPS or MHz.

## Performance — Software vs Hardware (cycle counts)

Real-world workloads run twice in firmware: a pure-software soft-float phase
(`tb/firmware/soft_half.h`) and a hardware phase driving the FPU through
custom0 instructions. The same binary is measured against the **custom FPU PCPI
wrapper** and the third-party **FPNew** unit (via `src/fpnew_pcpi_adapter.sv`).
Each run is checked against the golden model (all results match bit-for-bit).

From `testing_results/cycle_comparison_20260819_155138.md`:

| Workload | SW cycles | Custom FPU PCPI | FPNew | Speedup (PCPI vs SW) | Speedup (FPNew vs SW) |
|----------|----------:|----------------:|------:|---------------------:|----------------------:|
| 4x4 fp16 matrix multiply (`benchmm`) | 64,654 | 4,717 | 4,861 | **13.7x** | 13.3x |
| 5-tap FIR filter (`benchdig`) | 70,410 | 6,538 | 6,694 | **10.8x** | 10.5x |
| fp16 vector divide (`benchdiv`) | 88,651 | 5,170 | 4,978 | **17.1x** | 17.8x |
| Micro AI dense layer + normalize (`benchai`) | 37,405 | 2,252 | 2,308 | **16.6x** | 16.2x |

The custom unit matches or beats FPNew on most workloads (3 % faster on matmul,
2 % faster on FIR, ~2 % faster on the AI layer) and trails only ~4 % on the
divide-heavy loop — the price of the deterministic fixed 12-cycle FDIV — while
both are an order of magnitude faster than software emulation. Reproduce with:

```
./run_cycle_compare.sh     # builds + runs benchmm/benchdig/benchdiv/benchai, writes a .md report
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
| `0001001` | FDIV.H | 12 |

(Latencies are accept→ready PCPI handshake counts: the single-cycle datapaths
retire one cycle after accept, and FDIV has a fixed 12-cycle start-gated SRT
schedule. The `0000000`–`0000101` funct7 values are reserved by PicoRV32's own
IRQ custom instructions.) The FSM tracks a per-op latency counter and never fires
early, so the PCPI handshake protocol is never violated.

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

./run_cpu_test.sh benchmm       # SW-vs-HW cycle bench: 4x4 fp16 matmul
./run_cpu_test.sh benchdig      # SW-vs-HW cycle bench: 5-tap FIR
./run_cpu_test.sh benchdiv      # SW-vs-HW cycle bench: fp16 vector divide
./run_cpu_test.sh benchai       # SW-vs-HW cycle bench: micro AI dense layer + normalize

./run_cycle_compare.sh           # SW vs HW cycle-count comparison (custom + FPNew)

./run_flops.sh                   # FLOPS of fpu_pcpi (peak + realized) + cross-node FOMs
```

## Project Structure

```
.
├── src/                          # RTL
│   ├── fpu_FMUL.sv               # Booth-encoded Wallace-tree multiplier
│   ├── fpu_FADDSUB.sv            # IEEE 754 add/subtract
│   ├── fpu_FDIV.sv               # Sequential radix-4 SRT divider (fixed 12-cycle)
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
│                                 #   + benchmm/benchdig/benchdiv/benchai mains
│                                 #   + link.ld / link_ai.ld (benchai uses the
│                                 #     relaxed 0x1000 budget, no IRQ/emulator)
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
