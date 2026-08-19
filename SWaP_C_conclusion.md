# SWaP-C Real-Time Context: fpu_pcpi for FOC Robotics

## Executive Summary

The custom `fpu_pcpi` floating-point unit delivers exceptional **SWaP-C balance** (Size, Weight, Power, and Cost) optimized for real-time edge computing in robotics applications. Specifically, it meets the stringent timing requirements of Field-Oriented Control (FOC) motor loops while operating within battery-operated power budgets.

---

## Key Metrics Summary (post-routed, sky130_fd_sc_hd, tt 1.8V/25°C)

| Metric | 10 ns Target (Baseline) | 15 ns Target (Relaxed) |
|--------|------------------------|------------------------|
| **Stdcell Area** | 31,290 µm² (8.3 kGE) | 30,206 µm² (8.0 kGE) |
| **Power (OpenROAD, max_tt)** | 8.19 mW | 5.32 mW |
| **Fmax** | 100 MHz | 67 MHz |
| **Instances** | 4,137 | 4,105 |

> *Power from OpenROAD post-route STA at default activity (max_tt_025C_1v80 corner), `designs/fpu_pcpi/pareto/runs/`. Leakage is negligible (< 0.1 mW).*

---

## Real-Time Performance Guarantee

### The FOC Control Loop Requirement

In high-speed Field-Oriented Control (FOC) for robotics, the microcontroller must execute a strict **real-time control loop**:

```
┌─────────────────────────────────────────┐
│  Encoder Read    →  Calculate Control    │
│      ↓                                    │
│  Divide Operation                       │  Deadline: ~5–10 µs
│      ↓                                    │
│  PWM Fire Signal                        │
└─────────────────────────────────────────┘
```

#### Software Emulation Failure Scenario

Using software-emulated division (e.g., a C `float` library division routine):
- Each division operation stalls the CPU for **hundreds to thousands of cycles**
- Total latency: e.g., 500–2,000 cycles at ~80 MHz → **6–25 µs**
- **Result**: Consumes most or all of the FOC real-time deadline, leaving no headroom for the rest of the control math

#### Hardware Acceleration Solution

The `fpu_pcpi` hardware SRT divider:
- Completes division in a **fixed 12 cycles** (deterministic, handshake-bounded latency)
- At 100 MHz Fmax: **120 ns = 0.12 µs** latency
- **Result**: Meets the FOC deadline with >40× margin, leaving the CPU free for sensor read and PWM scheduling

---

## SWaP-C Analysis

### Size (S)
- **Stdcell area**: ~31 kµm² (single-lane FP16/FP32 unit, ~8.3 kGE)
- Compared to: Full RISC-V core with register file (~300 kµm² in the combined SoC)
- **Justification**: Small footprint enables integration into constrained embedded ASICs

### Weight (W)
- Directly correlated to power density and silicon real estate
- fpu_pcpi's compact 8 kGE design minimizes chip area overhead

### Power (P)
- **Dynamic power**: ~8.2 mW @100 MHz Fmax (10 ns target, tt corner)
- **Leakage power**: negligible (< 0.1 mW)
- **Total power envelope**: Well within typical embedded ASIC budgets for motor control loops

### Cost (C)
- SkyWater 130 nm PDK: Mature, open-source design kit
- OpenLane PnR flow: Automated physical design with clean signoff (0 DRC, 0 LVS violations)
- Single-lane scalar design avoids area explosion from vector/parallelism

---

## Battery-Autonomy Validation

For battery-operated robotics applications:

**Power Budget Calculation:**
```
fpu_pcpi @100 MHz Fmax:     ~8.2 mW
FOC control loop frequency: 2–5 kHz (period 200–500 µs)
Divisions per second:       2,000–5,000
Energy per division:        (8.2 mW × 12 cycles/op) / 100 MHz ≈ 1.0 nJ/op
Daily energy for divisions: 1.0 nJ × 3,000 ops/s × 86,400 s/day
                             ≈ 260 mJ/day

For a 1000 mAh Li-Ion battery at 3.7 V (= 3.7 Wh = 13.32 MJ):
Power overhead:     < 0.01% of daily energy budget
```

**Conclusion**: The fpu_pcpi's ~8 mW power draw is negligible for battery-operated real-time control systems.

---

## SWaP-C Conclusion

The `fpu_pcpi` FPU delivers exceptional **SWaP-C balance**:
- ✅ **Size**: Compact 8 kGE fits within constrained embedded ASICs
- ✅ **Weight**: Minimal die area reduces silicon mass and fabrication complexity
- ✅ **Power**: ~5–8 mW enables battery-operated operation at real-time frequencies
- ✅ **Cost**: Open-source PDK + automated PnR = low NRE cost

**This SWaP-C balance is ideal for edge AI/robotics applications where battery autonomy matters.**

---

## Deterministic Latency Guarantee

The PCPI (Pico Co-Processor Interface) FSM guarantees (verified by `run_fsm.sh`, 29/29 checks pass):
- FADD/FSUB/FMUL operations: **1 cycle each** (single registered datapath stage)
- FDIV operation: **fixed 12 cycles** (start-gated sequential SRT core, quotient stable the cycle after `done`)
- Handshake protocol ensures no premature completion signals and no re-trigger while `pcpi_valid` is held

This deterministic timing model is essential for hard real-time systems, where worst-case execution time must be known and bounded.

---

## References

1. OpenROAD post-route metrics (OpenSTA power/timing reports, `designs/fpu_pcpi/pareto/runs/pareto_{10,15}ns/54-openroad-stapostpnr/`)
2. FSM timing spec: `src/fpu_pcpi.sv`, `tb/tb_pcpi_fsm.cpp`
3. Pre-route fpu_pcpi analysis in `testing_results/flops_*/flops_report.md`
4. PicoRV32 PCPI protocol specification
