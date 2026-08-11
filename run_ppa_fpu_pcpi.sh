#!/bin/bash
# run_ppa_fpu_pcpi.sh — PPA analysis for the fpu_pcpi PCPI wrapper top module.
# Synthesizes src/fpu_pcpi.sv (3-state FSM + fpu_test datapath) with Yosys + ABC,
# then analyzes timing and power with OpenSTA using the Sky130 library.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p synth_outputs

echo "=============================================="
echo "PPA Analysis for fpu_pcpi (PCPI wrapper + FSM)"
echo "=============================================="

echo "[Step 1] Synthesizing with Yosys + ABC..."
yosys -q synth_scripts/ppa_fpu_pcpi.ys

if [ ! -f synth_outputs/fpu_pcpi_synth.v ]; then
    echo "ERROR: Synthesis failed or netlist not created!"
    exit 1
fi
echo "[OK] Netlist created: synth_outputs/fpu_pcpi_synth.v"

echo "[Step 2] Analyzing with OpenSTA..."
STA_BIN="${STA_BIN:-$(command -v sta || command -v opensta)}"
"$STA_BIN" synth_scripts/sta_fpu_pcpi.tcl
