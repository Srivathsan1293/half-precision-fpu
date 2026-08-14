#!/usr/bin/env bash
# tools/sv2v_fpnew.sh — SV→Verilog conversion of the FPNew slice for Yosys 0.66.
#
# Why: Yosys 0.66's SV frontend cannot parse several SystemVerilog constructs
# used by fpnew (packaged `'{default:}` assignment patterns, `return` in
# functions, // pragma translate_off SVA guards). We therefore pass the eight
# fpnew sources (vendored UNCHANGED at 355c388, incl. common_cells +
# fpu_div_sqrt_mvp submodules) plus our wrapper through sv2v -- the standard
# converter used by the pulp ecosystem -- and synthesize the resulting plain
# Verilog. The vendored sources are never modified.
#
# Output: $1 = path of the converted Verilog file (written by this script).
# Usage: tools/sv2v_fpnew.sh out/tmp_fp16_converted.v
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SRC=(
  third_party/fpnew/src/fpnew_pkg.sv
  third_party/fpnew/src/common_cells/src/cf_math_pkg.sv
  third_party/fpnew/src/common_cells/src/rr_arb_tree.sv
  third_party/fpnew/src/common_cells/src/lzc.sv
  third_party/fpnew/src/fpnew_opgroup_fmt_slice.sv
  third_party/fpnew/src/fpnew_opgroup_multifmt_slice.sv
  third_party/fpnew/src/fpnew_opgroup_block.sv
  third_party/fpnew/src/fpnew_fma.sv
  third_party/fpnew/src/fpnew_fma_multi.sv
  third_party/fpnew/vendor/cvw/fma/fmalza.sv
  third_party/fpnew/src/fpnew_rounding.sv
  third_party/fpnew/src/fpnew_classifier.sv
  third_party/fpnew/src/fpnew_cast_multi.sv
  third_party/fpnew/src/fpnew_divsqrt_multi.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/defs_div_sqrt_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/control_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/nrbd_nrsc_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/norm_div_sqrt_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/iteration_div_sqrt_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/preprocess_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/div_sqrt_top_mvp.sv
  third_party/fpnew/src/fpu_div_sqrt_mvp/hdl/div_sqrt_mvp_wrapper.sv
  synth_scripts/fpnew_bench_top.sv
)
OUT="$1"
mkdir -p "$(dirname "$OUT")"

SV2V="${SV2V:-$(command -v sv2v || echo /tmp/opencode/sv2v-Linux/sv2v-Linux/sv2v)}"
"$SV2V" -DVERILATOR \
  -I third_party/fpnew/src \
  -I third_party/fpnew/src/common_cells/include \
  "${SRC[@]}" > "$OUT" 2>sv2v_stderr.tmp

# Strip non-synthesizable system tasks ($finish/$fatal/$display beside the
# fatal-path message bodies in fpnew_pkg functions and divsqrt small-sqrt
# guards, and $time args). These only ever execute on impossible/unreachable
# inputs; our wrapper constrains them away. Loop until fixed point.
python3 - "$OUT" << 'EOF'
import re, sys
p = sys.argv[1]
src = open(p).read()
# Only strip *fatal/debug* system tasks ($finish/$fatal/$display/$write/$strobe/
# $monitor/$info/$warning/$error/$fwrite/$fclose). $clog2/$clogb2 etc. are kept.
TASKS = r'\$(?:finish|fatal|display|write|strobe|monitor|info|warning|error|fwrite|fclose)'
for _ in range(8):
    n = src
    src = re.sub(r'\s*' + TASKS + r'\s*\([^;]*\);', '', src)
    src = re.sub(r'\b\$time\b', '0', src)
    if src == n:
        break
# Collect signals that are used as instance port connections: .port_name(signal_name)
inst_sigs = set(re.findall(r'\.\s*[A-Za-z_]\w*\s*\(\s*([A-Za-z_]\w*(?:\[[^\]]+\])?)\s*\)', src))
# Remove assigns that drive those signals to constants (1'b0 or 1'b1)
for _ in range(8):
    n = src
    for sig in list(inst_sigs):
        # match assign <sig> = 1'b0; or 1'b1; (allow whitespace)
        src = re.sub(r"(?m)^[ \t]*assign[ \t]+" + re.escape(sig) + r"[ \t]*=[ \t]*1'b[01][ \t]*;[ \t]*\n?", "", src)
    if src == n:
        break
# If anything still assigns constant single-bit values to nets, strip those assigns
# (These originate from sv2v when branches resolve to constants in some parameterizations.)
for _ in range(4):
    n = src
    src = re.sub(r"(?m)^[ \t]*assign[ \t]+[A-Za-z_]\w*(?:\[[^\]]+\])?[ \t]*=[ \t]*1'b[01][ \t]*;[ \t]*\n?", "", src)
    if src == n:
        break
# Remove assigns that write constant values into fmt_outputs slices — these create nets tied to constants
src = re.sub(r"(?m)^[ \t]*assign[ \t]+fmt_outputs\[[^\]]+\][^;]*;[ \t]*\n?", "", src)
# drop now-empty `initial begin end` blocks and bare `initial` lines left
# behind by system-task removal
src = re.sub(r'initial\s+begin\s*\n\s*end\b', '', src)
src = re.sub(r'^[ \t]*initial[ \t]*$', '', src, flags=re.M)
open(p, 'w').write(src)
EOF
rm -f sv2v_stderr.tmp
echo "sv2v OK -> $OUT ($(wc -l < \"$OUT\") lines)"