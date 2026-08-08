# Half-Precision FPU (Float16)

A fast, IEEE 754 compliant half-precision (binary16) floating-point unit with multiply, add/subtract, and divide operations. Synthesized for the SkyWater 130 nm open-source PDK.

## Project Goals

1. **IEEE 754 Compliance** — Correct rounding, NaN/Inf handling, subnormal support
2. **High Performance** — Minimize combinational critical path delay
3. **Pipelining** — Introduce pipeline stages to increase throughput and Fmax
4. **Low Power** — Reduce dynamic power of the dominant power sinks (FDIV correction logic)
5. **AXI4 Interface** — Wrap the FPU with an AXI4-Lite/AXI4-Stream interface for easy integration
6. **CPU Integration** — Verify with a RISC-V model CPU (e.g., PicoRV32, VexRiscv) on FPGA *(in progress: PicoRV32 SoC + PCPI bus + firmware + harness in place; FPU PCPI wrapper pending)*

## Architecture

| Module | Description |
|--------|-------------|
| `fpu_FMUL.sv` | Booth-encoded Wallace tree multiplier |
| `fpu_FADDSUB.sv` | IEEE 754 compliant add/subtract |
| `fpu_FDIV.sv` | Iterative division via reciprocal ROM |
| `fpu_modules.sv` | Packed module definitions |

## PPA (Sky130 @ 25C, 1.80 V)

Synthesis: Yosys + ABC (Sky130, mfs2 mapping flow). Timing/Power: OpenSTA with a virtual 10 ns clock and 10% input toggle rate. FDIV area/power include the reciprocal ROM synthesized as a logic mux-tree (1,024-entry).

| Module | Stages | Cells | Area | Critical Path (LTP) | Fmax | Power |
|--------|--------|-------|------|---------------------|------|-------|
| FMUL | 1 | 1,083 | 7,417 um^2 | **7.78 ns** | 128.6 MHz | 1.76 mW |
| FADDSUB | 1 | 773 | 5,266 um^2 | **8.39 ns** | 119.2 MHz | 1.33 mW |
| FDIV | 4 | 2,852 | 19,699 um^2 | **8.39 ns** | 119.3 MHz | 7.83 mW |
| **Combined `fpu_test`** | 4 | 5,635 | 33,784 um^2 | **8.15 ns** | 122.7 MHz | 9.85 mW |

Latency = stages x 10 ns clock period; throughput = one result per cycle after pipeline fill (Fmax above). The standalone FMUL/FADDSUB rows are the single internal datapath stage; the combined `fpu_test` aligns all three pipelines to a common 4-cycle latency.

The combined `fpu_test` top instantiates all three datapath modules (FMUL, FADDSUB, FDIV) with shared special-case flag logic. Its area is the sum of the three sub-units plus the shared decode/mux overhead; the critical path (8.15 ns) is set by the slowest stage among the three pipelines.

## FDIV Optimization: 19.61 ns -> 8.39 ns

The FDIV module uses a reciprocal-ROM-based algorithm: look up 1/B from a ROM, multiply by A, then perform a back-multiply quotient refinement (q_trial * B) and compare the result against the shifted dividend to correct the quotient.

The critical path is entirely combinational with three sequential operations chained:
1. `initial_prod = reciprocalB * final_manA` (14x11 multiply)
2. `trial_A = q_trial * final_manB` (15x11 multiply + 26-bit subtract for `diff`)
3. Cascaded signed comparator tree (6 branches) to select `q_final`

### Synthesis flow

```
Yosys 0.66 -> ABC (strash + dc2 + dch + timing-driven map)
          -> OpenSTA 3.1.0 for timing/power analysis
```

Default ABC mapping produces a 19.61 ns combinational critical path. Adding ABC's timing-driven gate sizing (`-D` + `-constr` flags) triggers `upsize`/`dnsize`/`buffer` passes that replace weak drive-strength cells with faster `_2`/`_4` variants, reducing the path by 27% to 14.32 ns.

### Pipelining: 14.32 ns -> 8.39 ns

Breaking past the 14.32 ns combinational limit required splitting the datapath with pipeline registers:

- **FDIV**: 4-stage pipeline -> longest stage 8.39 ns (119.3 MHz)
- **FMUL**: 1-stage pipeline -> longest stage 7.78 ns (128.6 MHz)
- **FADDSUB**: 1-stage pipeline -> longest stage 8.39 ns (119.2 MHz)

All modules verified exhaustively (every combination of two half-precision inputs) with Verilator: 0 mismatches vs. the golden model across NaN/Inf/zero/subnormal/normal categories. The combined `fpu_test` top-level was also verified with an IEEE-754-aware golden model over all 4 operations (ADD/SUB/MUL/DIV), every `(a,b)` input pair, and NaN-tolerant comparisons: **0 failures across 4.29B combos per operation** (NaN 1.06B, Inf 1.0M, Zero 1.0M, Subnormal 1.02B, Normal 15.1B).

