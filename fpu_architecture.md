# Half-Precision (fp16) FPU Architecture Documentation

This document provides a conceptual overview and flowcharts for the half-precision IEEE-754 FPU coprocessor (`fpu_pcpi`) integrated with the PicoRV32 RISC-V processor.

---

## 1. System Architecture & Overview

The system consists of the **PicoRV32 CPU core** communicating with the **`fpu_pcpi` coprocessor wrapper** over the Pico Co-Processor Interface (PCPI). The FPU supports four core arithmetic operations (`FADD`, `FSUB`, `FMUL`, `FDIV`) in half-precision format (`binary16`).

```mermaid
graph TD
    subgraph PicoRV32 CPU
        CPU[CPU Core] -->|pcpi_valid, insn, rs1, rs2| PCPI_BUS[PCPI Bus]
        PCPI_BUS -->|pcpi_rd, wr, ready, wait| CPU
    end

    subgraph fpu_pcpi Wrapper
        PCPI_BUS --> FSM[3-State FSM & Decoder]
        FSM -->|fpu_op / start / operands| DATAPATH[FPU Datapath fpu_test]
    end

    subgraph FPU Datapath
        DATAPATH -->|FADD / FSUB| ADD[fpu_FADDSUB.sv]
        DATAPATH -->|FMUL| MUL[fpu_FMUL.sv]
        DATAPATH -->|FDIV| DIV[fpu_FDIV.sv]
    end
```

### Operation Summary
- **FADD / FSUB**: Single-cycle registered arithmetic datapath (Adder/subtractor with exponent alignment and normalization).
- **FMUL**: Booth-encoded Wallace-tree multiplier with single-cycle registered pipeline stage.
- **FDIV**: Sequential radix-4 SRT divider with fixed 12-cycle deterministic latency.

---

## 2. PCPI Protocol & FSM Control Flow

The `fpu_pcpi` wrapper implements a 3-state Finite State Machine (`IDLE` → `COMPUTE` → `DONE`) to manage the handshake with the PicoRV32 CPU.

```mermaid
stateDiagram-v2
    [*] --> IDLE
    
    IDLE --> COMPUTE : on<br/>start_compute<br/>(rising edge of valid)<br/>& recognized instruction
    
    state COMPUTE {
        [*] --> Processing
        Processing --> FADD_FSUB_FMUL : 1-cycle (registered)
        Processing --> FDIV_SRT : 12-cycles (fixed SRT schedule)
    }
    
    COMPUTE --> DONE : on<br/>answer_valid<br/>(result ready)
    
    DONE --> IDLE : when<br/>pcpi_valid falls<br/>(instruction retired)
```

### Handshake Semantics
1. **`pcpi_valid`**: Level signal asserted by the CPU when executing an unhandled instruction. It stays high until `pcpi_ready` is asserted.
2. **`pcpi_wait`**: Asserted immediately upon instruction acceptance to stall the CPU and suppress illegal-instruction timeouts.
3. **`pcpi_ready` & `pcpi_wr`**: Asserted together for exactly **one cycle** when the result is valid, writing back `pcpi_rd` into the destination register (`rd`).

---

## 3. FDIV Operation & Start-Gated SRT Divider Flow

Division (`FDIV`) is the most complex operation, utilizing a deterministic, start-gated radix-4 SRT core that operates over a fixed 12-cycle schedule.

```mermaid
sequenceDiagram
    autonumber
    participant CPU as PicoRV32 CPU
    participant Wrapper as fpu_pcpi Wrapper
    participant DIV as fpu_FDIV.sv
    participant SRT as SRT Divider

    Note over Wrapper: Decode FDIV, assert<br/>pcpi_wait=1
    Wrapper->>DIV: start pulse, a & b operands
    
    Note over DIV: Latch operands,<br/>pending metadata
    DIV->>SRT: start pulse
    
    loop 8 Radix-4 iterations<br/>(11 cycles total)
        SRT->>SRT: quotient calc & remainder updates
    end
    
    SRT-->>DIV: srt_done pulse (cycle 11)
    Note over DIV: Capture quotient,<br/>normalize/round
    DIV-->>Wrapper: ans valid (cycle 12)
    
    Wrapper->>CPU: pcpi_ready=1, pcpi_wr=1,<br/>pcpi_rd=result
    CPU->>Wrapper: pcpi_valid=0<br/>(retire instruction)
    Note over Wrapper: Return to IDLE state
```

### SRT Divider Core Details (`fdiv_datapath_blocks.sv`)
- **Radix-4 Division**: Computes two bits of quotient per clock cycle using redundant digit representation.
- **Start-Gated Control**: The core remains idle between divisions. A single `start` pulse reloads operands, runs 8 iterations, and signals completion via `srt_done` 11 cycles later.
- **Deterministic Latency**: Guarantees worst-case execution time (WCET) for real-time control loops such as Field-Oriented Control (FOC) motor drivers.
