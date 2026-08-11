#!/usr/bin/env bash
# run_pcpi_handshake.sh — build & run the wrapper-only PCPI handshake test.
#
# Verilates src/fpu_pcpi.sv (with the FPU datapath sources) as the top
# module and drives the PCPI bus directly from tb/tb_pcpi_handshake.cpp,
# checking the handshake protocol rules without needing a CPU or firmware.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

OBJDIR="obj_dir_pcpi_handshake"
BUILD_ONLY=0
[ "${1:-}" = "--build-only" ] && BUILD_ONLY=1

echo "==> Verilating fpu_pcpi (wrapper-only handshake test) ..."
verilator --cc --build -j \
    --top-module fpu_pcpi \
    --Mdir "$OBJDIR" \
    -Wno-TIMESCALEMOD \
    src/fpu_pcpi.sv src/fpu_test.sv src/fpu_FMUL.sv src/fpu_FADDSUB.sv \
    src/fpu_FDIV.sv src/fpu_modules.sv src/fdiv_datapath_blocks.sv \
    --exe tb/tb_pcpi_handshake.cpp

if [ "$BUILD_ONLY" -eq 1 ]; then
    echo "==> Build-only (skipping handshake test) ..."
    exit 0
fi

echo "==> Running handshake test ..."
"$OBJDIR/Vfpu_pcpi"
