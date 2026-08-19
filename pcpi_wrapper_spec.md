# PCPI Wrapper Specification (fpu_pcpi)

This document defines the inputs, outputs, and handshake requirements for the
PCPI coprocessor wrapper that connects the half-precision FPU
(`src/fpu_test.sv`) to PicoRV32 (`third_party/picorv32.v`) through the Pico
Co-Processor Interface (PCPI). It is written as the specification you design
the `src/fpu_pcpi.sv` state machine against.

The wrapper is instantiated by `tb/soc_fpu_top.sv` when the build defines
`HAS_FPU_PCPI` (see `run_cpu_test.sh fpu`).

---

## 1. Module interface

```
                ┌───────────────────────────┐
                │          fpu_pcpi         │
                │                           │
  clk ─────────►│              a[15:0] ─────►│──► fpu_test
  resetn ──────►│                           │       │
                │              b[15:0] ─────►│──► fpu_test
 pcpi_valid ───►│              op[1:0] ─────►│──► fpu_test
 pcpi_insn ────►│                           │       │
 pcpi_rs1 ─────►│             ans[15:0] ◄────│◄── fpu_test
 pcpi_rs2 ─────►│                           │
                │              clk ─────────►│──► fpu_test
  pcpi_wr ◄─────│                           │
  pcpi_rd ◄─────│                           │
  pcpi_wait ◄───│                           │
  pcpi_ready ◄──│                           │
                └───────────────────────────┘
```

### 1.1 CPU-side ports (PCPI bus from/to PicoRV32)

| Port | Width | Direction | Description |
|------|-------|-----------|-------------|
| `clk` | 1 | in | System clock. All state machine transitions happen on the rising edge. |
| `resetn` | 1 | in | Active-low reset. Hold low for several cycles; the FSM must return to IDLE. |
| `pcpi_valid` | 1 | in | **Level**, asserted by the CPU while it is presenting an instruction to coprocessors. **Stays high until you assert `pcpi_ready`** (see §2.1). |
| `pcpi_insn` | 32 | in | The instruction word. **Stable while `pcpi_valid` is high.** Decode opcode + funct7 + funct3 from here (§3). |
| `pcpi_rs1` | 32 | in | Value of register `rs1`. **Stable while `pcpi_valid` is high.** Low 16 bits are FPU operand `a`. |
| `pcpi_rs2` | 32 | in | Value of register `rs2`. **Stable while `pcpi_valid` is high.** Low 16 bits are FPU operand `b`. |
| `pcpi_wr` | 1 | out | Write-enable for the result. Assert together with `pcpi_ready` for one cycle to make the CPU write `pcpi_rd` into the instruction's `rd` register. |
| `pcpi_rd` | 32 | out | Result value returned to the CPU. Captured by the CPU in the cycle `pcpi_ready` is high (written back to `rd` only when `pcpi_wr` is also high). Must hold `{16'b0, ans}` at that time. |
| `pcpi_wait` | 1 | out | **Stall request.** Assert as soon as you accept the instruction and keep it asserted until you assert `pcpi_ready`. It (a) freezes the CPU and (b) **suppresses the illegal-instruction timeout** (§2.3). |
| `pcpi_ready` | 1 | out | **Completion.** Assert for exactly **one cycle** when the result is valid. The CPU then commits the instruction, writes back `pcpi_rd` if `pcpi_wr` is high, and continues with the next instruction. |

### 1.2 FPU-side ports (to/from `fpu_test`)

`fpu_test` is now **single-cycle for FADD/FSUB/FMUL** (registered datapaths,
result valid 1 cycle after the operands are presented) and a **start-gated
radix-4 SRT core for FDIV** that signals completion via its `done` pulse (fixed
11-cycle schedule after a `start` pulse; the wrapper uses the done-handshake
described in §4).

| Port | Width | Direction | Description |
|------|-------|-----------|-------------|
| `a` | 16 | out | FPU operand A = `pcpi_rs1[15:0]` (binary16). |
| `b` | 16 | out | FPU operand B = `pcpi_rs2[15:0]` (binary16). |
| `op` | 2 | out | Operation select: `2'b00`=ADD, `2'b01`=SUB, `2'b10`=MUL, `2'b11`=DIV. |
| `clk` | 1 | out | Same clock as the wrapper (share the input clock). |
| `start` | 1 | out | **FDIV start pulse.** Asserted for exactly one cycle on the accept edge; the DIV latches `a`/`b` then. Must be FDIV-only and never re-triggered while `pcpi_valid` is held. |
| `ans` | 16 | in | FPU result. **Valid 1 cycle after the operands are presented for FADD/FSUB/FMUL; valid for FDIV on the cycle after the `done` pulse** (fixed 12 cycles after `start`). |
| `done` | 1 | in | **FDIV completion pulse** (1 cycle, 11 cycles after `start`). The wrapper uses it to commit; see §4. |

