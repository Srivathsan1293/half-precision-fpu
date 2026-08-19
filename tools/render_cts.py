#!/usr/bin/env python3
"""Render the CTS clock-tree routing from a routed DEF to a high-res PNG.

Draws the full routed die in dark gray as context and overlays the clock
network routing in bright sky130 layer colors, mirroring the KLayout
screenshots used elsewhere in the report.
"""
import argparse
import re
from PIL import Image, ImageDraw

LAYER_CFG = {
    "li1":  {"color": (232, 213, 163), "width": 150},
    "mcon": {"color": (255, 255, 255), "width": 100},
    "met1": {"color": (0x41, 0x66, 0xFF), "width": 170},
    "via":  {"color": (0x8A, 0x8A, 0x8A), "width": 80},
    "met2": {"color": (0x00, 0xB0, 0x20), "width": 140},
    "via2": {"color": (0x8A, 0x8A, 0x8A), "width": 80},
    "met3": {"color": (0xFF, 0x7F, 0x00), "width": 280},
    "via3": {"color": (0x8A, 0x8A, 0x8A), "width": 80},
    "met4": {"color": (0xFF, 0x00, 0xFF), "width": 560},
    "via4": {"color": (0x8A, 0x8A, 0x8A), "width": 80},
    "met5": {"color": (0xFF, 0xD7, 0x00), "width": 1600},
}
CONTEXT_COLOR = (72, 72, 72)
BOUNDARY_COLOR = (150, 150, 150)

POINT_RE = re.compile(r"\(\s*(\d+|\*)\s+(\d+|\*)\s*\)")
STMT_RE = re.compile(r"^\s*(?:\+\s*)?(?:ROUTED|NEW|FIXED)\s+(\w+)(?:\s+(\d+))?")


def parse_def(def_path):
    """Return (ctx_polys, clk_polys, clk_vias, cts_bufs, diearea, clk_names)."""
    txt = open(def_path).read()

    diearea = None
    m = re.search(r"DIEAREA\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", txt)
    if m:
        diearea = tuple(int(v) for v in m.groups())

    ctx_polys = []
    clk_polys = []
    clk_vias = []
    cts_bufs = []
    clk_names = set()

    def is_clock(name):
        return name == "clk" or name.startswith("clknet_")

    def walk_body(lines, clock):
        last_x = last_y = None

        for line in lines:
            sm = STMT_RE.match(line)
            if not sm:
                continue
            layer = sm.group(1)
            cfg = LAYER_CFG.get(layer)
            if cfg is None:
                continue
            width = int(sm.group(2)) if sm.group(2) else cfg["width"]

            pts = []
            for pm in POINT_RE.finditer(line):
                a, b = pm.groups()
                a = last_x if a == "*" else int(a)
                b = last_y if b == "*" else int(b)
                last_x, last_y = a, b
                pts.append((a, b))
            if not pts:
                continue

            if layer.startswith("via"):
                for pt in pts:
                    if clock:
                        clk_vias.append((layer, pt, width))
                continue

            if len(pts) == 1:
                if clock:
                    clk_vias.append((layer, pts[0], width))
                continue

            poly = (layer, width, pts)
            if clock:
                clk_polys.append(poly)
            else:
                ctx_polys.append(poly)

    for section, section_is_clock in (("SPECIALNETS", False), ("NETS", True)):
        start = txt.index(section) + len(section)
        end = txt.index("END " + section)
        body = txt[start:end]
        current_name = None
        current_lines = []
        for line in body.splitlines():
            nm = re.match(r"^\s*-\s+(\S+)\s+", line)
            if nm:
                if current_name is not None:
                    clock = section_is_clock and is_clock(current_name)
                    if clock:
                        clk_names.add(current_name)
                    walk_body(current_lines, clock)
                current_name = nm.group(1)
                current_lines = []
            else:
                current_lines.append(line)
        if current_name is not None:
            clock = section_is_clock and is_clock(current_name)
            if clock:
                clk_names.add(current_name)
            walk_body(current_lines, clock)

    cs = txt.index("COMPONENTS") + len("COMPONENTS")
    ce = txt.index("END COMPONENTS")
    for line in txt[cs:ce].splitlines():
        nm = re.match(r"\s*-\s+\S+\s+(\S+)", line)
        if not nm or "clk" not in nm.group(1).lower():
            continue
        pm = re.search(r"\bPLACED\s*\(\s*(-?\d+)\s+(-?\d+)\s*\)", line)
        if pm:
            dm = re.search(r"clk(?:buf|inv|invlp)_(\d+)", nm.group(1))
            drive = int(dm.group(1)) if dm else 1
            cts_bufs.append((int(pm.group(1)), int(pm.group(2)), drive))

    return ctx_polys, clk_polys, clk_vias, cts_bufs, diearea, clk_names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("def_path")
    ap.add_argument("out_png")
    ap.add_argument("--size", type=int, default=2560)
    ap.add_argument("--supersample", type=int, default=2)
    args = ap.parse_args()

    ctx_polys, clk_polys, clk_vias, cts_bufs, diearea, clk_names = parse_def(args.def_path)
    if not diearea:
        raise SystemExit("no DIEAREA found")

    x0, y0, x1, y1 = diearea
    dw, dh = x1 - x0, y1 - y0
    ss = args.supersample
    W = H = args.size * ss
    s = ((args.size - 2 * 30) / max(dw, dh)) * ss
    px_w, px_h = dw * s, dh * s
    x_off = (W - px_w) / 2
    y_off = (H - px_h) / 2

    def to_px(x, y):
        px = x_off + (x - x0) * s
        py = H - y_off - (y - y0) * s
        return px, py

    img = Image.new("RGB", (W, H), (0, 0, 0))
    dr = ImageDraw.Draw(img)

    def draw_polys(polys, bright):
        for layer, width, pts in polys:
            cfg = LAYER_CFG[layer]
            color = cfg["color"] if bright else CONTEXT_COLOR
            w = max(4, round(width * s)) if bright else max(2, round(width * s))
            for i in range(len(pts) - 1):
                dr.line([to_px(*pts[i]), to_px(*pts[i + 1])], fill=color, width=w)

    draw_polys(ctx_polys, bright=False)

    for layer, (x, y), width in clk_vias:
        px, py = to_px(x, y)
        r = max(4, round(width * s * 0.9))
        color = (255, 255, 255) if layer.startswith("via") else LAYER_CFG[layer]["color"]
        dr.ellipse([px - r, py - r, px + r, py + r], fill=color)

    draw_polys(clk_polys, bright=True)

    for x, y, drive in cts_bufs:
        px, py = to_px(x, y)
        h = max(10, round((8 + 6 * (drive.bit_length() - 1)) * ss))
        box = [px, py - h, px + h, py]
        dr.rectangle(box, fill=(255, 255, 0), outline=(255, 255, 255), width=max(1, round(0.5 * s)))

    ax, ay = to_px(x0, y0)
    bx, by = to_px(x1, y1)
    dr.rectangle(
        [min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)],
        outline=BOUNDARY_COLOR,
        width=max(2, round(1.5 * s)),
    )

    if ss > 1:
        img = img.resize((args.size, args.size), Image.LANCZOS)
    img.save(args.out_png)
    print(
        f"wrote {args.out_png}: {args.size}x{args.size}, "
        f"{len(ctx_polys)} context polys, {len(clk_polys)} clock polys, "
        f"{len(clk_vias)} clock vias, clock nets: {len(clk_names)}"
    )


if __name__ == "__main__":
    main()