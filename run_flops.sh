#!/usr/bin/env bash
# run_flops.sh — self-contained FLOPS measurement for the fpu_pcpi PCPI wrapper.
#
# Measures, separately:
#   1. Process/area   : Yosys + ABC synthesis of src/fpu_pcpi.sv (area/GE/cells/flops)
#   2. Process/speed  : OpenSTA clock sweep (LTP -> Fmax, timing-closed Fmax)
#   3. Process/energy : OpenSTA power at the timing-closed period (mW, pJ/op)
#   4. System/cycles  : Verilator SoC runs of the existing firmware workloads
#                       (benchmm/benchdig/benchdiv), HW-phase cycle counts
# then combines them (tools/flops_report.py) into a FLOPS report: peak
# (interface-limited and datapath) and realized, plus process-node-independent
# comparison metrics (ops/FO4, kGE, pJ/op, FLOPS/W).
#
# Usage:
#   ./run_flops.sh                 # defaults: --dly 4000 --clks "10 9 8 7 6 5 4"
#   ./run_flops.sh --dly 9000 --clks "14 13 12 11 10"
#   ./run_flops.sh --workloads benchmm benchdiv
#
# The --clk sweep must include the tightest period you want to try to close;
# the script reports the timing-closed Fmax found in the sweep.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

DLY="4000"
CLKS="10 9 8 7 6 5 4"
ACT="0.1"
WORKLOADS=(benchmm benchdig benchdiv)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dly) DLY="$2"; shift 2 ;;
        --clks) CLKS="$2"; shift 2 ;;
        --act) ACT="$2"; shift 2 ;;
        --workloads) shift; WORKLOADS=("$@"); break ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "error: unknown arg '$1'"; exit 1 ;;
    esac
done

command -v yosys >/dev/null || { echo "yosys not found"; exit 1; }
STA_BIN="${STA_BIN:-$(command -v sta || command -v opensta)}"
[[ -n "$STA_BIN" ]] || { echo "OpenSTA (sta) not found"; exit 1; }
command -v verilator >/dev/null || { echo "verilator not found"; exit 1; }

# Normalization constants (GE / FO4 derived from the local Sky130 lib).
if [[ ! -f testing_results/norm_metrics.json ]]; then
    python3 tools/fo4.py
fi

OUT="testing_results/flops_$(date +%Y%m%d_%H%M%S)"
RUNS="$OUT/runs"
mkdir -p "$RUNS"
cp testing_results/norm_metrics.json "$OUT/norm_metrics.json"

echo "==> output dir: $OUT"
echo "==> ABC dly : $DLY ps"
echo "==> clk sweep : ${CLKS} ns   activity $ACT"
echo "==> workloads : ${WORKLOADS[*]}"

# --- 1. Synthesize fpu_pcpi (area / GE / cells / flops) ----------------------
NETLIST="$RUNS/fpu_pcpi_synth.v"
sed -e "s|map -D \${ABC_DLY_PS}|map -D ${DLY}|" \
    -e "s|write_verilog -noattr synth_outputs/fpu_pcpi_synth.v|write_verilog -noattr ${NETLIST}|" \
    synth_scripts/ppa_fpu_pcpi.ys > "$RUNS/synth.ys"
echo "==> synthesizing fpu_pcpi (dly=$DLY) ..."
if ! yosys -s "$RUNS/synth.ys" > "$RUNS/synth.log" 2>&1; then
    echo "!! synthesis failed (see $RUNS/synth.log)"
    exit 1
fi
echo "==> synthesis OK: $NETLIST"

# --- 2+3. STA sweep (Fmax, timing-closed Fmax, power / pJ) -------------------
for CLK in $CLKS; do
    work="$RUNS/sta_c${CLK}"
    mkdir -p "$work"
    sed -e "s|^set NETLIST .*|set NETLIST ${NETLIST}|" \
        -e "s|^set CLK_NS .*|set CLK_NS ${CLK}|" \
        -e "/^set CLK_NS /a set ACTIVITY ${ACT}" \
        synth_scripts/sta_fpu_pcpi.tcl > "$work/sta.tcl"
    if ! "$STA_BIN" "$work/sta.tcl" > "$work/sta.log" 2>&1; then
        echo "!! sta failed at clk=$CLK (see $work/sta.log)"
        exit 1
    fi
    echo "==> sta OK at clk=$CLK ns"
done

# --- 4. SoC workload runs (realized FLOPS cycle counts) ----------------------
for w in "${WORKLOADS[@]}"; do
    echo "==> running workload $w (SoC simulation) ..."
    ./run_cpu_test.sh "$w" > "$RUNS/${w}.log" 2>&1 || true
    if ! grep -q "cycles: SW soft-float phase" "$RUNS/${w}.log"; then
        echo "!! workload $w produced no cycle line; see $RUNS/${w}.log"
        exit 1
    fi
    echo "==> $w OK: $(grep -m1 'cycles:' "$RUNS/${w}.log" | sed 's/^ *//')"
done

# --- 5. Combine into the FLOPS report ----------------------------------------
echo "==> computing FLOPS report ..."
python3 tools/flops_report.py --out "$OUT" --norm "$OUT/norm_metrics.json" \
    --dly "$DLY" --activity "$ACT"

echo "==> done: $OUT/flops_report.md , $OUT/flops.json"