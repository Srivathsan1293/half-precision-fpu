#!/usr/bin/env python3
"""tools/flops_report.py — compute FLOPS + process-normalized metrics for fpu_pcpi.

Reads the run tree produced by run_flops.sh and writes a self-contained report:

    <outdir>/
      norm_metrics.json
      runs/fpu_pcpi_synth.v / synth.log        # Yosys output + log
      runs/sta_c<CLK>/sta.log                  # OpenSTA per clock period
      runs/benchmm.log benchdig.log benchdiv.log  # SoC sim logs (cycles line)

FLOPS definition (fp16, single-lane scalar unit):
  - Peak, interface-limited: the fpu_pcpi FSM retires one op per latency
    (FADD/FSUB/FMUL = 1 cyc, FDIV = 12 cyc), so peak = Fmax / latency.
  - Peak, datapath: fpu_test is a fully pipelined 1-op/cycle datapath, so
    datapath peak = Fmax (the wrapper serializes this down to the interface rate).
  - Realized: (FP ops in workload) * Fmax / HW-phase cycles, measured from the
    SoC simulation of the existing firmware workloads.

Process-node-independent normalization (all process axes measured separately):
  - throughput  -> ops/FO4 = 1 / LTP_fo4        (FO4 from norm_metrics.json)
  - area        -> GE (1 GE = nand2 area), FLOPS/GE
  - energy      -> pJ/op, FLOPS/W, mW/MHz
  - combined    -> ADP (um2.ns), FLOPS*FO4/GE
"""
import argparse
import json
import os
import re

# ---- workload metadata: (total_ops, mul, add, div), counted from the kernels
#   benchmm  : 4x4 fp16 matmul, 16 cells x (4 fmul + 4 fadd + 1 acc fadd) = 144
#   benchdig : 5-tap FIR, 16 samples; sum(min(n+1,5)) = 70 taps -> 140 ops
#              + 16 acc fadd = 156
#   benchdiv : 16 samples x 4 passes x (1 fdiv + 1 acc fadd) = 128
WORKLOADS = {
    "benchmm":  {"total": 144, "mul": 64,  "add": 80, "div": 0},
    "benchdig": {"total": 156, "mul": 70,  "add": 86, "div": 0},
    "benchdiv": {"total": 128, "mul": 0,   "add": 64, "div": 64},
}

CELLS_RE = re.compile(r"Number of cells:\s+(\d+)")
AREASUMMARY_RE = re.compile(r"\s+(\d+)\s+[+-]?[\d.eE+-]+\s+cells")
FLOPROW_RE = re.compile(
    r"\s+(\d+)\s+[+-]?[\d.eE+-]+\s+(sky130_fd_sc_hd__(?:df|sdf|dl)[a-z0-9_]*)\s*$"
)
CYCLES_RE = re.compile(r"HW PCPI phase = (\d+)")


def parse_synth(log_path):
    area = cells = flops = None
    try:
        with open(log_path, errors="ignore") as fh:
            for ln in fh:
                m = re.search(r"Chip area for module\s+([^\s]+)\s+([\d.eE+-]+)", ln)
                if m:
                    area = float(m.group(2))
                m = CELLS_RE.search(ln)
                if m:
                    cells = int(m.group(1))
                m = AREASUMMARY_RE.search(ln)
                if m and cells is None:
                    cells = int(m.group(1))
                m = FLOPROW_RE.search(ln)
                if m:
                    flops = (flops or 0) + int(m.group(1))
    except OSError:
        pass
    return {"area_um2": area, "cells": cells, "flops": flops}


def parse_sta(log_path):
    """LTP = max |data arrival time|, slack = min slack, power from Total line."""
    ltp = slack = tot = None
    try:
        with open(log_path, errors="ignore") as fh:
            for ln in fh:
                m = re.match(r"^\s*(-?[\d.eE+]+)\s+data arrival time\s*$", ln)
                if m:
                    v = float(m.group(1))
                    ltp = max(ltp or 0, abs(v))
                m = re.match(r"^\s*(-?[\d.eE+]+)\s+slack\s+\((?:MET|VIOLATED)\)", ln)
                if m:
                    v = float(m.group(1))
                    slack = v if slack is None else min(slack, v)
                m = re.match(r"^Total\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)", ln)
                if m:
                    vals = [float(x) for x in m.groups()]
                    tot = vals[3]
    except OSError:
        pass
    return {"ltp_ns": ltp, "slack_ns": slack, "tot_w": tot}


