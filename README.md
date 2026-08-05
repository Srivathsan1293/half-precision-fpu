# Half-Precision FPU (Float16)

A fast, IEEE 754 compliant half-precision (binary16) floating-point unit with multiply, add/subtract, and divide operations. Synthesized for the SkyWater 130 nm open-source PDK.

## Project Goals

1. **IEEE 754 Compliance** — Correct rounding, NaN/Inf handling, subnormal support
2. **High Performance** — Minimize combinational critical path delay
3. **Pipelining** — Introduce pipeline stages to increase throughput and Fmax
4. **AXI4 Interface** — Wrap the FPU with an AXI4-Lite/AXI4-Stream interface for easy integration
5. **CPU Integration** — Verify with a RISC-V model CPU (e.g., PicoRV32, VexRiscv) on FPGA

## Architecture

| Module | Description |
|--------|-------------|
| `fpu_FMUL.sv` | Booth-encoded Wallace tree multiplier |
| `fpu_FADDSUB.sv` | IEEE 754 compliant add/subtract |
| `fpu_FDIV.sv` | Iterative division via reciprocal ROM |
| `fpu_modules.sv` | Packed module definitions |

## PPA (Sky130 @ 25C, 1.80 V, pipelined)

Synthesis: Yosys + ABC (Sky130). Timing/Power: OpenSTA with a virtual 10 ns clock and 10% input toggle rate. FDIV area/power include the reciprocal ROM synthesized as a logic mux-tree (1,024-entry).

| Module | Stages | Cells | Area | Critical Path (LTP) | Fmax | Power |
|--------|--------|-------|------|---------------------|------|-------|
| FMUL | 2 | 1,036 | 7,471 um^2 | **7.44 ns** | 134.4 MHz | 1.65 mW |
| FADDSUB | 2 | 690 | 4,763 um^2 | **8.74 ns** | 114.4 MHz | 1.48 mW |
| FDIV | 3 | 2,926 | 19,596 um^2 | **8.61 ns** | 116.2 MHz | 15.5 mW |

Latency = stages x 10 ns clock period; throughput = one result per cycle after pipeline fill (Fmax above).

## FDIV Optimization: 19.61 ns -> 8.61 ns

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

### Pipelining: 14.32 ns -> 8.61 ns

Breaking past the 14.32 ns combinational limit required splitting the datapath with pipeline registers:

- **FDIV**: 3-stage pipeline -> longest stage 8.61 ns (116.2 MHz)
- **FMUL**: 2-stage pipeline -> longest stage 7.44 ns (134.4 MHz)
- **FADDSUB**: 2-stage pipeline -> longest stage 8.74 ns (114.4 MHz)

All modules verified exhaustively (every combination of two half-precision inputs) with Verilator: 0 mismatches vs. the golden model across NaN/Inf/zero/subnormal/normal categories.

### Attempts that did NOT improve timing (combinational)

| Attempt | Result |
|---------|--------|
| Carry-select adder for `diff` (4-bit blocks) | 15.97 ns (worse) |
| Parallel comparator tree (replace if-else cascade) | 14.80 ns (worse) |
| Yosys `opt -full` + ABC | 14.32 ns (identical) |
| Custom ABC delay scripts (balance/rewrite) | identical or failed |
| ABC `if` mapper | unavailable in Yosys 0.66 |

ABC's AIG-based flatten-and-remap approach does not preserve RTL microarchitectural hints (carry-select, parallel comparators). The 14.32 ns fixed point is a fundamental limit of the algorithm's logic depth in Sky130.

### Beyond 8.61 ns

- Use a different division algorithm (Goldschmidt, SRT)
- Add more pipeline stages or a faster reciprocal ROM
- Move to a faster technology node

## Tools

- **RTL**: SystemVerilog
- **Synthesis**: Yosys 0.66 + ABC (Sky130 cell library)
- **Timing/Power**: OpenSTA 3.1.0
- **Simulation**: Verilator / Icarus Verilog
- **PDK**: SkyWater 130 nm (`sky130_fd_sc_hd`)

## Project Structure

```
.
├── src/
│   ├── fpu_FMUL.sv
│   ├── fpu_FADDSUB.sv
│   ├── fpu_FDIV.sv
│   └── fpu_modules.sv
├── tb/                     # Testbenches
├── synth_scripts/          # Yosys/OpenSTA PPA scripts
│   ├── ppa_DIV.ys
│   ├── sta_DIV.tcl
│   ├── ppa_FMUL.ys
│   ├── sta_FMUL.tcl
│   └── ...
├── synth_outputs/          # Synthesized netlists and reports
└── testing_results/
```
