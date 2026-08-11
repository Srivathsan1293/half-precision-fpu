#!/usr/bin/env bash
# run_exhaustive_tests.sh — six-stage exhaustive testing pipeline for the
# half-precision FPU + PicoRV32 PCPI integration.
#
# Stage A: per-module 4B (2^32) operand-combination tests. Rebuilds the four
#          existing per-module testbenches (tb_fpu / tb_fADDSUB / tb_fMUL /
#          tb_fDIV) against the OLD ALIGNED fpu_test.sv top
#          (src/fpu_test_aligned.sv, 4-cycle output alignment for FADDSUB/FMUL
#          vs the 4-stage FDIV). Each testbench sweeps the full
#          0xFFFF x 0xFFFF x ops input space against an independent IEEE-754
#          golden model and writes per-category failure logs.
# Stage B: exhaustive re-run of every suite created in this project phase —
#          fpu_stress (CPU stress + numeric sweep), standard-Zhinx integration
#          (fpu_zhinx_main.c), and asm_all_ops.S.
# Stage C: separate wrapper-only state-machine test (exact counter->ready
#          timing) plus the PCPI handshake protocol test.
# Stage D: separate CPU tests — baseline integer smoke, FPU PCPI, ebreak-IRQ
#          resume-PC premise (spike), and the software-emulator self-test (emu).
# Stage E: exhaustive trap / edge-case hunting — the boundary vector table
#          (fpu_edge_main.c, sNaN payloads, signed zeros, Inf/NaN FCVT
#          saturation, INT_MIN/INT_MAX/UINT32_MAX conversions, every FCLASS
#          category) and the unsupported-op halt probe (fpu_unsup_main.S).
# Stage F: custom asm + C programs calling EVERY instruction with golden
#          checks — asm_all_ops.S (17 ops, fixed operands) and the reusable C
#          per-instruction driver fpu_zhinx_main.c.
#
# Default behaviour: BUILD-ONLY. Every stage verilates/assembles its binaries
# but does NOT execute them (a full Stage A sweep is ~8.6e9 comparisons per op
# and would take hours). Pass --run to actually execute; --stage N to run just
# one stage. Logs go to testing_results/fpu_exhaustive_log.txt; any stage that
# fails is also appended to testing_results/fpu_failed_log.txt.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

LOG="testing_results/fpu_exhaustive_log.txt"
FAIL="testing_results/fpu_failed_log.txt"
RESULTS="testing_results"

DO_RUN=0
STAGE_FILTER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --run) DO_RUN=1 ;;
        --stage) STAGE_FILTER="$2"; shift ;;
        *) echo "usage: $0 [--run] [--stage N]" >&2; exit 2 ;;
    esac
    shift
done

mkdir -p "$RESULTS"
: > "$LOG"
: > "$FAIL"

log() { echo "$@" | tee -a "$LOG"; }
stage_enabled() { [ -z "$STAGE_FILTER" ] || [ "$STAGE_FILTER" = "$1" ]; }

TOTAL_PASS=0
TOTAL_FAIL=0

# stage_begin <name> ; stage_end <rc> <name> — bracketed by each stage.
stage_begin() {
    log "============================================================"
    log "== STAGE $1: $2"
    log "============================================================"
}
stage_end() {
    if [ "$1" -eq 0 ]; then
        log ">> stage $2: PASS"
        TOTAL_PASS=$((TOTAL_PASS + 1))
    else
        log ">> stage $2: FAIL (see $FAIL)"
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
    fi
    log ""
}

# run_captured <label> <cmd...> — runs a command, capturing its output to a
# temp file. On failure the command's output (tail) is replayed into the
# console AND appended to the log so the user always sees why it failed.
# Returns the command's exit status. Use for build-only stages (quiet on
# success, diagnostic on failure).
run_captured() {
    local label="$1"; shift
    local tmp rc
    tmp="$(mktemp)"
    if "$@" >"$tmp" 2>&1; then
        rc=0
    else
        rc=$?
        log "   FAILED: $label"
        log "   ---- output (tail 40) ----"
        tail -40 "$tmp" | sed 's/^/   | /' | tee -a "$LOG"
        log "   ---- end output ----"
    fi
    rm -f "$tmp"
    return $rc
}

# run_visible <label> <cmd...> — runs a command with output streamed straight
# to the console AND appended to the log (tee). Use for --run mode so pass
# results are always visible and failures carry their full reason. Returns the
# command's exit status.
run_visible() {
    local label="$1"; shift
    local rc
    "$@" 2>&1 | tee -a "$LOG"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ]; then
        log "   FAILED: $label (exit $rc)"
        echo "$label failed" >> "$FAIL"
    fi
    return $rc
}

