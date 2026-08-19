#!/usr/bin/env python3
"""Plot PPA Pareto curve for fpu_pcpi (10ns baseline + 15ns point)."""

import json
import matplotlib.pyplot as plt

def main():
    # Baseline: pnr_run7 @ 10 ns
    p7_path = "/home/srivathsann/Documents/uni/DECA/FPU_project/designs/fpu_pcpi/runs/pnr_run7"
    with open(f"{p7_path}/final/metrics.json") as f:
        m7 = json.load(f)
    
    # New run: pnr_run8 @ 15 ns (once complete)
    p8_path = "/home/srivathsann/Documents/uni/DECA/FPU_project/designs/fpu_pcpi/runs/pnr_run8"
    try:
        with open(f"{p8_path}/final/metrics.json") as f:
            m8 = json.load(f)
        has_new_run = True
    except FileNotFoundError:
        print("Waiting for pnr_run8 (15ns) to complete...")
        has_new_run = False
    
    clk_periods = [10.0] + ([15.0] if has_new_run else [])
    areas = [m7["design__instance__area"]] + \
            ([m8["design__instance__area"]] if has_new_run else [])  # µm²
    
    powers = [m7["power__total"]] + \
             ([m8["power__total"]] if has_new_run else [])  # W (OpenROAD metrics)
    
    plt.figure(figsize=(8, 5))
    
    for i, metric in enumerate(areas):
        color = '#1f77b4' if i == 0 else '#ff7f0e'
        marker = 'o' if i == 0 else 's'
        plt.plot(clk_periods[:len(powers)], areas[:len(powers)], 
                 label='Area', color=color, marker=marker)
    
    for i, metric in enumerate(powers):
        color = '#2ca02c' if i == 0 else '#d62728'
        marker = 'o' if i == 0 else '^'
        plt.plot(clk_periods[:len(areas)], powers[:len(areas)], 
                 label='Power', color=color, marker=marker)
    
    plt.xlabel("Clock Period (ns)")
    plt.ylabel("Metric")
    plt.title("fpu_pcpi PPA Pareto: Clock Target vs Area / Power")
    
    # Add lazy synthesis effect annotation
    if has_new_run and areas[0] > areas[1]:
        textstr = 'Lazy synthesis:\nRelaxed 15ns target → smaller area'
        plt.figtext(0.5, 0.01, textstr, ha='center', fontsize=9, 
                   family='monospace', color='gray', alpha=0.7)
    
    plt.legend()
    plt.grid(True, linestyle=":", alpha=0.5)
    
    output_path = "/home/srivathsann/Documents/uni/DECA/FPU_project/fpu_pcpi_ppa_pareto.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved Pareto plot: {output_path}")
    
    # Print comparison table
    print("\n=== PPA Comparison ===")
    for clk_period, (area, power) in zip(clk_periods[:len(areas)], 
                                          zip([m7["design__instance__area"]]+
                                              ([m8["design__instance__area"]] if has_new_run else []),
                                              [m7["power__total"]]+
                                              ([m8["power__total"]] if has_new_run else []))):
        area_kge = round(area / 3.7536, 1)
        power_mw = round(power * 1000, 1)
        print(f"{clk_period:4.1f} ns → {area:>8.0f} µm² ({area_kge:>5.1f} kGE) / {power_mw:>6.1f} mW")

if __name__ == "__main__":
    main()
