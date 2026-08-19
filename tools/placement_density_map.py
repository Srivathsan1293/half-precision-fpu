import re
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

def parse_def_placement(def_path, exclude_fill=True, bins=48):
    die = None
    cells = []
    with open(def_path) as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"DIEAREA \( (\d+) (\d+) \) \( (\d+) (\d+) \) ;", line)
        if m:
            die = tuple(int(x) for x in m.groups())
            break
    in_comp = False
    for line in lines:
        if line.startswith("COMPONENTS"):
            in_comp = True
            continue
        if line.startswith("END COMPONENTS"):
            break
        if in_comp:
            m = re.match(r"\s*-\s+(\S+)\s+(\S+)\s+(?:\+ SOURCE \S+ )?\+ (PLACED|FIXED) \( (\d+) (\d+) \) (\S+) ;", line)
            if m:
                name, cell, place, x, y, orient = m.groups()
                x, y = int(x), int(y)
                if exclude_fill and ("FILLER" in name or "decap" in cell or "TAP" in cell):
                    continue
                cells.append((x, y))
    return die, cells

def main():
    def_path = sys.argv[1]
    out_path = sys.argv[2]
    bins = int(sys.argv[3]) if len(sys.argv) > 3 else 48
    die, cells = parse_def_placement(def_path)
    if die is None or not cells:
        print("ERROR: could not parse DEF")
        sys.exit(1)
    dx, dy = die[2] - die[0], die[3] - die[1]
    H, xedges, yedges = np.histogram2d(
        [c[0] for c in cells], [c[1] for c in cells], bins=bins,
        range=[[die[0], die[2]], [die[1], die[3]]],
    )
    bin_area = (dx / bins) * (dy / bins)
    # normalize: report as % of stdcell area per bin, where each cell ~ avg cell area
    avg_cell = 1.0  # will rescale below
    density = H / H.max() * 100.0 if H.max() > 0 else H

    fig, ax = plt.subplots(figsize=(8, 8))
    im = ax.imshow(
        density.T, origin="lower",
        extent=[die[0] / 1000, die[2] / 1000, die[1] / 1000, die[3] / 1000],
        cmap="viridis", aspect="equal", vmin=0, vmax=100,
    )
    cbar = plt.colorbar(im, ax=ax, label="Relative placement density (%)")
    ax.set_title(f"Placement Density Map — fpu_pcpi (DEF, {len(cells):,} logic cells)")
    ax.set_xlabel("X (µm)")
    ax.set_ylabel("Y (µm)")
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    print(f"saved {out_path}")
    print(f"  die: {dx} x {dy} µm, logic cells: {len(cells)}, peak bin: {H.max():.0f} cells")

if __name__ == "__main__":
    main()
