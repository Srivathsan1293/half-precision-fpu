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

### If you just pulled the repo from GitHub

The FPNew reference unit is **vendored into the repository** (`third_party/fpnew/`,
including its `common_cells` / `fpu_div_sqrt_mvp` submodules and the `vendor/`
trees — all ~249 files are committed). There are **no git submodules to
initialise**:

```bash
git clone https://github.com/Srivathsan1293/half-precision-fpu.git
cd half-precision-fpu
git submodule update --init --recursive   # optional: nothing to fetch, just a no-op
```

The only step needed beyond the prerequisites above is making sure `sv2v` is on
`PATH` (see the note above). Everything else — the Sky130 liberty file, Yosys
scripts, testbenches, firmware, and the vendored FPNew sources — ships with the
repo. You can then go straight to `./run_cycle_compare.sh` (section 3).

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

### FPNEW conversion for SW-HW testing

`run_cycle_compare.sh` measures the same firmware binary against the third-party
**FPNew** unit (through `src/fpnew_pcpi_adapter.sv`). FPNew is written in
SystemVerilog constructs that Yosys 0.66 cannot parse, so it is pre-converted to
plain Verilog with `sv2v` before Verilating the SoC. This happens **automatically**
inside `run_cycle_compare.sh` whenever `third_party/fpnew/` exists — you normally
don't need to run anything by hand.

What the script does per workload (`benchmm` / `benchdig` / `benchdiv`):

1. Builds the firmware with the matching `FPU_TEST=<workload>` model.
2. Calls `tools/sv2v_fpnew.sh obj_dir_tb_picorv32_fpnew_<workload>/fpnew_conv.v`
   to convert the eight vendored FPNew sources (+ `common_cells`, the
   `fpu_div_sqrt_mvp` divider, and the `vendor/cvw` fmalza block) into one
   flat Verilog file.
3. Verilates `soc_fpu_top` with the converted file, `src/fpnew_pcpi_adapter.sv`,
   and `-DHAS_FPU_PCPI`, then runs it and parses the `cycles:` line.

If you want to do the conversion by hand (e.g. to debug a failed FPNEW run, or
before using `run_bench.sh fpnew`):

```bash
# Install sv2v first (see section 1). Then:
tools/sv2v_fpnew.sh /tmp/fpnew_conv.v        # any output path works
ls -la /tmp/fpnew_conv.v                     # plain Verilog, consumed by the ys scripts
```

Failure modes and what to check:

- `sv2v: command not found` → install it (section 1). `tools/sv2v_fpnew.sh`
  falls back to a bundled path only if one exists on your machine; on a fresh
  clone you must provide it.
- The script writes `sv2v_stderr.tmp` next to the output (project root in the
  automatic flow) — that file holds the conversion warnings/errors.
- If the FPNEW Verilator build fails, `run_cycle_compare.sh` records the reason
  in `testing_results/logs/<workload>_fpnew.log` and the report shows `N/A` in
  the FPNEW column; the custom-FPU column is unaffected.

> `run_bench.sh fpnew` also uses the converted netlist, but it expects you to
> have produced one and to point the Yosys script at it (`FPNEW_V` token in
> `synth_scripts/ppa_fpnew.ys`). The automated SW-HW flow above is the supported
> path.

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

## 5. `run_flops.sh` — FLOPS measurement (peak + realized)

Measures the **FLOPS of the `fpu_pcpi` PCPI wrapper** (the module that
interfaces with the PicoRV32 CPU), self-contained — every input is measured
separately and combined into one report:

1. **Process/area** — Yosys + ABC synthesis of `src/fpu_pcpi.sv` (area / GE /
   cells / flops).
2. **Process/speed** — OpenSTA clock sweep (LTP → Fmax, timing-closed Fmax).
3. **Process/energy** — OpenSTA power at the timing-closed period (mW, pJ/op).
4. **System/cycles** — Verilator SoC runs of the existing firmware workloads
   (`benchmm` / `benchdig` / `benchdiv`), HW-phase cycle counts.
