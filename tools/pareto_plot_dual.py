#!/usr/bin/env python3
"""
Build PPA Pareto frontier with dual Y-axes:
- Left axis: Area (µm²) 
- Right axis: Power (mW)
X-axis: Target Clock Period (ns)

Shows "lazy synthesis" effect and helps find optimal configuration.
"""

import json
import matplotlib.pyplot as plt
import numpy as np

def load_run_metrics(run_path):
    """Extract key metrics from OpenROAD metrics.json"""
    try:
        with open(f"{run_path}/final/metrics.json") as f:
            return json.load(f)
    except FileNotFoundError:
        return None

def main():
    # Clock periods for Pareto sweep
    clk_periods = [10.0, 13.33, 16.66, 20.0, 30.0]
    
    print("=== PPA Pareto Frontier Generation ===\n")
    print(f"Target clock periods: {clk_periods}")
    
    # Load existing runs and identify which are missing
    run_paths = [
        "designs/fpu_pcpi/runs/pnr_run7",      # 10ns - already exists
        "designs/fpu_pcpi/runs/pnr_run8_13ns", # 13.33ns
        "designs/fpu_pcpi/runs/pnr_run8_16ns", # 16.66ns  
        "designs/fpu_pcpi/runs/pnr_run8_20ns", # 20ns
        "designs/fpu_pcpi/runs/pnr_run8_30ns"  # 30ns
    ]
    
    areas = []
    powers = []
    valid_runs = []
    
    for i, (clk, run_path) in enumerate(zip(clk_periods, run_paths)):
        metrics = load_run_metrics(run_path)
        if metrics:
            area = round(metrics.get('design__instance__area', 0), 1)
            power = round(metrics.get('power__total', 0) * 1000, 2)
            valid_runs.append(run_path.split('/')[-1])
            print(f"{clk:6.1f} ns → {area:>8,.0f} µm² ({area/3.7536:.1f} kGE) / {power:>6.2f} mW")
        else:
            # Placeholder for missing runs - will be populated when complete
            print(f"{clk:6.1f} ns → [RUN IN PROGRESS...]")
    
    print("\n=== Plotting with Dual Y-Axes ===\n")
    
    # Create dual-axis plot
    fig, ax1 = plt.subplots(figsize=(10, 6))
    
    # Left axis (Area in µm²)
    color_area = '#1f77b4'
    ax1.set_xlabel('Target Clock Period (ns)', fontsize=12)
    ax1.set_ylabel('Stdcell Area (µm²)', color=color_area, fontsize=12)
    ax1.tick_params(axis='y', labelcolor=color_area)
    
    line1, = ax1.plot(clk_periods, areas if areas else [0]*len(clk_periods), 
                      'o-', color=color_area, linewidth=2, markersize=8, label='Area (µm²)')
    
    # Secondary right axis (Power in mW)
    ax2 = ax1.twinx()
    color_power = '#d62728'
    ax2.set_ylabel('Total Power (mW)', color=color_power, fontsize=12)
    ax2.tick_params(axis='y', labelcolor=color_power)
    
    line2, = ax2.plot(clk_periods, powers if powers else [0]*len(clk_periods),
                      's-', color=color_power, linewidth=2, markersize=8, label='Power (mW)')
    
    # Customize plot
    ax1.set_title('fpu_pcpi PPA Pareto Frontier: Lazy Synthesis Trade-off\n' +
                  '(Left Y: Area µm² | Right Y: Power mW)', fontsize=14, fontweight='bold')
    
    # Add "lazy synthesis" annotation
    if len(areas) >= 2 and areas[0] > areas[-1]:
        textstr = 'Lazy synthesis effect:\nRelaxed clock → ↓ Area & ↓ Power'
        ax1.figtext(0.5, 0.01, textstr, ha='center', fontsize=9, 
                   family='monospace', color='gray', alpha=0.6)
    
    # Add "optimal configuration" annotation for smallest clock that meets targets
    if len(areas) >= 2:
        # Find transition point where power/area drop significantly
        optimal_idx = 1
        for i in range(1, len(areas)):
            area_drop = areas[i-1] - areas[i]
            power_drop = powers[i-1] - powers[i] if powers else 0
            if area_drop > 0.5 and power_drop > 0:
                optimal_idx = i
                break
        
        if optimal_idx < len(clk_periods):
            optimal_clk = clk_periods[optimal_idx]
            ax1.annotate(f'Optimal: {optimal_clk}ns', xy=(optimal_clk, areas[optimal_idx]),
                        xytext=(0, 20), textcoords='offset points',
                        bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.7),
                        fontsize=9, arrowprops=dict(arrowstyle='->', color=color_power))
    
    ax1.grid(True, linestyle=':', alpha=0.5)
    ax2.grid(True, linestyle=':', alpha=0.5)
    
    # Add legend with both metrics
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper right')
    
    # Save plot
    output_path = "/home/srivathsann/Documents/uni/DECA/FPU_project/fpu_pcpi_ppa_pareto_dual.png"
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved Pareto plot: {output_path}")
    
    # Print summary table
    print("\n=== PPA Summary Table ===")
    print("Clock(ns) │ Instances │ Area(µm²) │ kGE  │ Power(mW) │ Fmax(MHz)")
    print('-' * 70)
    for clk, (metrics_path, metrics) in enumerate(zip(run_paths, [load_run_metrics(p) or None for p in run_paths])):
        if metrics:
            inst = metrics.get('design__instance__count', 0)
            area = metrics.get('design__instance__area', 0)
            power = metrics.get('power__total', 0) * 1000
            kge = area / 3.7536
            fmax = 1000.0 / clk
            print(f'{clk:9.1f} │ {inst:>7,} │ {area:>8,.0f} │ {kge:>4.1f} │ {power:>9.2f} │ {fmax:>6.0f}')
        else:
            print(f'{clk:9.1f} │ [in progress] │ [waiting] │  —  │ [waiting] │  — ')

if __name__ == "__main__":
    main()
