# fpu_pcpi FLOPS Report (fp16, Sky130)

- Run tree: `flops_20260814_161745`
- Top module: `fpu_pcpi` (PCPI wrapper + FSM + fpu_test datapath)
- Synthesis: Yosys + ABC, `map -D 4000` ps
- Timing/power: OpenSTA, activity 0.1
- Library: `sky130_fd_sc_hd__tt_025C_1v80.lib` (tt / 1.8 V / 25 C)
- Normalization: 1 GE = 3.7536 um2 (`nand2_1`); FO4 = 37.2 ps (absolute at 4x C_in = 57.7 ps)

## Process measurements (each axis measured separately)

| Metric | Value |
|--------|-------|
| Area (um2) | 34429.3
| Area (kGE) | 9.17
| Cells / flops | 6006 / 228
| LTP (ns) | 8.334
| LTP (FO4) | 224
| Fmax = 1/LTP (MHz) | 120.0
| Timing-closed Fmax (MHz) | 111.1 at clk 9 ns
| Total power @ closed (mW) | 11.30
| Energy/op (pJ) | 101.7

## Peak FLOPS (fp16, single-lane)

> `fpu_pcpi` is a *blocking* PCPI coprocessor: it retires one op per latency (FADD/FSUB/FMUL = 1 cyc, FDIV = 4 cyc). Peak is therefore interface-limited to `Fmax / latency`. The underlying `fpu_test` datapath is fully pipelined at 1 op/cycle, so the datapath peak is `Fmax`; the wrapper serializes it. Both are reported; the interface number is what the coprocessor can actually sustain.

| Variant | Formula | LTP-bound (MFLOP/s) | Timing-closed (MFLOP/s) |
|---------|---------|--------------------:|------------------------:|
| Datapath peak (any op) | Fmax | 120.0 | 111.1 |
| Interface peak FADD/FSUB/FMUL | Fmax / 1 | 120.0 | 111.1 |
| Interface peak FDIV | Fmax / 4 | 30.0 | 27.8 |

## Realized FLOPS (measured, SoC + firmware overhead included)

| Workload | FP ops (mul/add/div) | HW-phase cycles | Realized (MFLOP/s) | ops/cycle |
|----------|---------------------:|----------------:|-------------------:|----------:|
| benchmm | 144 (64/80/0) | 4717 | 3.39 | 0.0305 |
| benchdig | 156 (70/86/0) | 6538 | 2.65 | 0.0239 |
| benchdiv | 128 (0/64/64) | 4658 | 3.05 | 0.0275 |

> Realized FLOPS = FP ops x timing-closed Fmax / HW-phase cycles. The gap to the interface peak is firmware + loop + load/store overhead (each custom op costs 4 instructions in fpu_macros.h), not the FPU.

## Process-node-independent comparison metrics

| Metric | Value |
|--------|-------|
| ops/FO4 (= 1/LTP_fo4) | 0.0045
| FLOPS/GE (peak, LTP-bound) | 13.082 kFLOP/s/GE
| FLOPS/W (peak, closed) | 1.09 GFLOPS/W
| mW/MHz | 0.1017
| ADP (um2.ns) | 286934

> Raw FLOPS/MHz are NOT portable across process nodes. Compare designs with ops/FO4 (speed), kGE (area) and pJ/op (energy).
