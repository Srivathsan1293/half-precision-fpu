#!/usr/bin/env python3
"""tools/fo4.py — derive normalization constants from the local Sky130 liberty.

Outputs (JSON, also stdout when run directly):
  nand2_area_um2 : area of sky130_fd_sc_hd__nand2_1 (1 GE)
  inv_c_in_ff    : input pin capacitance of sky130_fd_sc_hd__inv_1
  fo4_ps         : fanout-of-4 inverter delay estimate at tt/1.8V/25C

FO4 estimate: delay of inv_1 driving a load of 4 x its own input pin
capacitance, taken from the NLDM cell_rise/cell_fall tables (fast input slew,
first index_1 entry). Used only as a cross-node normalization reference; the
true ring-oscillator FO4 is expected in the same ballpark.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, "sky130_fd_sc_hd__tt_025C_1v80.lib")
OUT = os.path.join(ROOT, "testing_results", "norm_metrics.json")

LOAD_STD = 0.01  # ns input slew used for the FO4 reading


def cell_block(lines, name):
    """Yield the keyword tokens of a single cell block as a flat list."""
    start = None
    for i, ln in enumerate(lines):
        if ln.strip().startswith('cell ("%s") {' % name):
            start = i
            break
    if start is None:
        raise SystemExit(f"cell {name} not found in liberty")
    depth = 0
    toks = []
    for ln in lines[start:]:
        toks.append(ln)
        depth += ln.count("{") - ln.count("}")
        if depth == 0 and ln.rstrip().endswith("}") and toks:
            break
    return toks


def cell_area(toks):
    for t in toks:
        m = re.match(r"\s*area\s*:\s*([\d.]+)", t)
        if m:
            return float(m.group(1))
    return None


def pin_cap(toks, pin):
    inside = False
    for i, t in enumerate(toks):
        if 'pin ("%s") {' % pin in t:
            inside = True
            continue
        if inside and t.count("{") == 0 and t.count("}") == 0:
            m = re.match(r"\s*capacitance\s*:\s*([\d.]+)", t)
            if m:
                return float(m.group(1))
        if inside and "pin (" in t and 'pin ("%s")' % pin not in t:
            break
    return None


def delay_at(toks, related, kind, load):
    """Read scalar cell_rise/cell_fall arc del_1_7_7 for related_pin.

    Scoped to the requested arc block by brace depth so one arc's index_2 /
    values tables never bleed into another arc's parse.
    """
    selected = None
    for i, t in enumerate(toks):
        if 'related_pin : "%s";' % related in t:
            selected = i
        if selected is not None and re.match(
                r'\s*%s\s*\(\"(del_\d+_\d+_\d+)\"\)\s*\{' % kind, t):
            start = i
            depth = 0
            for j in range(i, len(toks)):
                depth += toks[j].count("{") - toks[j].count("}")
                if depth == 0:
                    end = j
                    break
            idx2 = None
            vals = []
            block = toks[start:end + 1]
            for ki, t2 in enumerate(block):
                m2 = re.match(r"\s*index_2\(\"([^\"]+)\"\)", t2)
                if m2:
                    idx2 = [float(x) for x in m2.group(1).split(",")]
                if "values(" in t2:
                    buf = t2
                    jj = ki
                    while ");" not in buf:
                        jj += 1
                        buf = buf.rstrip("\\") + " " + block[jj].strip()
                    inner = buf[buf.index("(") + 1:buf.rindex(")")]
                    for tok in inner.split(","):
                        tok = tok.strip().strip('"').strip("\\")
                        if not tok:
                            continue
                        try:
                            vals.append(float(tok))
                        except ValueError:
                            continue
            if idx2 is None or not vals:
                return None
            cols = len(idx2)
            row = vals[:cols]  # first index_1 row = fast input slew
            lo, hi = min(idx2), max(idx2)
            if load <= lo:
                return row[0]
            if load >= hi:
                return row[-1]
            for a, b in zip(idx2, idx2[1:]):
                if a <= load <= b:
                    ra, rb = row[idx2.index(a)], row[idx2.index(b)]
                    return ra + (rb - ra) * (load - a) / (b - a)
            return row[-1]
    return None
    for a, b in zip(idx2, idx2[1:]):
        if a <= load <= b:
            ra, rb = row[idx2.index(a)], row[idx2.index(b)]
            return ra + (rb - ra) * (load - a) / (b - a)
    return row[-1]


def main():
    with open(LIB, errors="ignore") as fh:
        lines = fh.read().splitlines()

    nand = cell_block(lines, "sky130_fd_sc_hd__nand2_1")
    inv = cell_block(lines, "sky130_fd_sc_hd__inv_1")

    nand2_area = cell_area(nand)
    c_in = pin_cap(inv, "A")
    if None in (nand2_area, c_in):
        raise SystemExit("could not extract required cell attributes")

    load4 = 4.0 * c_in  # pF
    base_rise = delay_at(inv, "A", "cell_rise", 0.5 * c_in)
    fo4_rise = delay_at(inv, "A", "cell_rise", load4)
    base_fall = delay_at(inv, "A", "cell_fall", 0.5 * c_in)
    fo4_fall = delay_at(inv, "A", "cell_fall", load4)
    total_rise_dummy = load4  # (kept for symmetry; unused)
    fo4_r = (fo4_rise - base_rise) * 1000.0  # ns -> ps
    fo4_f = (fo4_fall - base_fall) * 1000.0
    fo4_ps = round((fo4_r + fo4_f) / 2.0, 1)
    fo4_ps_total = round((fo4_rise + fo4_fall) / 2.0 * 1000.0, 1)

    result = {
        "lib": os.path.basename(LIB),
        "nand2_area_um2": round(nand2_area, 4),
        "inv_c_in_ff": round(c_in * 1000.0, 2),
        "fo4_ps": fo4_ps,
        "fo4_ps_total": fo4_ps_total,
        "fo4_note": (
            f"fo4_ps={fo4_ps} is the load-slope FO4 increment "
            "(inv_1 delay at 4x C_in minus delay at 0.5x C_in, fast input slew); "
            f"fo4_ps_total={fo4_ps_total} is the absolute delay at 4x C_in. "
            "Ring-oscillator FO4 in sky130_fd_sc_hd typically lands between "
            "these (~50-90 ps). LTP-FO4 rows use fo4_ps."
        ),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(result, fh, indent=2)
    if len(sys.argv) > 2 and sys.argv[1] == "--json":
        print(json.dumps(result))
    else:
        print(f"nand2_area_um2 = {result['nand2_area_um2']}")
        print(f"inv C_in [ff]  = {result['inv_c_in_ff']}")
        print(f"FO4 [ps]       = {fo4_ps}  (rise {fo4_r:.1f}, fall {fo4_f:.1f})")
        print(f"wrote {OUT}")


if __name__ == "__main__":
    main()