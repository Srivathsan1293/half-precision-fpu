import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

data = list(csv.DictReader(open("testing_results/pareto_curve/pareto_data.csv")))
clk = [float(r["clock_ns"]) for r in data]
area = [float(r["area_um2"]) / 1000.0 for r in data]
power = [float(r["power_mW"]) for r in data]
fmax = [1000.0 / c for c in clk]

fig, ax1 = plt.subplots(figsize=(8, 5))

ax1.set_xlabel("Clock period (ns)")
ax1.set_ylabel("Area (kµm²)", color="tab:red")
ax1.plot(clk, area, "o-", color="tab:red", label="Area (kµm²)")
ax1.tick_params(axis="y", labelcolor="tab:red")
ax1.set_ylim(26, 34)
for x, y in zip(clk, area):
    ax1.annotate(f"{y:.1f}k", (x, y), textcoords="offset points", xytext=(4, 6), color="tab:red", fontsize=8)

ax2 = ax1.twinx()
ax2.set_ylabel("Power (mW)", color="tab:blue")
ax2.plot(clk, power, "s--", color="tab:blue", label="Power (mW)")
ax2.tick_params(axis="y", labelcolor="tab:blue")
ax2.set_ylim(0, 10)
for x, y in zip(clk, power):
    ax2.annotate(f"{y:.2f}", (x, y), textcoords="offset points", xytext=(4, -12), color="tab:blue", fontsize=8)

for i, x in enumerate(clk):
    ax1.axvline(x, color="gray", alpha=0.2, lw=0.5)
    ax1.text(x, 30.4, f"{fmax[i]:.0f}MHz", ha="center", fontsize=7, color="gray")

lines = ax1.get_lines() + ax2.get_lines()
ax1.legend(lines, [l.get_label() for l in lines], loc="upper right")
ax1.set_title("fpu_pcpi PPA Pareto — post-routed (sky130_fd_sc_hd, tt 1.8V/25°C, IO delay 0)")
plt.tight_layout()
plt.savefig("testing_results/pareto_curve/pareto_curve.png", dpi=150)
print("saved testing_results/pareto_curve/pareto_curve.png")