### IEEE-754 Compliance Fixes

Two cross-cycle contamination bugs in the pre-existing FADDSUB/FMUL datapaths (not introduced by the FDIV pipelining) were fixed so the exhaustive check above genuinely passes:

- **FADDSUB** (`src/fpu_FADDSUB.sv`): the normalization stage combined the *registered* mantissa of one input set with the *unregistered* exponent and special-case flags of the next set, corrupting results whenever the input exponents or NaN/Inf flags changed between cycles (visible even for all-normal operands). `final_exp` and the six special flags (`nanA/nanB/infinA/infinB/A0/B0`) are now registered in the same `always_ff` as `pre_norm_man`, so every pipeline stage operates on one consistent input set.
- **FADDSUB inf - inf**: now returns NaN only when the effective signs are equal (e.g. `+Inf - (+Inf)`); opposite-sign infinities (`+Inf - (-Inf)`) correctly return `+/-Inf` per IEEE-754.
- **FMUL** (`src/fpu_FMUL.sv`): the arithmetic result's sign bit was taken from the *current* input while the product came from the registered stage, so the sign of a result could be stamped from an unrelated input in a changing stream (also broke sign-of-zero on underflow). `ans_corrected_0[15]` now uses the registered `sign_bit_reg`.

Verification of the fixes: alternating-exponent stream probe (previously 7/7 mismatches) now 0; IEEE special-vector suite (1,296 vectors) 0 fails; 268M-check quick pass 0 fails; and the full exhaustive run over all 4 ops and all `(a,b)` pairs — **17.2B checks, 0 failures**.

## FDIV Power Reduction: 18.5 mW -> 15.2 mW

Restructured the FDIV correction stage from a 7-way if/else chain with 5 adders and a mux to parallel predicate encoding with a single shared adder (see `src/fpu_FDIV.sv`):

- Standalone FDIV: **11.2 mW** (-2.8 mW, -20%)
- Combined `fpu_test`: **15.2 mW** (-3.3 mW, -18%)
- Area: 31,764 -> **31,620 um^2** (-144 um^2, -0.5%)
- Slack: 2.577 ns -> 1.945 ns (still MET at 100 MHz)

The correction stage was the dominant power sink (~18% of FDIV power); the minor timing penalty (0.51 ns path regression) is acceptable for the power saving. Verified: combinatorial equivalence over all 67M `(diff, B)` pairs (0 mismatches) and exhaustive IEEE-754 verification (~17B combos, 0 failures). Final numbers after the later 4th FDIV pipeline stage and the IEEE-compliance fixes are in the PPA table above (combined 9.85 mW).

### Attempts that did NOT improve timing (combinational)

| Attempt | Result |
|---------|--------|
| Carry-select adder for `diff` (4-bit blocks) | 15.97 ns (worse) |
| Parallel comparator tree (replace if-else cascade) | 14.80 ns (worse) |
| Yosys `opt -full` + ABC | 14.32 ns (identical) |
| Custom ABC delay scripts (balance/rewrite) | identical or failed |
| ABC `if` mapper | unavailable in Yosys 0.66 |

ABC's AIG-based flatten-and-remap approach does not preserve RTL microarchitectural hints (carry-select, parallel comparators). The 14.32 ns fixed point is a fundamental limit of the algorithm's logic depth in Sky130.

### Beyond 8.39 ns

- Use a different division algorithm (Goldschmidt, SRT)
- Add more pipeline stages or a faster reciprocal ROM
- Move to a faster technology node

## CPU Integration (PicoRV32 + PCPI)

A PicoRV32 SoC is set up so the half-precision FPU can be attached as a Pico
Co-Processor (PCPI) and driven by RISC-V `custom0` instructions. The core,
memory, toolchain, and Verilator harness are in place; the FPU PCPI wrapper
(`src/fpu_pcpi.sv`) is the plug-in point.

| File | Description |
|------|-------------|
| `third_party/picorv32.v` | PicoRV32 core (YosysHQ, vendored, ISC license) |
| `tb/soc_fpu_top.sv` | SoC: PicoRV32 + 16 KB RAM + PCPI bus. Built with `HAS_FPU_PCPI` it instantiates the wrapper; without it the PCPI handshake inputs are tied off |
| `tb/tb_fpu_pcpi.cpp` | Verilator harness: resets the core, runs to a done marker, checks results vs a golden `_Float16` model |
| `tb/firmware/` | Bare-metal firmware: `test_main.c` (integer smoke test), `fpu_test_main.c` (FPU test), `fpu_macros.h`, `link.ld`, `Makefile`, `bin2hex.py` |
| `run_cpu_test.sh` | One-shot build + run |

