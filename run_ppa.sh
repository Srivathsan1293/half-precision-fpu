#!/bin/bash
# Run PPA analysis for fpu_test top module (fpu_test.sv)
# Synthesizes with Yosys + ABC, then analyzes with OpenSTA using Sky130 library

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="sky130_fd_sc_hd__tt_025C_1v80.lib"

echo "=============================================="
echo "PPA Analysis for fpu_test Top Module"
echo "=============================================="
echo ""

# Step 1: Synthesize with Yosys
echo "[Step 1] Synthesizing with Yosys + ABC..."
yosys -q "$SCRIPT_DIR/synth_scripts/ppa_combined_top.ys"
echo ""

# Step 2: Check if netlist was created
if [ ! -f "$SCRIPT_DIR/synth_outputs/fpu_test_synth.v" ]; then
    echo "ERROR: Synthesis failed or netlist not created!"
    exit 1
fi
echo "[OK] Netlist created: synth_outputs/fpu_test_synth.v"
echo ""

# Step 3: Run OpenSTA for timing and power analysis
echo "[Step 2] Analyzing with OpenSTA..."
cd "$SCRIPT_DIR"
opensta synth_scripts/sta_fpu_test.tcl
