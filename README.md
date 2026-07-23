# Half-Precision FPU (Float16)

A fast, IEEE 754 compliant half-precision (binary16) floating-point unit with multiply, add/subtract, and divide operations. Synthesized for the SkyWater 130 nm open-source PDK.

## 🎯 Project Goals

1. **IEEE 754 Compliance** — Correct rounding, NaN/Inf handling, subnormal support
2. **High Performance** — Minimize combinational critical path delay
3. **Pipelining** — Introduce pipeline stages to increase throughput and Fmax
4. **AXI4 Interface** — Wrap the FPU with an AXI4-Lite/AXI4-Stream interface for easy integration
5. **CPU Integration** — Verify with a RISC-V model CPU (e.g., PicoRV32, VexRiscv) on FPGA

## 🔧 Architecture

| Module | Description |
|--------|-------------|
| `fpu_FMUL.sv` | Booth-encoded Wallace tree multiplier |
| `fpu_FADDSUB.sv` | IEEE 754 compliant add/subtract |
| `fpu_FDIV.sv` | Iterative division via reciprocal ROM |
| `fpu_modules.sv` | Packed module definitions |

## 📊 Current PPA (Sky130 @ 25°C, 1.80 V)

### Area

| Module | Cells | Area (µm²) |
|--------|-------|------------|
| FMUL | 1,175 | 6,757.73 |
| FADDSUB | 710 | 4,322.90 |
| FDIV | 2,907 | 15,642.50 |
| **Total** | **4,792** | **26,723.13** |

### Timing (combinational, virtual 10 ns clock)

| Module | Critical Path | Max Frequency |
|--------|--------------|---------------|
| FMUL | 16.24 ns | 61.6 MHz |
| FADDSUB | 15.12 ns | 66.1 MHz |
| FDIV | 21.97 ns | 45.5 MHz |

### Power (10% toggle rate)

| Module | Internal | Switching | Leakage | Total |
|--------|----------|-----------|---------|-------|
| FMUL | 1.12 mW | 1.16 mW | 2.54 nW | 2.28 mW |
| FADDSUB | 0.54 mW | 0.64 mW | 1.58 nW | 1.19 mW |
| FDIV | 37.50 mW | 33.70 mW | 5.97 nW | 71.10 mW |

## 🗺️ Roadmap

- [ ] **Reduce critical path** — Re-synthesize with timing-driven ABC, restructure long paths
- [ ] **Pipeline stages** — Insert pipeline registers, balance stage delays
- [ ] **AXI4 wrapper** — AXI4-Lite control + AXI4-Stream data interface
- [ ] **RISC-V integration** — Test with PicoRV32 or VexRiscv on FPGA
- [ ] **FPGA prototyping** — Synthesize for Lattice/XC7 target board

## 🛠️ Tools

- **RTL**: SystemVerilog
- **Synthesis**: Yosys + ABC (Sky130 cell library)
- **Timing/Power**: OpenSTA 3.1.0
- **Simulation**: Verilator / Icarus Verilog
- **PDK**: SkyWater 130 nm (`sky130_fd_sc_hd`)

## 📁 Project Structure

```
.
├── src/
│   ├── fpu_FMUL.sv
│   ├── fpu_FADDSUB.sv
│   ├── fpu_FDIV.sv
│   └── fpu_modules.sv
├── tb/                     # Testbenches
├── ppa_MUL.ys              # Yosys synthesis scripts
├── ppa_ADDSUB.ys
├── ppa_DIV.ys
├── sta_FMUL.tcl            # OpenSTA timing/power scripts
├── sta_ADDSUB.tcl
├── sta_DIV.tcl
└── ppa_analytics.txt       # Full PPA report
```
