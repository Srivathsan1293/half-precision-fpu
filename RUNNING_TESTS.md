# Running Tests & Programs

This guide covers how to install the prerequisites and run the three main test
harnesses plus your own assembly programs on the half-precision FPU + PicoRV32
SoC. All instructions are **Linux-only** and assume a Debian/Ubuntu-style system
(sudo available). Run every command from the project root
(`FPU_project/`).

---

## 1. Prerequisites

You need a hardware simulator (Verilator), a synthesis + STA flow for PPA, and a
RISC-V toolchain for building firmware.

### Debian / Ubuntu (apt)

```bash
sudo apt update

# Simulation + RTL
sudo apt install -y verilator

# Synthesis / timing / power (PPA) — run_bench.sh
sudo apt install -y yosys opensta

# RISC-V bare-metal firmware toolchain
# Either the GNU cross toolchain...
sudo apt install -y gcc-riscv64-unknown-elf
# ...or (alternative) clang + LLVM lld/objcopy
sudo apt install -y clang lld llvm

# FPNew SV->Verilog conversion (run_cycle_compare.sh / run_bench.sh fpnew)
sudo apt install -y sv2v

# General
sudo apt install -y python3 make
```

### Arch (pacman)

```bash
sudo pacman -S verilator yosys opensta sv2v clang lld python
# GNU RISC-V toolchain:
sudo pacman -S riscv64-elf-gcc
```

> **Note on `sv2v`**: it is only needed if you run the FPNEW comparison
> (`run_cycle_compare.sh` will otherwise skip FPNEW gracefully, and
> `run_bench.sh fpnew` requires it). If your distro lacks a package, install it
> via `cargo install sv2v` (Rust) or build from
> <https://github.com/zachjs/sv2v>.

### Verify the toolchain is available

```bash
verilator --version      # e.g. Verilator 5.x
yosys --version
sta --version            # or: opensta --version
clang --version          # or: riscv64-unknown-elf-gcc --version
```

The firmware `Makefile` auto-detects the toolchain: it prefers
`riscv64-unknown-elf-gcc`, otherwise falls back to `clang --target=riscv32-unknown-elf`
+ `ld.lld`. You only need one of them.

> Optional: the project ships a local Sky130 liberty file
> (`sky130_fd_sc_hd__tt_025C_1v80.lib`), so no PDK install is required.

---

## 2. `run_exhaustive_tests.sh` — six-stage exhaustive pipeline

The full verification suite. It rebuilds the four per-module exhaustive
testbenches and re-runs every CPU/PCPI/firmware suite from this phase.

| Argument | Effect |
|---|---|
| *(none)* | **Build-only.** Verilates/assembles everything but executes nothing. |
| `--run` | Actually execute every stage (Stage A is ~8.6e9 comparisons/op → takes hours). |
| `--stage N` | Run only one stage (`A`–`F`). Combine with `--run`. |

```bash
# Build-only (default, quick sanity that everything compiles):
./run_exhaustive_tests.sh

# Build + run a single stage (e.g. Stage B — stress/zhinx/asm_all):
./run_exhaustive_tests.sh --run --stage B

# Build + run the whole thing (very long):
./run_exhaustive_tests.sh --run
```

- Logs → `testing_results/fpu_exhaustive_log.txt`
- Failures → `testing_results/fpu_failed_log.txt`
- Exit status is non-zero if any stage fails.

---

## 3. `run_cycle_compare.sh` — SW vs HW cycle-count comparison

Runs three workloads (`benchmm` 4x4 fp16 matmul, `benchdig` 5-tap FIR,
`benchdiv` fp16 vector divide) and compares cycle counts between:

- **Software** soft-float baseline (`soft_half.h`)
- **Custom** hardware (this FPU + PicoRV32 PCPI wrapper)
- **FPNew** reference unit (only if `third_party/fpnew/` is present; skipped
  gracefully otherwise)

```bash
./run_cycle_compare.sh
```

- Per-workload logs → `testing_results/logs/*.log`
- Markdown report → `testing_results/cycle_comparison_<timestamp>.md`
  (printed to the console at the end too).

> The FPNEW runs are best-effort. If you haven't converted FPNew yet, the
> script notes the skipped FPNEW column rather than failing.

---

## 4. `run_bench.sh` — PPA (area / timing / power) benchmark

