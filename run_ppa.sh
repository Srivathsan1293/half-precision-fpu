#!/bin/bash
# Run PPA analysis for fpu_test top module (fpu_test.sv)
# Synthesizes with Yosys + ABC, then analyzes with OpenSTA using Sky130 library

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="sky130_fd_sc_hd__tt_025C_1v80.lib"

echo "=============================================="
echo "PPA Analysis for fpu_test Top Module"
echo "=============================================="
echo ""

# Step 1: Synthesize with Yosys (ABC_DLY_PS default 9000)
echo "[Step 1] Synthesizing with Yosys + ABC..."
ABC_DLY_PS="${ABC_DLY_PS:-9000}"
mkdir -p "$SCRIPT_DIR/synth_outputs"
sed "s|map -D \${ABC_DLY_PS}|map -D ${ABC_DLY_PS}|" \
    "$SCRIPT_DIR/synth_scripts/ppa_combined_top.ys" > "$SCRIPT_DIR/synth_outputs/.ppa_combined_top.ys"
yosys -q "$SCRIPT_DIR/synth_outputs/.ppa_combined_top.ys"
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
STA_BIN="${STA_BIN:-$(command -v sta || command -v opensta)}"
"$STA_BIN" synth_scripts/sta_fpu_test.tcl
