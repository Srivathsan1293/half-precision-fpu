#!/usr/bin/env bash
# run_cycle_compare.sh
# Run specified bench workloads and compare cycle counts between:
#  - Software soft-float (soft_half.h) (measured by SW phase in firmware)
#  - "Custom" hardware (PicoRV32 + FPU PCPI wrapper used as custom)
#  - FPNEW (if src/fpnew.sv exists, attempted as an alternate hardware)
#
# Outputs per-workload logs in testing_results/ and writes a Markdown summary.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

WORKLOADS=(benchmm benchdig benchdiv)
OUTDIR="testing_results"
mkdir -p "$OUTDIR/logs"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT="$OUTDIR/cycle_comparison_${TIMESTAMP}.md"

FPNEW_DIR="third_party/fpnew"
HAS_FPNEW=0
if [ -d "$FPNEW_DIR" ]; then
    HAS_FPNEW=1
fi

echo "Running cycle-count comparison for workloads: ${WORKLOADS[*]}"

# Helper: run run_cpu_test.sh for a workload mode (benchmm/benchdig/benchdiv)
# Captures simulator stdout to log and extracts the cycles line.
run_fpu_pcpi() {
    local workload=$1
    local log=$2
    echo "Building+running (FPU PCPI) $workload ..."
    ./run_cpu_test.sh "$workload" > "$log" 2>&1 || true
}

# Helper: attempt an fpnew run by converting the FPNew SV sources to Verilog
# (tools/sv2v_fpnew.sh), instantiating the adapter (src/fpnew_pcpi_adapter.sv),
# and running Verilator. Best-effort; skip gracefully on failure.
run_fpnew() {
    local workload=$1
    local log=$2
    if [ $HAS_FPNEW -eq 0 ]; then
        echo "FPNEW sources not found; skipping FPNEW run" > "$log"
        return 1
    fi

    echo "Building firmware for $workload ..."
    case "$workload" in
        benchmm) MAKE_MODEL=FPU_TEST=benchmm ;;
        benchdig) MAKE_MODEL=FPU_TEST=benchdig ;;
        benchdiv) MAKE_MODEL=FPU_TEST=benchdiv ;;
        *) MAKE_MODEL=FPU_TEST=bench ;;
    esac
    make -C tb/firmware clean >/dev/null 2>&1 || true
    make -C tb/firmware $MAKE_MODEL >/dev/null 2>&1 || true

    echo "Converting FPNew SystemVerilog to Verilog (sv2v) ..."
    OBJDIR="obj_dir_tb_picorv32_fpnew_${workload}"
    # Ensure a fresh build so old binaries without HAS_FPU_PCPI aren't reused
    rm -rf "$OBJDIR" || true
    mkdir -p "$OBJDIR"
    FPNEW_VERILOG="$OBJDIR/fpnew_conv.v"
    if ! tools/sv2v_fpnew.sh "$FPNEW_VERILOG" > "$OBJDIR/sv2v.log" 2>&1; then
        echo "sv2v conversion failed (see $OBJDIR/sv2v.log)" > "$log"
        return 2
    fi

    echo "Verilating SoC top with FPNEW (converted) ..."
    set +e
    verilator --cc --build -j --trace \
        --top-module soc_fpu_top \
        --Mdir "$OBJDIR" \
        -Wno-fatal \
        -Wno-TIMESCALEMOD -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-WIDTHCONCAT \
        -Wno-SELRANGE -Wno-UNSIGNED -Wno-CASEINCOMPLETE -Wno-CASEX \
        -Wno-LATCH -Wno-BLKANDNBLK -Wno-UNOPTFLAT -Wno-UNDRIVEN -Wno-UNUSED \
        -Wno-PINMISSING -Wno-MULTIDRIVEN -Wno-VARHIDDEN -Wno-IMPLICIT \
        -Wno-DECLFILENAME -Wno-REALCVT -Wno-BSSPACE \
        --public-flat-rw \
        -DHAS_FPU_PCPI \
        "$FPNEW_VERILOG" \
        src/fpnew_pcpi_adapter.sv \
        src/fpu_test.sv src/fpu_FMUL.sv src/fpu_FADDSUB.sv src/fpu_FDIV.sv \
        src/fpu_modules.sv src/fdiv_datapath_blocks.sv \
        third_party/picorv32.v \
        tb/soc_fpu_top.sv \
        --exe tb/tb_fpu_pcpi.cpp > "$log" 2>&1
    VERILATOR_EXIT=$?
    set -e
    if [ $VERILATOR_EXIT -ne 0 ]; then
        echo "Verilator build failed for FPNEW (see $log)" >&2
        return 3
    fi

    BIN="$OBJDIR/Vsoc_fpu_top"
    if [ -x "$BIN" ]; then
        set +e
        "$BIN" > "$log" 2>&1
        set -e
    else
        echo "Simulation binary not found: $BIN" > "$log"
        return 4
    fi
    return 0
}