### Custom-instruction encoding

The FPU is reached through RISC-V `custom0` (opcode `0001011`), R-type with
`funct3 = 000`. `funct7` selects the operation; operands are half-precision
bit patterns in the low 16 bits of `rs1`/`rs2`, and the 16-bit result is
zero-extended into `rd`.

| funct7 | mnemonic |
|--------|----------|
| `0000110` | FADD |
| `0000111` | FSUB |
| `0001000` | FMUL |
| `0001001` | FDIV |

(The `0000000`–`0000101` funct7 values are reserved by PicoRV32's own IRQ
custom instructions.)

### Toolchain

The firmware is built with `clang --target=riscv32-unknown-elf -march=rv32i
-mabi=ilp32` + `ld.lld` + `llvm-objcopy` (no GNU toolchain required). If
`riscv64-unknown-elf-gcc` is installed it is auto-detected and used instead.

### Building and running

```
./run_cpu_test.sh            # baseline integer smoke test (no coprocessor)
./run_cpu_test.sh fpu        # FPU PCPI test (requires src/fpu_pcpi.sv)
```

Baseline result: firmware runs on the core, writes its results and done
marker through the native memory interface, and the harness verifies them —
**STATUS: PASS** (4/4 integer checks, ~71 cycles to completion, no trap).

### Connecting the FPU PCPI wrapper

Write `src/fpu_pcpi.sv` with exactly this interface:

```systemverilog
module fpu_pcpi (
    input  logic clk, resetn,
    input  logic        pcpi_valid,
    input  logic [31:0] pcpi_insn,
    input  logic [31:0] pcpi_rs1,
    input  logic [31:0] pcpi_rs2,
    output logic        pcpi_wr,
    output logic [31:0] pcpi_rd,
    output logic        pcpi_wait,
    output logic        pcpi_ready
);
```

Handshake notes (see `picorv32_pcpi_mul` in `third_party/picorv32.v` for the
reference pattern):

- `pcpi_valid` is held high by the CPU until the coprocessor asserts
  `pcpi_ready`; `pcpi_insn/rs1/rs2` are stable during that time, so the
  4-cycle FPU pipeline can be fed continuously and the result sampled after
  the pipeline fills.
- Assert `pcpi_wait` as soon as the instruction is decoded to suppress the
  CPU's 16-cycle illegal-instruction timeout.
- Assert `pcpi_ready` together with `pcpi_wr` and `pcpi_rd = {16'b0, fpu_ans}`
  for one cycle when the result is valid; the CPU writes `rd` and moves on.
- Register `pcpi_wait` and detect its rising edge to latch each fresh
  instruction without re-triggering.

`fpu_macros.h` provides `fadd_half/fsub_half/fmul_half/fdiv_half` and
`fpu_test_main.c` is a ready 15-vector test (NaN/Inf/zero/subnormal/normal)
whose expected results are checked by the harness.

## Tools

- **RTL**: SystemVerilog
- **Synthesis**: Yosys 0.66 + ABC (Sky130 cell library)
- **Timing/Power**: OpenSTA 3.1.0
- **Simulation**: Verilator / Icarus Verilog
- **PDK**: SkyWater 130 nm (`sky130_fd_sc_hd`)
- **CPU firmware**: clang/LLD or `riscv64-unknown-elf-gcc` (RV32I, bare-metal)

## Project Structure

```
.
├── src/
│   ├── fpu_FMUL.sv
│   ├── fpu_FADDSUB.sv
│   ├── fpu_FDIV.sv
│   ├── fpu_modules.sv
│   └── fpu_test.sv         # Combined top: FMUL + FADDSUB + FDIV
├── third_party/
│   └── picorv32.v          # PicoRV32 RISC-V core (vendored)
├── tb/                     # Testbenches (tb_fpu.cpp = exhaustive)
│   ├── tb_fpu_pcpi.cpp     # PicoRV32 + FPU-PCPI SoC harness
│   ├── soc_fpu_top.sv      # PicoRV32 + RAM + PCPI SoC top
│   └── firmware/           # Bare-metal test firmware (C, RV32I)
├── synth_scripts/          # Yosys/OpenSTA PPA scripts
│   ├── ppa_DIV.ys
│   ├── sta_DIV.tcl
│   ├── ppa_FMUL.ys
│   ├── sta_FMUL.tcl
│   ├── ppa_combined_top.ys # Combined fpu_test synthesis
│   ├── sta_fpu_test.tcl    # Combined fpu_test timing/power
│   └── ...
├── synth_outputs/          # Synthesized netlists and reports
├── run_ppa.sh              # One-shot combined PPA flow
├── run_cpu_test.sh         # One-shot PicoRV32 + FPU-PCPI sim flow
└── testing_results/
```
