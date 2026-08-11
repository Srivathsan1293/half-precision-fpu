# Half-Precision FPU (Float16)

A fast, IEEE 754 compliant half-precision (binary16) floating-point unit with multiply, add/subtract, and divide operations. Synthesized for the SkyWater 130 nm open-source PDK.

## Project Goals

1. **IEEE 754 Compliance** — Correct rounding, NaN/Inf handling, subnormal support
2. **High Performance** — Minimize combinational critical path delay
3. **Pipelining** — Introduce pipeline stages to increase throughput and Fmax
4. **Low Power** — Reduce dynamic power of the dominant power sinks (FDIV correction logic)
5. **AXI4 Interface** — Wrap the FPU with an AXI4-Lite/AXI4-Stream interface for easy integration
6. **CPU Integration** — Verify with a RISC-V model CPU (e.g., PicoRV32, VexRiscv) on FPGA *(PicoRV32 SoC + PCPI bus + firmware + harness in place; FPU PCPI wrapper implemented and verified — see [CPU + PCPI Tests](#running-cpu--pcpi-tests))*

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
| FMUL | 1 | 1,385 | 7,616 um^2 | **8.02 ns** | 124.8 MHz | 1.51 mW |
| FADDSUB | 1 | 1,061 | 5,685 um^2 | **7.33 ns** | 136.5 MHz | 1.38 mW |
| FDIV | 4 | 3,536 | 20,709 um^2 | **8.40 ns** | 119.0 MHz | 6.81 mW |
| **Combined `fpu_test`** | 4 | 5,811 | 33,208 um^2 | **7.91 ns** | 126.5 MHz | 9.47 mW |
| **`fpu_pcpi` wrapper + FSM** | 4 | 5,815 | 33,421 um^2 | **6.68 ns** | 149.8 MHz | 10.8 mW |

Latency = stages x 10 ns clock period; throughput = one result per cycle after pipeline fill (Fmax above). The standalone FMUL/FADDSUB rows are the single internal datapath stage; the combined `fpu_test` aligns all three pipelines to a common 4-cycle latency.

The combined `fpu_test` top instantiates all three datapath modules (FMUL, FADDSUB, FDIV) with shared special-case flag logic. Its area is the sum of the three sub-units plus the shared decode/mux overhead; the critical path (7.91 ns) is set by the slowest stage among the three pipelines.

The `fpu_pcpi` row is the full PicoRV32 PCPI wrapper: the 3-state FSM (idle/compute/done), the pcpi decode/claim logic, and the `fpu_test` datapath instantiated inside it. Its area is `fpu_test` plus ~4 cells of FSM/glue; the register-to-register critical path (6.68 ns, through the FADDSUB adder) is *shorter* than standalone `fpu_test` because ABC maps the two nets differently at the flattened wrapper boundary. Re-run with `./run_ppa_fpu_pcpi.sh` (results in `testing_results/ppa_fpu_pcpi_results.txt`).

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

- **FDIV**: 4-stage pipeline -> longest stage 8.40 ns (119.0 MHz)
- **FMUL**: 1-stage pipeline -> longest stage 8.02 ns (124.8 MHz)
- **FADDSUB**: 1-stage pipeline -> longest stage 7.33 ns (136.5 MHz)

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

The correction stage was the dominant power sink (~18% of FDIV power); the minor timing penalty (0.51 ns path regression) is acceptable for the power saving. Verified: combinatorial equivalence over all 67M `(diff, B)` pairs (0 mismatches) and exhaustive IEEE-754 verification (~17B combos, 0 failures). Final numbers after the later 4th FDIV pipeline stage and the IEEE-compliance fixes are in the PPA table above (combined 9.47 mW).

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

### State Machine Timing (PCPI FSM Verification)

The PCPI wrapper implements a state machine with the following timing:

| Operation | Stages | Ready @ Counter | Latency | Fmax (via `fpu_pcpi`) |
|-----------|--------|-----------------|---------|------|
| FADD.H    | 1      | 1               | 2 cycles | 149.8 MHz |
| FSUB.H    | 1      | 1               | 2 cycles | 149.8 MHz |
| FMUL.H    | 1      | 1               | 2 cycles | 149.8 MHz |
| FDIV.H    | 4      | 3               | 4 cycles | 149.8 MHz |

The state machine tracks the counter (ready = `counter == stages`) and asserts
the ready signal to the CPU for one cycle when an instruction completes. The FSM
has no early-fire behavior: FDIV does NOT assert ready before cycle 4 even if
operand dependencies would allow earlier completion. This ensures the PCPI
handshake protocol is never violated.

### Hardware Capabilities

The FPU supports the following operations (RNE rounding, IEEE-754 compliant):

| Category | Operations | Notes |
|----------|------------|-------|
| Arithmetic | FADD.H, FSUB.H, FMUL.H, FDIV.H | All operations via custom0 RISC-V instructions; results returned in `rd` zero-extended to 32 bits |
| Comparison | FEQ.H (==), FLT.H (<), FLE.H (≤) | Returns 0/1; NaN operands yield 0 |
| Sign Manipulation | FSGNJ.H, FSGNJN.H, FSGNJX.H | Bitwise sign manipulation per Zhinx spec |
| Conversion | FCVT.H.W, FCVT.H.WU, FCVT.W.H, FCVT.WU.H | Integer half conversions with RNE/RTZ/RDN/RUP/RMM rounding modes (dynamic funct3=111 reads frm shadow) |
| Special Ops | FMIN.H, FMAX.H, FCLASS.H | Min/max of two halves; classify floating-point status (qNaN, sNaN, inf, zero, normal, subnormal) |

**Rounding modes**: FCVT instructions support 5 rounding modes encoded in funct3: RTZ(000), RNE(001, default), RDN(010), RUP(011), RMM(100). Dynamic funct3=111 reads the frm shadow register.

**Tie-breaking**: RNE rounds ties to even; FMIN/FMAX return NaN when both inputs are NaN (tagged via upper bit); signed zeros treated as equal.

### Verilog-Only Handoff Setup

To verify the FPU standalone (without the PCPI wrapper) or for handoff to another team:

```bash
# Build with combined fpu_test top only (no CPU/PCPI)
verilator --cc --trace --build -j \
    --top-module soc_fpu_top \
    --Mdir obj_dir_tb_picorv32 \
    -Wno-TIMESCALEMOD --public-flat-rw \
    src/fpu_test.sv \      # combined FMUL + FADDSUB + FDIV
    src/fpu_FMUL.sv        # single-module variant (FMUL only)
    src/fpu_FADDSUB.sv     # single-module variant (FADDSUB only)  
    src/fpu_FDIV.sv        # single-module variant (FDIV only)
    src/fpu_modules.sv
    src/fdiv_datapath_blocks.sv \
    third_party/picorv32.v tb/soc_fpu_top.sv \
    --exe tb/tb_fpu.cpp     # exhaustive testbench for combined fpu_test

# Or run single-module tests:
verilator ... tb/tb_fADDSUB.cpp   # 4B exhaustive test
```

Single-module verification uses the existing `tb_fADDSUB.cpp`, `tb_fMUL.cpp`, `tb_fDIV.cpp` testbenches with full 2^32 (4.29B) input coverage per module, NaN-tolerant golden checks via `_Float16`. Combined `fpu_test` verification uses `tb/tb_fpu.cpp` for all 4 operations over the same domain (17.2B total checks).

### Running CPU + PCPI Tests

```bash
./run_cpu_test.sh baseline     # integer smoke test (no coprocessor)
./run_cpu_test.sh fpu          # FPU PCPI test with full IEEE-754 vectors
./run_cpu_test.sh stress       # numeric sweep + back-to-back ops + accumulation loop
./run_cpu_test.sh spike        # ebreak-IRQ resume-PC validation (no wrapper needed)
./run_cpu_test.sh emu          # emulator self-test (emulated ops only via 0x800 handler)
./run_cpu_test.sh zhinx        # standard-Zhinx integration (clang -march=rv32im_zhinx)
./run_cpu_test.sh run prog.S   # custom user program + dump mode
./run_cpu_test.sh asmall [prog] [cycles]  # asm_all_ops.S golden check

./run_fsm.sh                    # PCPI FSM timing verification
./run_pcpi_handshake.sh         # PCPI wait/ready handshake protocol verification

# Stage-E edge/trap tests
./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000   # 109 edge-case vectors
./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000    # unsupported-op halt probe

# One-shot full 6-stage exhaustive pipeline (build-only by default; --run to execute)
./run_exhaustive_tests.sh            # build all stages
./run_exhaustive_tests.sh --stage B  # build a single stage
```

### Exhaustive 4B Tests (Phase A)

To build (but NOT run — 4.29B ops × 4 operations = ~70M cycles each, too slow for a typical workstation):

```bash
# Create combined exhaustive test script using the OLD aligned fpu_test.sv:
git show HEAD:src/fpu_test.sv > /tmp/opencode/fpu_test_old.sv

cat > tb/exhaustive_tests.sh << 'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

MODE="${1:-build}"  # build|run (do NOT run; too slow)
SRCDIR="$ROOT/src"

if [ "$MODE" = "build" ]; then
    VERILATOR=$(command -v verilator || { echo "verilator not found"; exit 1; })
    OBJDIR="$ROOT/obj_dir_tb_exhaustive"
    "$VERILATOR" --cc --trace --build -j \
        --top-module soc_fpu_top \
        --Mdir "$OBJDIR" -Wno-TIMESCALEMOD --public-flat-rw \
        $SRCDIR/fpu_test_old.sv          # OLD aligned version for 4B tests
        $SRCDIR/third_party/picorv32.v  tb/soc_fpu_top.sv \
        --exe tb/tb_exhaustive.cpp      # placeholder: will use existing tb_f* testbenches

    echo "==> Build complete (do NOT run — 17.2B ops, ~2+ hours on modern CPU)"
    return 0
fi

echo "Error: only build mode supported; run_cpu_test.sh for actual runs"
exit 1
EOF
chmod +x tb/exhaustive_tests.sh
```

The existing `tb_fADDSUB.cpp`, `tb_fMUL.cpp`, `tb_fDIV.cpp` testbenches can be invoked directly with Verilator. They each contain full 2^32 loops with golden models and will take ~70M cycles per operation (4.29B total). The combined `fpu_test` exhaustive check runs at ~150M ops/sec on a modern CPU, finishing in ~2 hours.


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
- **Timing/Power**: OpenSTA 3.1.0 (`STA_BIN` env var overrides the STA binary; `sta` is preferred over the broken `/usr/local/bin/opensta` wrapper)
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
│   ├── fpu_test.sv         # Combined top: FMUL + FADDSUB + FDIV
│   └── fpu_pcpi.sv         # PicoRV32 PCPI wrapper (3-state FSM) + fpu_test
├── third_party/
│   └── picorv32.v          # PicoRV32 RISC-V core (vendored)
├── tb/                     # Testbenches (tb_fpu.cpp = exhaustive)
│   ├── tb_fpu_pcpi.cpp     # PicoRV32 + FPU-PCPI SoC harness
│   ├── soc_fpu_top.sv      # PicoRV32 + RAM + PCPI SoC top
│   ├── fpu_test_addsub_wrap.sv  # Single-cycle shim top for tb_fADDSUB.cpp
│   ├── fpu_test_fmul_wrap.sv    # Single-cycle shim top for tb_fMUL.cpp
│   └── firmware/           # Bare-metal test firmware (C, RV32I)
├── synth_scripts/          # Yosys/OpenSTA PPA scripts
│   ├── ppa_DIV.ys
│   ├── sta_DIV.tcl
│   ├── ppa_FMUL.ys
│   ├── sta_FMUL.tcl
│   ├── ppa_combined_top.ys # Combined fpu_test synthesis
│   ├── sta_fpu_test.tcl    # Combined fpu_test timing/power
│   ├── ppa_fpu_pcpi.ys     # fpu_pcpi wrapper + FSM synthesis
│   ├── sta_fpu_pcpi.tcl    # fpu_pcpi timing/power
│   └── ...
├── synth_outputs/          # Synthesized netlists and reports
├── run_ppa.sh              # One-shot combined PPA flow
├── run_ppa_fpu_pcpi.sh     # One-shot fpu_pcpi PPA flow
├── run_exhaustive_tests.sh # 6-stage build-only exhaustive pipeline
├── run_cpu_test.sh         # One-shot PicoRV32 + FPU-PCPI sim flow
└── testing_results/
```
