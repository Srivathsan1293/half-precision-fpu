#!/usr/bin/env bash
# run_bench.sh — standalone FPU PPA benchmark harness (FMUL/FADDSUB/DIV/fpu_test/fpu_pcpi).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

TOPS=()
DLYS="9000"
CLKS="10"
ACT="0.1"

while [[ $# -gt 0 ]]; do
    case "$1" in
        all) TOPS=(FMUL FADDSUB DIV fpu_test fpu_pcpi); shift ;;
        fpnew|FPNEW) TOPS+=("fpnew"); shift ;; # run separately - different architecture (combinational vs pipelined)
        FMUL|FADDSUB|DIV|fpu_test|fpu_pcpi|fpnew) TOPS+=("$1"); shift ;;
        --dly) DLYS="$2"; shift 2 ;;
        --clk) CLKS="$2"; shift 2 ;;
        --act) ACT="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown arg '$1'"; exit 1 ;;
    esac
done
[[ ${#TOPS[@]} -gt 0 ]] || { echo "error: no tops selected (use `all` or a module name)"; exit 1; }
IFS=',' read -r -a DLY_ARR <<< "$DLYS"
IFS=',' read -r -a CLK_ARR <<< "$CLKS"

# top -> (yosys script, canonical netlist stem, linked top name, sta script)
top_meta() {
    case "$1" in
        FMUL)    echo "ppa_MUL.ys|FMUL_synth.v|FMUL|sta_FMUL.tcl" ;;
        FADDSUB) echo "ppa_ADDSUB.ys|ADDSUB_synth.v|addsub|sta_ADDSUB.tcl" ;;
        DIV)     echo "ppa_DIV.ys|DIV_synth.v|DIV|sta_DIV.tcl" ;;
        fpu_test) echo "ppa_combined_top.ys|fpu_test_synth.v|fpu_test|sta_fpu_test.tcl" ;;
        fpu_pcpi) echo "ppa_fpu_pcpi.ys|fpu_pcpi_synth.v|fpu_pcpi|sta_fpu_pcpi.tcl" ;;
        fpnew)   echo "ppa_fpnew.ys|fpnew_synth.v|fpnew_bench_top|sta_fpnew.tcl" ;;
    esac
}

STA_BIN="${STA_BIN:-$(command -v sta || command -v opensta)}"
command -v yosys >/dev/null || { echo "yosys not found"; exit 1; }
[[ -n "$STA_BIN" ]] || { echo "OpenSTA (sta) not found"; exit 1; }

# normalization constants
if [[ ! -f testing_results/norm_metrics.json ]]; then
    python3 tools/fo4.py
fi

OUT="testing_results/bench_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT/runs" "$OUT/netlists"
cp testing_results/norm_metrics.json "$OUT/norm_metrics.json"

echo "==> output dir: $OUT"
echo "==> tops : ${TOPS[*]}"
echo "==> dly  : ${DLY_ARR[*]} ps"
echo "==> clk  : ${CLK_ARR[*]} ns   activity $ACT"

# ... (existing code up to line 15)

    for TOP in "${TOPS[@]}"; do
        meta="$(top_meta "$TOP")"
        [[ -n "$meta" ]] || { echo "unknown top $TOP"; exit 1; }
        IFS='|' read -r YS NETSYS TOPNAME STA <<< "$meta"

        for DLY in "${DLY_ARR[@]}"; do
            work="$OUT/runs/${TOP}_d${DLY}"
            mkdir -p "$work"
            bench_v="$OUT/netlists/${TOP}_d${DLY}.v"

            # synthesize (sed: substitute ABC_DLY_PS token and write_verilog path)
            sed -e "s|map -D \${ABC_DLY_PS}|map -D ${DLY}|" \
                -e "s|write_verilog -noattr synth_outputs/.*|write_verilog -noattr ${bench_v}|" \
                "synth_scripts/$YS" > "$work/synth.ys"
            if ! yosys -s "$work/synth.ys" > "$work/synth.log" 2>&1; then
                echo "!! synthesis failed: $TOP dly=$DLY (see $work/synth.log)"
                continue
            fi
            echo "synth OK: $TOP dly=$DLY"

            for CLK in "${CLK_ARR[@]}"; do
                run="$work/sta_${TOP}_d${DLY}_c${CLK}"
                mkdir -p "$run"
                sed -e "s|^set NETLIST .*|set NETLIST ${bench_v}|" \
                    -e "s|^set CLK_NS .*|set CLK_NS ${CLK}|" \
                    -e "/^set CLK_NS /a set ACTIVITY ${ACT}" \
                    "synth_scripts/$STA" > "$run/sta.tcl"
                if ! "$STA_BIN" "$run/sta.tcl" > "$run/sta.log" 2>&1; then
                    echo "!! sta failed: $TOP dly=$DLY clk=$CLK"
                    continue
                fi
                cat > "$run/cfg.json" <<EOF
{"top":"$TOP","dly":$DLY,"clk":$CLK,"activity":$ACT,"ys":"$YS","sta":"$STA","netlist":"$bench_v"}
EOF
                echo "sta OK: $TOP dly=$DLY clk=$CLK"
            done
        done
    done

echo "==> aggregating"
python3 tools/extract_metrics.py --rundir "$OUT/runs" --norm "$OUT/norm_metrics.json" --out "$OUT"
echo "==> done: report in $OUT/bench_report.md , data in $OUT/bench.csv"