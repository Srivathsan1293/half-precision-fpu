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

## PPA (Sky130 @ 25C, 1.80 V)

### Area

| Module | Cells (plain) | Cells (timing-driven) | Delta |
|--------|---------------|----------------------|-------|
| FMUL | 1,047 | 1,086 | +39 |
| FADDSUB | 647 | 672 | +25 |
| FDIV | 2,335 | 2,412 | +77 |
| **Total** | **4,029** | **4,170** | **+141** |

### Timing (combinational, virtual 10 ns clock, ABC timing-driven sizing)

| Module | Plain abc | abc -D 500 -constr | Max Freq | Slack |
|--------|-----------|-------------------|----------|-------|
| FMUL | 13.44 ns | **10.54 ns** | 94.9 MHz | –0.54 ns |
| FADDSUB | 13.28 ns | **10.39 ns** | 96.2 MHz | –0.39 ns |
| FDIV | 19.61 ns | **14.32 ns** | 69.8 MHz | –4.32 ns |

### Power (10% toggle rate)

| Module | Internal | Switching | Total |
|--------|----------|-----------|-------|
| FMUL | 1.25 mW | 1.21 mW | **2.46 mW** |
| FADDSUB | 0.74 mW | 0.82 mW | **1.56 mW** |
| FDIV | 42.60 mW | 38.87 mW | **81.4 mW** |

## FDIV Optimization: 19.61 ns -> 14.32 ns

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

Default ABC mapping produces a 19.61 ns critical path. Adding ABC's timing-driven gate sizing (`-D` + `-constr` flags) triggers `upsize`/`dnsize`/`buffer` passes that replace weak drive-strength cells with faster `_2`/`_4` variants, reducing the path by 27%.

### Attempts that did NOT improve timing

| Attempt | Result |
|---------|--------|
| Carry-select adder for `diff` (4-bit blocks) | 15.97 ns (worse) |
| Parallel comparator tree (replace if-else cascade) | 14.80 ns (worse) |
| Yosys `opt -full` + ABC | 14.32 ns (identical) |
| Custom ABC delay scripts (balance/rewrite) | identical or failed |
| ABC `if` mapper | unavailable in Yosys 0.66 |

ABC's AIG-based flatten-and-remap approach does not preserve RTL microarchitectural hints (carry-select, parallel comparators). The 14.32 ns fixed point is a fundamental limit of the algorithm's logic depth in Sky130.

### To break past 14.32 ns

- Add pipeline registers inside the quotient refinement loop
- Use a different division algorithm (Goldschmidt, SRT)
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