---

## 2. CPU-side handshake semantics

Reference implementation: `picorv32_pcpi_mul` in `third_party/picorv32.v`
(lines ~2195–2316) — the canonical "accept + stall + writeback" pattern.

### 2.1 `pcpi_valid` is a level, not a pulse

When the CPU decodes an instruction it does not implement natively, it
asserts `pcpi_valid` and **waits in place** (it does not fetch the next
instruction). `pcpi_valid` remains high continuously until your wrapper
asserts `pcpi_ready`, then the CPU deasserts it and moves on.

Consequences for the FSM:

- Because the CPU is stalled, `pcpi_insn/rs1/rs2` are stable for the whole
  operation. You may keep driving the FPU pipeline with the same operands for
  many cycles; the pipeline output converges and stays correct.
- You must **not** start a new operation merely because `pcpi_valid` is high —
  it is high for the entire duration of one instruction. Detect the *rising
  edge* of `pcpi_valid` (or of a registered `pcpi_wait`, see §4.1) to start
  each instruction exactly once.

### 2.2 `pcpi_wait` and `pcpi_ready`

- Assert `pcpi_wait` on the cycle you accept the instruction and keep it high
  until `pcpi_ready`.
- Assert `pcpi_ready` + `pcpi_wr` together for **one cycle** when done; drive
  `pcpi_rd = {16'b0, ans}` at that time.
- After you assert `pcpi_ready`, `pcpi_valid` stays high for 1 more cycle (the
  CPU deasserts it on the rising edge after it samples `pcpi_ready`). The FSM
  must not re-trigger a new operation during this window — it should only
  re-arm after `pcpi_valid` (and hence `instr_fpu`) actually falls.

### 2.3 Timeout / illegal-instruction trap

PicoRV32 runs a 4-bit timeout counter (`pcpi_timeout_counter`) that
decrements while `pcpi_valid && !pcpi_wait`. If it reaches zero (~16 cycles),
the CPU traps the instruction as illegal (in this SoC, the `trap` output
asserts and the core halts).

**Therefore: assert `pcpi_wait` in the same cycle you accept the instruction**
(or within 1–2 cycles) — otherwise the core traps. Decoding combinationally
and registering `pcpi_wait` is sufficient.

### 2.4 Pass-through ("not mine")

A PCPI core that does **not** recognize the instruction asserts `pcpi_ready`
with `pcpi_wr = 0` and `pcpi_wait = 0`, passing it to the next core in the
chain (there is none here). Your wrapper should only ever do this if the
decode does not match an FPU instruction. **Do not** leave both `pcpi_wait`
and `pcpi_ready` low indefinitely, or the CPU times out.

---

## 3. Instruction decode

The wrapper recognizes RISC-V `custom0` instructions in the FPU namespace:

```
| funct7    | rs2   | rs1   | funct3 | rd   | opcode  |
| 31:25     | 24:20 | 19:15 | 14:12  | 11:7 | 6:0     |
| 0000110..9|  reg  |  reg  |  000   | reg  | 0001011 |
```

- `opcode` must be `7'b0001011` (`custom0`).
- `funct3` is `3'b000` for all four FPU ops (the firmware only emits this).
  Check it in the decode so an unexpected `custom0` with the same `funct7`
  but a different `funct3` is not wrongly claimed.
- `funct7` selects the operation and maps to the FPU `op` bus:

| funct7 (31:25) | Mnemonic | FPU op |
|----------------|----------|--------|
| `0000110` | FADD | `2'b00` |
| `0000111` | FSUB | `2'b01` |
| `0001000` | FMUL | `2'b10` |
| `0001001` | FDIV | `2'b11` |

Notes:

- Do **not** match funct7 `0000000`–`0000101`: those are PicoRV32's built-in
  IRQ custom instructions (`getq`, `setq`, `retirq`, `maskirq`, `waitirq`,
  `timer`). With IRQs disabled here they would otherwise be routed to PCPI and
  trap.
- With `ENABLE_IRQ = 0` in this SoC, any instruction that falls through the
  base ISA (and is not your funct7 range) reaches PCPI and will trap after the
  timeout. That is expected for a mis-compiled program.
- `rs1`/`rs2` are ordinary register numbers — the CPU already resolved them
  and supplies the values on `pcpi_rs1`/`pcpi_rs2`. Your wrapper does not
  decode them.

Suggested combinational decode:

```systemverilog
wire is_fpu_f3 = (pcpi_insn[14:12] == 3'b000);
wire is_custom0 = (pcpi_insn[6:0] == 7'b0001011);
wire instr_fadd = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000110);
wire instr_fsub = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000111);
wire instr_fmul = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001000);
wire instr_fdiv = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001001);
wire instr_any  = instr_fadd | instr_fsub | instr_fmul | instr_fdiv;

wire [1:0] fpu_op = instr_fdiv ? 2'b11 : instr_fmul ? 2'b10
                  : instr_fsub ? 2'b01 : 2'b00;
```