5. **Report** — combines them into peak (interface-limited and datapath) and
   realized FLOPS, plus process-node-independent FOMs (ops/FO4, kGE, pJ/op,
   FLOPS/W) so the numbers can be compared to other designs regardless of
   process node.

```bash
# Defaults: --dly 4000 --clks "10 9 8 7 6 5 4" --workloads benchmm benchdig benchdiv
./run_flops.sh

# Tune the synthesis / timing / workload selection:
./run_flops.sh --dly 9000 --clks "14 13 12 11 10"
./run_flops.sh --workloads benchmm benchdiv
#   --dly  : ABC delay in ps (default 4000)
#   --clks : clock period(s) in ns to try to close (default "10 9 8 7 6 5 4")
#   --act  : signal activity factor for power (default 0.1)
```

- Needs `yosys`, `sta`/`opensta` (or `STA_BIN=/path/to/opensta`), and `verilator`.
- Report → `testing_results/flops_<timestamp>/flops_report.md` (+ `flops.json`,
  per-run `synth.log` / `sta.log` / workload logs).

> **Why peak ≫ realized?** The `fpu_pcpi` FSM is *blocking*: it retires one op
> per latency (FADD/FSUB/FMUL = 1 cycle, FDIV = 4), so the interface peak is
> `Fmax/latency` (the underlying `fpu_test` datapath is a 1-op/cycle pipeline,
> reported separately). Realized FLOPS instead measures the whole SoC stack —
> each custom op costs ~30 extra cycles of firmware/loop/load-store overhead on
> the single-issue PicoRV32 (the FPU itself spends only 1–4 of them), which is
> why realized lands ~1/30th of peak. Compare **peak** against other FPU
> designs; compare **realized** only against other whole systems.

---

## 6. Post-PnR analysis — OpenLane physical design of the FPU

The `fpu_pcpi` wrapper is taken through the full OpenLane 2 PnR flow on the
Sky130A PDK (synthesis → floorplan → placement → CTS → routing → fill → RC
extraction → signoff STA, DRC, LVS). OpenLane 2.3.10 is **not bundled in the
repo** — install it as a native pip package or via the Docker image (see
Prerequisites below); the design sources/configs live under `designs/fpu_pcpi/`:

```
designs/fpu_pcpi/
├── config.json                         # baseline PnR config (CLOCK_PERIOD = 10 ns)
├── src/fpu_pcpi.v                      # sv2v-flattened RTL consumed by the flow
└── pareto/
    ├── config_{10,13,15,20,30}ns.json  # Pareto-sweep configs (clock targets)
    └── runs/                           # pareto_10ns .. pareto_30ns
```

### Prerequisites

- **OpenLane 2** — native `pip install openlane==2.3.10` (Python ≥ 3.10) or the
  matching Docker image `ghcr.io/efabless/openlane2:2.3.10`.
- **PDK** — sky130A via volare at `~/.volare`. The pre-route PPA flow uses the
  shipped liberty file, but the PnR flow needs the full PDK for its Magic/KLayout
  views and LVS.
- **KLayout + Magic** — invoked by the DRC/LVS steps; **matplotlib** for the
  plots. (Magic/KLayout need not be on `PATH` if you use the Dockerized flow.)

### Baseline PnR run

```bash
# Native (after pip install openlane==2.3.10):
openlane --pdk-root ~/.volare \
    --run-tag pnr_run16 designs/fpu_pcpi/config.json

# Dockerized alternative (mount project + PDK):
docker run --rm -v "$PWD":/work -v ~/.volare:/volare -w /work \
    -e PDK_ROOT=/volare ghcr.io/efabless/openlane2:2.3.10 \
    --pdk-root /volare --run-tag pnr_run16 designs/fpu_pcpi/config.json
```

- Run directory: `designs/fpu_pcpi/runs/fpu_pcpi_pnr_run16/` — 74 numbered step
  dirs plus `final/`. Pick a fresh `--run-tag` per run, or add `--overwrite` to
  rerun the same tag.
