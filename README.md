# Half-Precision FPU (Sky130)

## Project Overview
Half-precision (FP16) Floating-Point Processor supporting IEEE 754 division (DIV), multiplication (MUL), and addition/subtraction (ADD/SUB).

## Current Status: Power Reduction Achieved

### PPA Results
| Metric        | Baseline   | After    | Change     |
|---------------|------------|----------|------------|
| **Area**      | 31,764 µm² | 31,620 µm² | **-144 µm²** (-0.5%) |
| **Slack**     | 2.577 ns   | 1.945 ns  | -0.63 ns (MET) |
| **Power**     | 18.5 mW    | 15.2 mW   | **-3.3 mW** (-18%) |

### Functional Verification
- Exhaustive IEEE-754 verification: **~17 billion combinations tested, 0 failures**
- All edge cases verified (NaN, Inf, Zero, Subnormal, Normal)
- Timing MET: Critical path 7.933 ns < 10 ns @ 100 MHz

### Recent Change: FDIV Correction Tree Restructuring
Restructured the FDIV correction stage from a 7-way if/else chain with multiple adders to parallel predicate encoding with single shared adder. This eliminated ~18% of power in the correction logic (dominant power sink). See `ppa_fpu_test_results.txt` for details.

## Directory Structure
- `src/fpu_test.sv` — Top module instantiating FADD, FMUL, FDIV
- `synth_scripts/` — Yosys scripts for synthesis and PPA analysis
- `synth_outputs/` — Synthesized netlists
- `tb/` — Testbenches (exhaustive + random)

## Tools Used
- Yosys 0.66 (ABC integration via mfs2 flow)
- OpenSTA 3.1.0 (timing/power analysis)
- Verilator 5.050 (functional verification)