run_stage_a() {
    stage_begin A "per-module 4B (2^32) exhaustive operand tests (old aligned fpu_test.sv)"
    local rc=0
    local aligned="src/fpu_test_aligned.sv"
    local combined="$aligned src/fpu_FMUL.sv src/fpu_FADDSUB.sv src/fpu_FDIV.sv"
    combined="$combined src/fpu_modules.sv src/fdiv_datapath_blocks.sv"

    # Testbench -> (obj-dir name, RTL top module, source files). The combined
    # fpu_test top is the OLD ALIGNED version (src/fpu_test_aligned.sv, 4-cycle
    # output alignment); tb_fpu and tb_fDIV use 4-deep history buffers to track
    # in-flight inputs. tb_fADDSUB / tb_fMUL were written for a single-cycle
    # interface (direct sub / no op mux), so they are built against per-module
    # flag-computing shims (tb/fpu_test_{addsub,fmul}_wrap.sv) that expose the
    # same port set while exercising the real addsub / FMUL datapaths.
    while IFS='|' read -r tb exe top files; do
        local odir="obj_dir_${exe}"
        log "   building $tb -> $odir/V$top"
        if ! verilator --cc --build -j \
                --top-module "$top" \
                --Mdir "$odir" \
                -Wno-TIMESCALEMOD \
                $files \
                --exe "$tb" ; then
            log "   FAILED to build $tb"
            echo "Stage A: build $tb failed" >> "$FAIL"
            rc=1
            continue
        fi
        if [ "$DO_RUN" -eq 1 ]; then
            log "   running $odir/V$top"
            if ! "$odir/V$top" ; then
                log "   FAILED at runtime: $tb"
                echo "Stage A: $tb failed" >> "$FAIL"
                rc=1
            fi
        else
            log "   [build-only] run with: $odir/V$top"
        fi
    done <<EOF
tb/tb_fpu.cpp|fpu|fpu_test|$combined
tb/tb_fADDSUB.cpp|fADDSUB|fpu_test|tb/fpu_test_addsub_wrap.sv src/fpu_FADDSUB.sv
tb/tb_fMUL.cpp|fMUL|fpu_test|tb/fpu_test_fmul_wrap.sv src/fpu_FMUL.sv src/fpu_modules.sv
tb/tb_fDIV.cpp|fDIV|fpu_test|$combined
EOF

    if [ "$DO_RUN" -eq 1 ]; then
        log "   NOTE: tb_fADDSUB / tb_fMUL / tb_fDIV have no quick-mode bound and run"
        log "   the full 4.3e9-8.6e9-combination sweep (~minutes each). tb_fpu accepts"
        log "   an optional bound: ./obj_dir_fpu/Vfpu 64"
    fi
    stage_end "$rc" A
}

run_stage_b() {
    stage_begin B "exhaustive re-run of this phase's suites (stress / zhinx / asm_all)"
    local rc=0
    # NOTE: asmall takes the program path as $2 and cycle count as $3, so the
    # cycles must be passed as the THIRD argument, not the second.
    for m in stress zhinx asmall; do
        log "   building+configuring mode '$m'"
        local cmd=(./run_cpu_test.sh "$m")
        [ "$m" = "asmall" ] && cmd+=(tb/firmware/asm_all_ops.S 200000)
        if [ "$DO_RUN" -eq 1 ]; then
            log "   running: ${cmd[*]}"
            if ! run_visible "mode '$m'" "${cmd[@]}"; then
                echo "Stage B: mode '$m' failed" >> "$FAIL"
                rc=1
            fi
        else
            log "   [build-only] run with: ${cmd[*]}"
            if ! run_captured "build mode '$m'" ./run_cpu_test.sh "$m"; then
                echo "Stage B: build '$m' failed" >> "$FAIL"
                rc=1
            fi
        fi
    done
    stage_end "$rc" B
}

run_stage_c() {
    stage_begin C "separate wrapper-only state-machine + handshake tests"
    local rc=0
    for s in run_fsm.sh run_pcpi_handshake.sh; do
        if [ "$DO_RUN" -eq 1 ]; then
            log "   running: ./$s"
            if ! ./$s; then
                log "   FAILED: $s"
                echo "Stage C: $s failed" >> "$FAIL"
                rc=1
            fi
        else
            log "   [build-only] run with: ./$s"
            if ! ./$s --build-only; then
                log "   FAILED to build: $s"
                echo "Stage C: build $s failed" >> "$FAIL"
                rc=1
            fi
        fi
    done
    stage_end "$rc" C
}