- The flow is signoff-clean when the run dir's `error.log` is empty and the
  checker steps (`checker-magicdrc`, `checker-klayoutdrc`, `checker-lvs`) pass.

### Pareto sweep across clock targets

```bash
cd designs/fpu_pcpi/pareto
for t in 10 13 15 20 30; do
    openlane --pdk-root ~/.volare \
        --run-tag pareto_${t}ns config_${t}ns.json
done
```

(Dockerized: use the `docker run ...` form above with `-w /work/designs/fpu_pcpi/pareto`.)

### Reading the signoff metrics

`final/metrics.json` holds each step's key metric. The ones used for PPA:

| Metric | `metrics.json` key | run15 (10 ns) |
|---|---|---|
| Standard cells | `design__instance__count` | 4,137 |
| Stdcell area | `design__instance__area` (µm²) | 31,290 |
| Total power | `power__total` (W, OpenROAD) | 8.19 mW |
| Setup WNS (tt) | `timing__setup__wns__corner:nom_tt_025C_1v80` | met (slack reported in the STA log) |
| Setup / hold violations | `timing__setup_vio__count__corner:...` / `timing__hold_vio__count__corner:...` | 0 |
| DRC (KLayout + Magic) | `checker-magicdrc` / `checker-klayoutdrc` step reports | 0 |
| LVS | `checker-lvs` / `68-netgen-lvs/reports/lvs.netgen.rpt` | 0 |

DRC/LVS detail reports live in the numbered step dirs, e.g.
`.../fpu_pcpi_pnr_run15/63-klayout-drc/reports/drc_violations.klayout.json`
and `.../68-netgen-lvs/reports/lvs.netgen.rpt`. `final/` also exports the GDS,
DEF, LEF, SDC, SDF, SPEF, spice, and the standard-cell `.lib` timing model.

### Post-PnR visualizations + Pareto plot

```bash
# Full routed die: KLayout screenshot of final/gds/fpu_pcpi.gds -> final/die.png
# (rendered in KLayout using the PDK tech file
#  ~/.volare/volare/sky130/versions/<ver>/sky130A/libs.tech/klayout/tech/sky130A.lyt)

# CTS clock-tree routing view from the final DEF:
python3 tools/render_cts.py \
    designs/fpu_pcpi/runs/fpu_pcpi_pnr_run15/final/def/fpu_pcpi.def \
    designs/fpu_pcpi/runs/fpu_pcpi_pnr_run15/final/cts.png

# Placement density heatmap from the final DEF:
python3 tools/placement_density_map.py \
    designs/fpu_pcpi/runs/fpu_pcpi_pnr_run15/final/def/fpu_pcpi.def \
    designs/fpu_pcpi/runs/fpu_pcpi_pnr_run15/final/density/placement_density.png

# Pareto curve (area + power vs clock target) from the sweep's metrics:
python3 tools/plot_pareto.py    # -> testing_results/pareto_curve/pareto_curve.png
```

The expected 5-point sweep (current RTL) is tabulated in the README's PPA
section; `SWaP_C_conclusion.md` frames the post-PnR numbers for the real-time
FOC/robotics use case.

---

## 7. Running your own assembly / C program on the SoC

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

## 8. Summary of the other helper scripts

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
./run_flops.sh                  # FLOPS of fpu_pcpi: peak + realized + cross-node FOMs
```

`run_cpu_test.sh` usage:

```bash
./run_cpu_test.sh [mode] [prog] [max_cycles]
# mode: baseline (default) | fpu | stress | bench | benchmm | benchdig |
#       benchdiv | spike | emu | zhinx | run | asmall
```

## 9. Keeping the tree clean

Build artifacts (`obj_dir_*`, `*.vcd`, `firmware.*`) are git-ignored, so you can
run the scripts repeatedly without worrying about committing transient output.
Reports under `testing_results/` are the intended artifacts to keep.