Synthesizes the selected top modules with Yosys and runs OpenSTA timing/power
analysis against the local Sky130 library. The first run derives normalization
constants from the liberty file via `tools/fo4.py`.

```bash
# All datapath + combined tops:
./run_bench.sh all

# Just a single module:
./run_bench.sh FMUL
./run_bench.sh FADDSUB
./run_bench.sh DIV
./run_bench.sh fpu_test        # combined FPU top
./run_bench.sh fpu_pcpi        # wrapper + FSM
./run_bench.sh fpnew           # reference unit (needs sv2v, separate arch)

# Tune synthesis / timing knobs:
./run_bench.sh all --dly 4000 --clk 7,8,9 --act 0.1
#   --dly  : ABC delay in ps (default 9000)
#   --clk  : target clock period(s) in ns, comma-separated (default 10)
#   --act  : signal activity factor for power (default 0.1)
```

- Needs `yosys` and `sta`/`opensta` on `PATH` (or set `STA_BIN=/path/to/opensta`).
- Reports → `testing_results/bench_<timestamp>/bench_report.md` (+ `bench.csv`,
  per-run `synth.log` / `sta.log`).

> `fpnew` is synthesized with a *different* (combinational vs pipelined)
> architecture, so pass it separately from the custom tops.

---

## 5. Running your own assembly / C program on the SoC

`run_cpu_test.sh run` builds a bare-metal RV32I image from your source file,
links it with the IRQ stub + software emulator (so both hardware FP ops and
emulated Zhinx ops work), simulates it in Verilator, then dumps the RAM and
register file to `testing_results/dump.txt`. No golden checks are performed.

```bash
# Run the bundled demo (default if you omit the program):
./run_cpu_test.sh run

# Run your own assembly program (limit sim to 20000 cycles):
./run_cpu_test.sh run myprog.S 20000

# Run your own C program:
./run_cpu_test.sh run myprog.c 20000
```

### Writing a program

Assembly (Zhinx holds fp16 values in the **integer** register file — no F
registers). See `tb/firmware/demo_zhinx.S` for a complete example; the pattern is:

```asm
    .section .text._start
    .globl _start
_start:
    .word 0x0600000B            # maskirq x0, 0   (unmask ebreak IRQ for emulated ops)
    li      t0, 0x3C00          # fp16 1.0
    li      t1, 0x4000          # fp16 2.0
    fadd.h  t2, t0, t1          # hardware op: t2 = 3.0 (0x4200)
    fcvt.w.h t3, t0             # emulated op: t3 = 1
    ...
1:  j       1b                  # halt (spin loop)
```

Hardware executes `fadd.h / fsub.h / fmul.h / fdiv.h`; everything else
(compares, sign-inject, FCLASS, FCVT, FMIN/FMAX) traps to the 0x800 emulator.

### Reading the dump

After the run, `testing_results/dump.txt` contains the final cycle count, the
GPR register file, and the RAM contents — use it to inspect your program's
results (the demo stores its outputs at address `0x1000`).

```bash
./run_cpu_test.sh run tb/firmware/demo_zhinx.S 20000
cat testing_results/dump.txt
```

---

## 6. Summary of the other helper scripts

These are invoked internally by the pipelines above, but are also runnable
standalone:

```bash
./run_cpu_test.sh fpu           # FPU PCPI IEEE-vector test
./run_cpu_test.sh stress        # numeric sweep + back-to-back ops + accumulation
./run_cpu_test.sh zhinx         # full Zhinx feature set (hw + emulator)
./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000   # edge-case sweep
./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000   # unsupported-op probe
./run_fsm.sh                    # PCPI FSM exact-timing verification
./run_pcpi_handshake.sh         # PCPI wait/ready handshake protocol
```

`run_cpu_test.sh` usage:

```bash
./run_cpu_test.sh [mode] [prog] [max_cycles]
# mode: baseline (default) | fpu | stress | bench | benchmm | benchdig |
#       benchdiv | spike | emu | zhinx | run | asmall
```

## 7. Keeping the tree clean

Build artifacts (`obj_dir_*`, `*.vcd`, `firmware.*`) are git-ignored, so you can
run the scripts repeatedly without worrying about committing transient output.
Reports under `testing_results/` are the intended artifacts to keep.