# Parse cycles line like:
#   cycles: SW soft-float phase = 123  HW PCPI phase = 45  speedup = 2.7x
# Returns sw_cyc hw_cyc
parse_cycles() {
    local log=$1
    local cycles_line
    cycles_line=$(grep "cycles:" -m1 "$log" || true)
    if [ -z "$cycles_line" ]; then
        echo "0 0"
        return
    fi
    # Extract two numbers (first = SW, second = HW)
    local sw_cyc hw_cyc
    sw_cyc=$(echo "$cycles_line" | sed -n 's/.*SW soft-float phase = \([0-9]*\).*/\1/p')
    hw_cyc=$(echo "$cycles_line" | sed -n 's/.*HW PCPI phase = \([0-9]*\).*/\1/p')
    # Fallback: try to grab numbers in order if patterns not matched
    if [ -z "$sw_cyc" ] || [ -z "$hw_cyc" ]; then
        # pick first two integers on the line
        read -r sw_cyc hw_cyc _ <<< "$(echo "$cycles_line" | grep -o '[0-9]\+' | awk 'NR==1{printf $0" "; next} NR==2{printf $0} NR>2{exit}')"
    fi
    sw_cyc=${sw_cyc:-0}
    hw_cyc=${hw_cyc:-0}
    echo "$sw_cyc $hw_cyc"
}

# Prepare report header
cat > "$REPORT" <<HEADER
# Cycle-Count Comparison Report

Workloads: ${WORKLOADS[*]}

| Workload | SW cycles | Custom (FPU PCPI) cycles | FPNEW cycles | Speedup (PCPI vs SW) | Speedup (FPNEW vs SW) | PCPI vs FPNEW |
|---|---:|---:|---:|---:|---:|---:|
HEADER

for w in "${WORKLOADS[@]}"; do
    echo "\n=== Workload: $w ==="
    log_pcpi="$OUTDIR/logs/${w}_fpu_pcpi.log"
    run_fpu_pcpi "$w" "$log_pcpi"
    read -r sw_pcpi hw_pcpi <<< "$(parse_cycles "$log_pcpi")"
    echo "Captured: SW=$sw_pcpi  HW(PCPI)=$hw_pcpi"

    fpnew_cycles_str="N/A"
    hw_fpnew=0
    if [ $HAS_FPNEW -eq 1 ]; then
        log_fpnew="$OUTDIR/logs/${w}_fpnew.log"
        run_fpnew "$w" "$log_fpnew" || true
        read -r sw_fp hw_fp <<< "$(parse_cycles "$log_fpnew")"
        # sw_fp should match sw_pcpi (firmware SW phase), use hw_fp as FPNEW hw cycles
        hw_fpnew=${hw_fp:-0}
        if [ "$hw_fpnew" -gt 0 ]; then
            fpnew_cycles_str=$hw_fpnew
            echo "Captured FPNEW HW cycles: $hw_fpnew"
        else
            echo "FPNEW run did not produce cycles info; see $log_fpnew"
        fi
    else
        echo "FPNEW source not present; skipping FPNEW run"
    fi

    # SW cycles baseline: prefer sw_pcpi (from the run above)
    sw_cycles=$sw_pcpi

    # Compute speedups (floating point), guard division by zero
    sp_pcpi="N/A"
    sp_fpnew="N/A"
    pcpi_vs_fpnew="N/A"
    if [ "$sw_cycles" -gt 0 ] && [ "$hw_pcpi" -gt 0 ]; then
        sp_pcpi=$(awk -v s=$sw_cycles -v h=$hw_pcpi 'BEGIN{printf "%.3f", s / h}')
    fi
    if [ "$sw_cycles" -gt 0 ] && [ "$hw_fpnew" -gt 0 ]; then
        sp_fpnew=$(awk -v s=$sw_cycles -v h=$hw_fpnew 'BEGIN{printf "%.3f", s / h}')
    fi
    if [ "$hw_pcpi" -gt 0 ] && [ "$hw_fpnew" -gt 0 ]; then
        pcpi_vs_fpnew=$(awk -v a=$hw_pcpi -v b=$hw_fpnew 'BEGIN{printf "%.3f", b ? (a / b) : 0}')
    fi

    # Append to summary table
    echo "| $w | ${sw_cycles:-N/A} | ${hw_pcpi:-N/A} | ${fpnew_cycles_str} | ${sp_pcpi}x | ${sp_fpnew}x | ${pcpi_vs_fpnew}x |" >> "$REPORT"

done

cat >> "$REPORT" <<FOOT

Generated: $(date '+%Y-%m-%d %H:%M:%S')

Notes:
- Each workload run builds the firmware (tb/firmware) and runs a Verilator simulation.
- The firmware contains both SW and HW phases; the SW phase (soft_half.h) is the same across hardware variants and serves as the baseline.
- The script treats the existing FPU PCPI wrapper (run_cpu_test.sh) as the "custom" hardware implementation.
- FPNEW support is best-effort: the script attempts an FPNEW-based Verilator build only if $FPNEW_DIR exists; integration may require additional source files.
FOOT

echo "Done. Report written to: $REPORT"
cat "$REPORT"
