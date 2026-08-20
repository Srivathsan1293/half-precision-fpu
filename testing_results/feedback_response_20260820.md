# Feedback Response — Multi-Corner STA, Power Characterization & FLOPS/W Fix

Response to the external review of the final report (`feedback.txt`). Covers the
three items scoped for this revision:

1. **Multi-corner STA** — setup signoff at the SS corner, hold at FF.
2. **Comprehensive power characterization** — leakage vs dynamic, activity
   sensitivity, and real-workload (VCD) toggle rates.
3. **FLOPS/W correction** — the published 2.78 GFLOPS/W was a units bug.

Items the review raised but which are explicitly **out of scope** for this
revision (recorded as future work): integrated clock gating (ICG), IR-drop /
PDN / EM analysis, and a non-blocking (AXI4-Stream/TileLink) interface +
FMADD.H. Rationale: ICG and the interface/FMA are design changes that would
invalidate the exhaustive-verification record and require a full re-PnR;
IR-drop/EM need commercial-grade tooling not present in the OpenLane flow.

---

## 1. Multi-corner STA

### 1.1 Pre-route (OpenSTA on the shipped tt-synthesized netlist)

Netlist: `fpu_pcpi`, Yosys + ABC `map -D 4000` (the same netlist as the FLOPS
report). Measured on `sky130_fd_sc_hd` at each corner lib, IO delays 0, activity 0.1:

| Corner | lib | LTP (ns) | Fmax (1/LTP) MHz | WNS @ 10 ns | Timing-closed Fmax |
|--------|-----|---------:|-----------------:|------------:|-------------------:|
| Slow | `ss_100C_1v60` | 15.03 | 66.6 | **−5.03 (violated)** | ~66.5 MHz (clk 15 ns) |
| Typ. | `tt_025C_1v80` | 7.31 | 136.8 | +2.57 (met) | **125.0 MHz (clk 8 ns)** |
| Fast | `ff_n40C_1v95` | 4.50 | 222.3 | +5.43 (met) | 200.0 MHz (clk 5 ns) |

Implication (as the review predicted): a **single** 100 MHz / 10 ns claim is
corner-dependent. At the worst-case slow corner the setup path needs ~15 ns,
so the honest statement is "100 MHz @ tt, 66 MHz @ ss, 200 MHz @ ff"
(process-corner table, §README). Hold at the FF corner is only meaningful on
the routed netlist (clock skew), see §1.2.

### 1.2 Post-PnR (OpenLane run `designs/fpu_pcpi/runs/pnr_run16`, CLOCK_PERIOD 10 ns)

OpenLane's signoff STA reports the routed design at all nine PVT corners
(max/nom/min × ss/tt/ff). `final/metrics.json` (`timing__setup__wns__corner:*`,
`timing__hold__wns__corner:*`, `*_vio__count__corner:*`):

| Corner | Setup WNS @ 10 ns | Setup vio | Hold WNS | Hold vio | Max slew vio | Implied Fmax |
|--------|------------------:|----------:|---------:|---------:|-------------:|-------------:|
| **ss** `max_ss_100C_1v60` | **−7.16** | 64 | +0.88 | 0 | 42 | **~58 MHz** (setup-limited) |
| **tt** `nom_tt_025C_1v80` | **+1.02** | 0 | +0.32 | 0 | 0 | **100 MHz (closed)** |
| **ff** `max_ff_n40C_1v95` | +4.24 | 0 | **+0.10** | **0** | 0 | ~174 MHz (hold clean) |

(t is `max_`/`nom_`/`min_` — the SS row quotes the worst of the three.)

Conclusions:

- The **10 ns / 100 MHz headline holds at tt** (setup +1.02 ns, hold +0.32 ns,
  matching the README's run15 numbers; the new run reproduces 4,137 cells /
  31,290 µm² / 8.19 mW exactly).
- The **SS corner is setup-limited to ~58 MHz** at this 10 ns target — this is
  exactly the review's concern, and is now quantified on the routed netlist
  (pre-route LTP said ~66 MHz; the routed clock tree + wire delay tighten it).
  To ship a genuine "100 MHz @ all corners" claim the design would need
  re-synthesis/place for the SS corner (larger area) — recorded as future work.
- The **FF corner is hold-clean** (hold WNS +0.10 ns, 0 violations), the
  standard fast-fast hold check passes on the routed tree — answering the
  review's "hold signoff at ff" point.