def parse_cycles(log_path):
    try:
        with open(log_path, errors="ignore") as fh:
            for ln in fh:
                m = CYCLES_RE.search(ln)
                if m:
                    return int(m.group(1))
    except OSError:
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--norm", default=None)
    ap.add_argument("--dly", type=int, default=4000, help="ABC map -D delay (ps)")
    ap.add_argument("--activity", type=float, default=0.1)
    args = ap.parse_args()

    norm = {}
    norm_path = args.norm or os.path.join(args.out, "norm_metrics.json")
    if os.path.exists(norm_path):
        with open(norm_path) as fh:
            norm = json.load(fh)
    nand2 = norm.get("nand2_area_um2", 3.7536)
    fo4_ps = norm.get("fo4_ps", 37.2)

    runs = os.path.join(args.out, "runs")
    s = parse_synth(os.path.join(runs, "synth.log"))
    area = s["area_um2"]
    ge = area / nand2 if area else None
    fo4_ps_total = norm.get("fo4_ps_total", fo4_ps)

    sta = {}
    for clk in sorted({int(d[5:]) for d in os.listdir(runs)
                       if d.startswith("sta_c") and os.path.isdir(os.path.join(runs, d))}):
        t = parse_sta(os.path.join(runs, f"sta_c{clk}", "sta.log"))
        sta[clk] = {**t, "clk_ns": clk,
                    "fmax_mhz": 1000.0 / clk,
                    "closed": bool(t["slack_ns"] is not None and t["slack_ns"] >= 0),
                    "tot_mw": t["tot_w"] * 1000.0 if t["tot_w"] is not None else None}

    ltp = next((t["ltp_ns"] for t in sta.values() if t["ltp_ns"] is not None), None)
    fmax_ltp = 1000.0 / ltp if ltp else None
    closed = [t for t in sta.values() if t["closed"]]
    best = max(closed, key=lambda t: t["fmax_mhz"]) if closed else None
    fmax_closed = best["fmax_mhz"] if best else None
    pj_op = (best["tot_mw"] / best["fmax_mhz"] * 1000.0) if best and best["tot_mw"] else None
    ltp_fo4 = (ltp * 1000.0 / fo4_ps) if ltp else None
    ops_per_fo4 = 1.0 / ltp_fo4 if ltp_fo4 else None

    # ---- workload realized FLOPS (uses the timing-closed Fmax) ----
    realized = {}
    for w, meta in WORKLOADS.items():
        cyc = parse_cycles(os.path.join(runs, f"{w}.log"))
        mflops = (meta["total"] * fmax_closed) / cyc if cyc and fmax_closed else None
        realized[w] = {"cycles": cyc, "mflops": mflops, **meta}

    md = []
    md.append("# fpu_pcpi FLOPS Report (fp16, Sky130)")
    md.append("")
    md.append(f"- Run tree: `{os.path.basename(args.out)}`")
    md.append(f"- Top module: `fpu_pcpi` (PCPI wrapper + FSM + fpu_test datapath)")
    md.append(f"- Synthesis: Yosys + ABC, `map -D {args.dly}` ps")
    md.append(f"- Timing/power: OpenSTA, activity {args.activity}")
    md.append(f"- Library: `{norm.get('lib', 'n/a')}` (tt / 1.8 V / 25 C)")
    md.append(f"- Normalization: 1 GE = {nand2} um2 (`nand2_1`); FO4 = {fo4_ps} ps "
              f"(absolute at 4x C_in = {fo4_ps_total} ps)")
    md.append("")

    md.append("## Process measurements (each axis measured separately)")
    md.append("")
    md.append("| Metric | Value |")
    md.append("|--------|-------|")
    md.append(f"| Area (um2) | {area:.1f}" if area else "| Area (um2) | n/a |")
    md.append(f"| Area (kGE) | {ge / 1000.0:.2f}" if ge else "| Area (kGE) | n/a |")
    md.append(f"| Cells / flops | {s['cells']} / {s['flops']}" if s["cells"] else "| Cells / flops | n/a |")
    md.append(f"| LTP (ns) | {ltp:.3f}" if ltp else "| LTP (ns) | n/a |")
    md.append(f"| LTP (FO4) | {ltp_fo4:.0f}" if ltp_fo4 else "| LTP (FO4) | n/a |")
    md.append(f"| Fmax = 1/LTP (MHz) | {fmax_ltp:.1f}" if fmax_ltp else "| Fmax = 1/LTP (MHz) | n/a |")
    md.append(f"| Timing-closed Fmax (MHz) | {fmax_closed:.1f} at clk {best['clk_ns']} ns"
              if best else "| Timing-closed Fmax (MHz) | none closed |")
    md.append(f"| Total power @ closed (mW) | {best['tot_mw']:.2f}"
              if best and best["tot_mw"] else "| Total power @ closed (mW) | n/a |")
    md.append(f"| Energy/op (pJ) | {pj_op:.1f}" if pj_op else "| Energy/op (pJ) | n/a |")
    md.append("")

    md.append("## Peak FLOPS (fp16, single-lane)")
    md.append("")
    md.append("> `fpu_pcpi` is a *blocking* PCPI coprocessor: it retires one op per "
              "latency (FADD/FSUB/FMUL = 1 cyc, FDIV = 12 cyc). Peak is therefore "
              "interface-limited to `Fmax / latency`. The underlying `fpu_test` "
              "datapath is fully pipelined at 1 op/cycle, so the datapath peak is "
              "`Fmax`; the wrapper serializes it. Both are reported; the interface "
              "number is what the coprocessor can actually sustain.")
    md.append("")
    md.append("| Variant | Formula | LTP-bound (MFLOP/s) | Timing-closed (MFLOP/s) |")
    md.append("|---------|---------|--------------------:|------------------------:|")
    md.append("| Datapath peak (any op) | Fmax | {:.1f} | {:.1f} |".format(
        fmax_ltp or 0, fmax_closed or 0))
    md.append("| Interface peak FADD/FSUB/FMUL | Fmax / 1 | {:.1f} | {:.1f} |".format(
        fmax_ltp or 0, fmax_closed or 0))
    md.append("| Interface peak FDIV | Fmax / 12 | {:.1f} | {:.1f} |".format(
        (fmax_ltp or 0) / 12, (fmax_closed or 0) / 12))
    md.append("")

    md.append("## Realized FLOPS (measured, SoC + firmware overhead included)")
    md.append("")
    md.append("| Workload | FP ops (mul/add/div) | HW-phase cycles | Realized (MFLOP/s) | ops/cycle |")
    md.append("|----------|---------------------:|----------------:|-------------------:|----------:|")
    for w, r in realized.items():
        md.append("| {w} | {t} ({m}/{a}/{d}) | {c} | {f:.2f} | {o:.4f} |".format(
            w=w, t=r["total"], m=r["mul"], a=r["add"], d=r["div"], c=r["cycles"],
            f=r["mflops"] or 0.0, o=(r["total"] / r["cycles"]) if r["cycles"] else 0.0))
    md.append("")
    md.append("> Realized FLOPS = FP ops x timing-closed Fmax / HW-phase cycles. The gap to "
              "the interface peak is firmware + loop + load/store overhead (each custom "
              "op costs 4 instructions in fpu_macros.h), not the FPU.")
    md.append("")

    md.append("## Process-node-independent comparison metrics")
    md.append("")
    md.append("| Metric | Value |")
    md.append("|--------|-------|")
    md.append(f"| ops/FO4 (= 1/LTP_fo4) | {ops_per_fo4:.4f}" if ops_per_fo4 else "| ops/FO4 | n/a |")
    md.append(f"| FLOPS/GE (peak, LTP-bound) | {(fmax_ltp or 0) * 1e6 / ge / 1e3:.3f} kFLOP/s/GE"
              if ge and fmax_ltp else "| FLOPS/GE | n/a |")
    md.append(f"| FLOPS/W (peak, closed) | {(fmax_closed or 0) / (pj_op or 1):.2f} GFLOPS/W"
              if pj_op and fmax_closed else "| FLOPS/W | n/a |")
    md.append(f"| mW/MHz | {(best['tot_mw'] or 0) / fmax_closed:.4f}" if best and fmax_closed else "| mW/MHz | n/a |")
    md.append(f"| ADP (um2.ns) | {area * ltp:.0f}" if area and ltp else "| ADP | n/a |")
    md.append("")
    md.append("> Raw FLOPS/MHz are NOT portable across process nodes. Compare designs with "
              "ops/FO4 (speed), kGE (area) and pJ/op (energy).")

    md_path = os.path.join(args.out, "flops_report.md")
    with open(md_path, "w") as fh:
        fh.write("\n".join(md) + "\n")
    print(f"wrote {md_path}")

    with open(os.path.join(args.out, "flops.json"), "w") as fh:
        json.dump({
            "top": "fpu_pcpi", "dly": args.dly, "activity": args.activity,
            "area_um2": area, "ge": ge, "cells": s["cells"], "flops": s["flops"],
            "ltp_ns": ltp, "ltp_fo4": ltp_fo4, "fmax_ltp_mhz": fmax_ltp,
            "fmax_closed_mhz": fmax_closed, "closed_clk_ns": best["clk_ns"] if best else None,
            "pj_op": pj_op, "ops_per_fo4": ops_per_fo4,
            "realized": realized,
        }, fh, indent=2)
    print(f"wrote {os.path.join(args.out, 'flops.json')}")


if __name__ == "__main__":
    main()