---

## 4. FPU pipeline handling

### 4.1 Timing budget

The wrapper retires one op per instruction. Because `a`/`b`/`op` are held
constant for the whole instruction, the result timing is:

- **FADD / FSUB / FMUL**: single-cycle registered datapaths. Drive `ans`
  through to `pcpi_rd` on the same cycle the datapath updates (the accepted
  instruction edge + 1 cycle), then pulse `pcpi_ready`.
- **FDIV**: start-gated sequential SRT core with a fixed 11-cycle schedule.
  A one-cycle `start` pulse latches the operands on the accept edge; the core
  reloads, runs 8 radix-4 iterations, emits the quotient, and pulses `done`
  11 cycles later. The wrapper waits for this single `done` (ready fires the
  cycle *after* `done`, when the quotient is stable on `pcpi_rd`). The latency
  is **fixed: exactly 12 cycles** from accept to ready, independent of any
  core phase (the SRT idles between divisions).

### 4.2 Idle gaps are harmless

Between instructions (CPU fetching/executing base-ISA code) the wrapper does
not drive meaningful operands, so the FPU pipeline holds garbage. This is fine
because every accepted instruction always runs a fresh fill before its result
is sampled: FADD/FSUB/FMUL re-present their operands for 1 cycle, and FDIV
re-latches its operands on the `start` pulse and re-presents the quotient on
its `done`. Never sample `ans` in the same cycle as the start edge, and never
for FDIV outside the cycle after `done`.

---

## 5. Suggested state machine (reference, not required)

Three states are sufficient:

```
                resetn
                  │
                  ▼
              ┌───────┐   accepted-instruction edge   ┌──────────┐
   ┌─────────►│ IDLE  │──────────────────────────────►│ COMPUTE  │
   │          └───────┘                                └──────────┘
   │              ▲                                          │
   │  instr_fpu   │                                          │ counter reaches
   │  falls (busy │                                          │ terminal count:
   │  clears)     │                                          │ latch ans
   │              │        ┌────────┐                        │
   │              └────────│  DONE  │◄───────────────────────┘
   │                       └────────┘
   │                        │ pcpi_wr=1, pcpi_ready=1 (one cycle),
   │                        │ pcpi_rd={16'b0, ans}
   └────────────────────────┘
```

| State | Outputs | Transition |
|-------|---------|-----------|
| IDLE | `pcpi_wait=0, pcpi_ready=0, pcpi_wr=0` | → COMPUTE on the accepted-instruction edge (rising edge of `pcpi_valid`/registered `pcpi_wait` with `instr_any`). |
| COMPUTE | `pcpi_wait=1` (stall + suppress timeout), operands continuously driven into `fpu_test` | → DONE when the datapath result is valid: cycle 1 for FADD/FSUB/FMUL (`cyc==0`), or the cycle after the `done` pulse for FDIV (fixed 12 cycles after the accept edge). |
| DONE | `pcpi_wait=1, pcpi_ready=1, pcpi_wr=1, pcpi_rd={16'b0, ans}` for one cycle | → IDLE when `instr_any` falls (instruction retired). |

Requirements recap (what your FSM must guarantee):

1. Start each instruction on a fresh edge, never on the level.
2. `pcpi_wait = 1` from acceptance until `pcpi_ready`.
3. `pcpi_ready` + `pcpi_wr` high for exactly one cycle; `pcpi_rd` valid then.
4. Do not re-trigger while `pcpi_valid` is still high after `pcpi_ready`;
   re-arm only once the instruction is gone.
5. Drive `a/b/op` continuously (they are stable inputs); sample `ans` at the
   op-specific commit point (cycle 1 for FADD/FSUB/FMUL, the cycle after the
   `done` pulse for FDIV). For FDIV, assert the one-cycle `start` pulse on the
   accept edge so the operands are latched deterministically — never sample
   `ans` mid-computation.
6. On `resetn`, return to IDLE with all outputs deasserted.

---

## 6. Verification notes

- Baseline (no wrapper): `./run_cpu_test.sh` — CPU runs pure RV32I, harness
  reports **PASS**, no trap.
- With wrapper: `./run_cpu_test.sh fpu` — the firmware
  (`tb/firmware/fpu_test_main.c`) runs 15 FPU ops (FADD/FSUB/FMUL/FDIV across
  normal, NaN, inf, zero operands), stores the results, and the harness
  (`tb/tb_fpu_pcpi.cpp`) compares them against an IEEE-754 `_Float16` golden
  model (NaN-tolerant comparison).
- If the wrapper is absent, the FPU firmware traps (timeout → illegal
  instruction) and the harness reports FAIL with a hint to build with
  `HAS_FPU_PCPI`.