- **Bonus — power grid / IR drop** (review item 4, partial): OpenLane's
  `Checker.PowerGrid` passed; worst `VPWR` drop is **1.09 mV (0.06 % of 1.8 V)**
  and `VGND` drop 1.13 mV, on a die this small. Full IR-drop *maps* are outside
  the OpenLane flow, but the grid-level check is clean and the drop is
  negligible.

---

## 2. Power characterization

### 2.1 Leakage vs dynamic (pre-route, tt, clk 8 ns = timing-closed point)

| Activity | Internal (mW) | Switching (mW) | Leakage (nW) | Total (mW) |
|---------:|--------------:|---------------:|--------------:|-----------:|
| 0.01 | 1.50 | 0.39 | 9.1 | 1.89 |
| 0.05 | 2.38 | 1.42 | 9.1 | 3.80 |
| 0.10 | 3.21 | 2.42 | 9.1 | **5.63** |
| 0.20 | 4.29 | 3.60 | 9.1 | 7.89 |
| 0.50 | 6.84 | 6.15 | 9.1 | 13.00 |

- **Leakage is 9.1 nW** (< 0.1 µW) — negligible, confirming the original
  `< 0.1 mW` note is itself conservative by 4+ orders of magnitude.
- Dynamic power scales **linearly** with activity (as expected for a
  combinational+register datapath with no clock gating).

### 2.2 Real-workload toggle rates (VCD from the actual SoC run)

Captured by adding `--vcd <path>` to `tb/tb_fpu_pcpi.cpp` (Verilator already
builds with `--trace`; the edit defers the VCD open until arg-parse so the
path is selectable and non-trace runs don't write the file). Toggle density of
the `fpu_pcpi` scope measured across the whole SoC run:

| Workload | FPU nets | Nets toggling | Total transitions | Clock cycles | **Avg toggle density** |
|----------|---------:|--------------:|------------------:|-------------:|----------------------:|
| benchmm (matmul) | 295 | 141 | 1,182,582 | 69,515 | **0.029** |
| benchdiv (vector div) | 295 | 141 | 1,768,269 | 93,953 | **0.032** |

Interpretation:

- The **workload-average activity is ≈ 0.03**, *below* the 0.1 default used
  for the headline power numbers — i.e. the published 8.19 mW (post-PnR) /
  5.63 mW (pre-route) is a **conservative upper bound** for these workloads.
- Mapping density → power (linear interpolation on §2.1): ≈ **2.9 mW** at the
  8 ns pre-route point, a large fraction of which is the SW soft-float phase
  where the hardware FPU is idle (its inputs are held stable by the PCPI FSM).
  The HW-only phase toggles far more.
- Between control-loop triggers the PCPI FSM holds operands static, so the
  "idle" datapoint is the 0.01 row (1.89 mW pre-route @ 8 ns) — the closest
  thing to clock-gated idle without adding ICG cells.

---

## 3. FLOPS/W fix (was a units bug)

`tools/flops_report.py` computed `FLOPS/W = Fmax[MHz] / pJ` = 125 / 45.04 =
2.78 — mixing MHz with pJ, silently dividing by ~1e3 twice. Correct form is
`Fmax[Hz] / P[W]` = `Fmax[MHz] / P[mW]`:

- Pre-route, timing-closed (125 MHz, 5.63 mW): **22.2 GFLOPS/W** (was 2.78).
- Post-PnR, 100 MHz / 8.19 mW: **12.2 GFLOPS/W** (matches the reviewer's own
  check `100e6 / 8.19e-3 / 1e9 = 12.21`).
- Sanity: 45 pJ/op ⇒ 1/45e-12 = 22.2 GFLOPS/W ✓ (self-consistent).

The `flops_report.py` formula is fixed; `run_flops.sh` was re-run and
regenerated `testing_results/flops_20260820_113346/flops_report.md` (now
22.20 GFLOPS/W). README tables updated accordingly.

---

## Artifacts

- `tools/flops_report.py` — FLOPS/W formula fix.
- `tb/tb_fpu_pcpi.cpp` — `--vcd <path>` support.
- `synth_scripts/ppa_fpu_pcpi_ff.ys`, `sta_fpu_pcpi_ff.tcl`,
  `sta_bench_common_ff.tcl` — FF-corner counterparts of the existing SS scripts.
- `sky130_fd_sc_hd__ss_100C_1v60.lib`, `sky130_fd_sc_hd__ff_n40C_1v95.lib`
  — committed alongside the shipped tt lib (copied from the PDK), so the
  corner STA is reproducible with no PDK install.
- `designs/fpu_pcpi/runs/fpu_pcpi_pnr_run16/` — fresh 10 ns OpenLane run
  (post-PnR metrics + corner STA).
- This document.