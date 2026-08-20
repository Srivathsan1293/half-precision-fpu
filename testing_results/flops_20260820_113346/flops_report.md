# fpu_pcpi FLOPS Report (fp16, Sky130)

- Run tree: `flops_20260820_113346`
- Top module: `fpu_pcpi` (PCPI wrapper + FSM + fpu_test datapath)
- Synthesis: Yosys + ABC, `map -D 4000` ps
- Timing/power: OpenSTA, activity 0.1
- Library: `sky130_fd_sc_hd__tt_025C_1v80.lib` (tt / 1.8 V / 25 C)
- Normalization: 1 GE = 3.7536 um2 (`nand2_1`); FO4 = 37.2 ps (absolute at 4x C_in = 57.7 ps)

## Process measurements (each axis measured separately)

| Metric | Value |
|--------|-------|
| Area (um2) | 23018.3
| Area (kGE) | 6.13
| Cells / flops | 3692 / 92
| LTP (ns) | 7.310
| LTP (FO4) | 197
| Fmax = 1/LTP (MHz) | 136.8
| Timing-closed Fmax (MHz) | 125.0 at clk 8 ns
| Total power @ closed (mW) | 5.63
| Energy/op (pJ) | 45.0

## Peak FLOPS (fp16, single-lane)

> `fpu_pcpi` is a *blocking* PCPI coprocessor: it retires one op per latency (FADD/FSUB/FMUL = 1 cyc, FDIV = 12 cyc). Peak is therefore interface-limited to `Fmax / latency`. The underlying `fpu_test` datapath is fully pipelined at 1 op/cycle, so the datapath peak is `Fmax`; the wrapper serializes it. Both are reported; the interface number is what the coprocessor can actually sustain.

| Variant | Formula | LTP-bound (MFLOP/s) | Timing-closed (MFLOP/s) |
|---------|---------|--------------------:|------------------------:|
| Datapath peak (any op) | Fmax | 136.8 | 125.0 |
| Interface peak FADD/FSUB/FMUL | Fmax / 1 | 136.8 | 125.0 |
| Interface peak FDIV | Fmax / 12 | 11.4 | 10.4 |

## Realized FLOPS (measured, SoC + firmware overhead included)

| Workload | FP ops (mul/add/div) | HW-phase cycles | Realized (MFLOP/s) | ops/cycle |
|----------|---------------------:|----------------:|-------------------:|----------:|
| benchmm | 144 (64/80/0) | 4717 | 3.82 | 0.0305 |
| benchdig | 156 (70/86/0) | 6538 | 2.98 | 0.0239 |
| benchdiv | 128 (0/64/64) | 5170 | 3.09 | 0.0248 |

> Realized FLOPS = FP ops x timing-closed Fmax / HW-phase cycles. The gap to the interface peak is firmware + loop + load/store overhead (each custom op costs 4 instructions in fpu_macros.h), not the FPU.

## Process-node-independent comparison metrics

| Metric | Value |
|--------|-------|
| ops/FO4 (= 1/LTP_fo4) | 0.0051
| FLOPS/GE (peak, LTP-bound) | 22.308 kFLOP/s/GE
| FLOPS/W (peak, closed) | 22.20 GFLOPS/W
| mW/MHz | 0.0450
| ADP (um2.ns) | 168264

> Raw FLOPS/MHz are NOT portable across process nodes. Compare designs with ops/FO4 (speed), kGE (area) and pJ/op (energy).