run_stage_d() {
    stage_begin D "separate CPU tests (baseline / fpu / spike / emu)"
    local rc=0
    for m in baseline fpu spike emu; do
        if [ "$DO_RUN" -eq 1 ]; then
            log "   running: ./run_cpu_test.sh $m"
            if ! run_visible "mode '$m'" ./run_cpu_test.sh "$m"; then
                echo "Stage D: mode '$m' failed" >> "$FAIL"
                rc=1
            fi
        else
            log "   [build-only] run with: ./run_cpu_test.sh $m 200000"
            if ! run_captured "build mode '$m'" ./run_cpu_test.sh "$m"; then
                echo "Stage D: build '$m' failed" >> "$FAIL"
                rc=1
            fi
        fi
    done
    stage_end "$rc" D
}

run_stage_e() {
    stage_begin E "exhaustive trap / edge-case hunting (edge vectors + unsupported-op probe)"
    local rc=0
    # fpu_edge_main.c: 100+ boundary vectors through every instruction.
    if [ "$DO_RUN" -eq 1 ]; then
        log "   running: ./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c"
        if ! run_visible "edge-case sweep" ./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000; then
            echo "Stage E: edge-case sweep failed" >> "$FAIL"
            rc=1
        fi
    else
        log "   [build-only] run with: ./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000"
        if ! run_captured "build edge-case sweep" ./run_cpu_test.sh asmall tb/firmware/fpu_edge_main.c 200000; then
            echo "Stage E: build edge failed" >> "$FAIL"
            rc=1
        fi
    fi
    # fpu_unsup_main.S: FSQRT.H must halt (emulator records insn + resume PC).
    if [ "$DO_RUN" -eq 1 ]; then
        log "   running: ./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S"
        if ! run_visible "unsupported-op probe" ./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000; then
            echo "Stage E: unsupported-op probe failed" >> "$FAIL"
            rc=1
        fi
    else
        log "   [build-only] run with: ./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000"
        if ! run_captured "build unsupported-op probe" ./run_cpu_test.sh asmall tb/firmware/fpu_unsup_main.S 30000; then
            echo "Stage E: build unsup failed" >> "$FAIL"
            rc=1
        fi
    fi
    stage_end "$rc" E
}

run_stage_f() {
    stage_begin F "custom asm + C program calling every instruction (golden checks)"
    local rc=0
    # Assembly: asm_all_ops.S (17 ops, fixed operands -> check_asm_all).
    if [ "$DO_RUN" -eq 1 ]; then
        log "   running: ./run_cpu_test.sh asmall tb/firmware/asm_all_ops.S"
        if ! run_visible "asm_all_ops.S" ./run_cpu_test.sh asmall tb/firmware/asm_all_ops.S 200000; then
            echo "Stage F: asm_all_ops.S failed" >> "$FAIL"
            rc=1
        fi
    else
        log "   [build-only] run with: ./run_cpu_test.sh asmall tb/firmware/asm_all_ops.S 200000"
        if ! run_captured "build asm_all_ops.S" ./run_cpu_test.sh asmall tb/firmware/asm_all_ops.S 200000; then
            echo "Stage F: build asm_all failed" >> "$FAIL"
            rc=1
        fi
    fi
    # C: fpu_zhinx_main.c (reusable per-instruction driver -> check_zhinx).
    if [ "$DO_RUN" -eq 1 ]; then
        log "   running: ./run_cpu_test.sh asmall tb/firmware/fpu_zhinx_main.c"
        if ! run_visible "fpu_zhinx_main.c" ./run_cpu_test.sh asmall tb/firmware/fpu_zhinx_main.c 200000; then
            echo "Stage F: fpu_zhinx_main.c failed" >> "$FAIL"
            rc=1
        fi
    else
        log "   [build-only] run with: ./run_cpu_test.sh asmall tb/firmware/fpu_zhinx_main.c 200000"
        if ! run_captured "build fpu_zhinx_main.c" ./run_cpu_test.sh asmall tb/firmware/fpu_zhinx_main.c 200000; then
            echo "Stage F: build zhinx failed" >> "$FAIL"
            rc=1
        fi
    fi
    stage_end "$rc" F
}

if stage_enabled A; then run_stage_a; fi
if stage_enabled B; then run_stage_b; fi
if stage_enabled C; then run_stage_c; fi
if stage_enabled D; then run_stage_d; fi
if stage_enabled E; then run_stage_e; fi
if stage_enabled F; then run_stage_f; fi

log "============================================================"
log "EXHAUSTIVE PIPELINE SUMMARY: $TOTAL_PASS passed, $TOTAL_FAIL failed"
if [ "$DO_RUN" -eq 0 ]; then
    log "Build-only run: no tests executed. Re-run with --run to execute."
fi
log "Full log:      $LOG"
log "Failures:      $FAIL"

[ "$TOTAL_FAIL" -eq 0 ]
