// tb/tb_pcpi_fsm.cpp
//
// Wrapper-only state-machine timing verification for src/fpu_pcpi.sv.
//
// Drives the PCPI bus exactly like PicoRV32 (no CPU, no firmware) and asserts
// the precise busy->ready timing of the option-1 handshake (busy flag +
// combinational pcpi_wait/pcpi_wr/pcpi_ready):
//
//   FADD/FSUB/FMUL (1 datapath stage):  answer valid 1 cycle after accept,
//       ready asserted combinationally that cycle
//       -> pcpi_ready first observed high on loop cycle 0 after accept
//   FDIV (start-gated sequential SRT core): the DIV latches the operands on
//       the accept edge and the SRT emits `done` 11 cycles later; the quotient
//       is stable the cycle after, so ready lands on a fixed 12 cycles after
//       accept regardless of the SRT phase (the core idles between divisions).
//       -> pcpi_ready first observed high on loop cycle 11
//   No early fire:                      pcpi_ready must stay low for every
//          cycle before the expected ready cycle
//   pcpi_ready + pcpi_wr pulse for exactly one cycle, then the wrapper returns
//   to idle (no re-trigger while pcpi_valid is held high)
//
// This complements tb/tb_pcpi_handshake.cpp (protocol rules) by pinning the
// exact cycle counts the state machine must hit.

#include <cstdint>
#include <cstdio>
#include <verilated.h>

#include "Vfpu_pcpi.h"

static int checks = 0, fails = 0;

static void check(const char* name, bool ok) {
    checks++;
    if (!ok) fails++;
    std::printf("  %-62s %s\n", name, ok ? "PASS" : "FAIL");
}

struct Obs {
    bool wait, ready, wr;
    uint32_t rd;
};

// One clock cycle: drive inputs, sample the wrapper's registered outputs,
// then clock a rising edge. Mirrors tb_pcpi_handshake.cpp.
static Obs cycle(Vfpu_pcpi* dut, bool valid, bool resetn, uint32_t insn,
                 uint32_t rs1, uint32_t rs2) {
    dut->pcpi_valid = valid;
    dut->resetn = resetn;
    dut->pcpi_insn = insn;
    dut->pcpi_rs1 = rs1;
    dut->pcpi_rs2 = rs2;
    dut->eval();
    Obs o;
    o.wait = dut->pcpi_wait;
    o.ready = dut->pcpi_ready;
    o.wr = dut->pcpi_wr;
    o.rd = dut->pcpi_rd;
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    dut->eval();
    return o;
}

// Run one op and collect the timing window: cycle (after the accept cycle,
// 0-based) where pcpi_ready first goes high, how many ready pulses, and
// whether ready/wr ever fired before the expected cycle. `no_fire_until` is
// the first cycle ready may legally appear (FDIV has a fixed 12-cycle
// latency, so early-fire is checked against cycle 11); `ready_max` is the
// upper bound on the ready cycle; `window` is the number of cycles to probe.
struct Timing {
    int ready_first = -1;
    int wait_first = -1;
    int ready_count = 0;
    bool early_fire = false;
    bool wait_gap = false;
    bool wr_ok = false;
    bool rd_ok = false;
    bool retrigger = false;
};

static Timing run_timing(Vfpu_pcpi* dut, uint32_t insn, uint32_t rs1,
                         uint32_t rs2, uint32_t exp, int no_fire_until,
                         int ready_max, int window) {
    Timing t;
    bool wait_hist[64] = {false};
    cycle(dut, 1, 1, insn, rs1, rs2);                 // accept cycle
    int window_end = -1;
    for (int cyc = 0; cyc < window; cyc++) {
        Obs o = cycle(dut, 1, 1, insn, rs1, rs2);
        wait_hist[cyc] = o.wait;
        if (o.wait && t.wait_first < 0) t.wait_first = cyc;
        if (t.ready_first >= 0 && cyc > window_end) {
            // pcpi_wait legitimately stays high while pcpi_valid is held
            // (pcpi_wait = busy | start_compute); only a second ready/wr pulse
            // counts as an illegal re-trigger.
            if (o.ready || o.wr) t.retrigger = true;
        }
        if (o.ready) {
            if (t.ready_first < 0) {
                t.ready_first = cyc;
                if (cyc < no_fire_until) t.early_fire = true;
                t.wr_ok = o.wr;
                t.rd_ok = (o.rd == exp);
            }
            t.ready_count++;
            window_end = cyc;
        }
    }
    if (t.ready_first > ready_max) t.ready_first = -2; // out of window / too late
    // wait must stay high continuously from wait_first until ready (it stays
    // high while pcpi_valid is held; the CPU drops pcpi_valid after ready).
    if (t.wait_first >= 0 && t.ready_first >= 0) {
        for (int cyc = t.wait_first + 1; cyc < t.ready_first; cyc++) {
            if (!wait_hist[cyc]) t.wait_gap = true;
        }
    }
    cycle(dut, 0, 1, 0, 0, 0);                        // drop valid -> idle
    return t;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_pcpi* dut = new Vfpu_pcpi;

    dut->clk = 0;
    dut->resetn = 0;
    dut->pcpi_valid = 0;
    dut->pcpi_insn = 0;
    dut->pcpi_rs1 = 0;
    dut->pcpi_rs2 = 0;
    dut->eval();

    const uint32_t rs1_1 = 0x00003C00;   // 1.0
    const uint32_t rs1_2 = 0x00004000;   // 2.0
    const uint32_t rs1_3 = 0x00004200;   // 3.0

    std::printf("PCPI FSM exact-timing test (fpu_pcpi wrapper only)\n");
    std::printf("FADD/FSUB/FMUL: ready on loop cycle 0; FDIV: fixed loop cycle 11\n");

    // ---- reset / idle ----
    bool idle_ok = true;
    for (int i = 0; i < 4; i++) {
        Obs o = cycle(dut, 0, 0, 0, 0, 0);
        if (o.wait || o.ready || o.wr) idle_ok = false;
    }
    for (int i = 0; i < 4; i++) {
        Obs o = cycle(dut, 0, 1, 0, 0, 0);
        if (o.wait || o.ready || o.wr) idle_ok = false;
    }
    check("reset + idle: wait/ready/wr deasserted", idle_ok);

    // ---- FADD: expected ready_first = 0 ----
    std::printf("Test 1: FADD.H (1.0 + 2.0 = 0x4200), expected ready cycle 0\n");
    {
        Timing t = run_timing(dut, 0x0CB5008B, rs1_1, rs1_2, 0x4200, 0, 0, 16);
        check("FADD: wait asserted within 2 cycles", t.wait_first >= 0 && t.wait_first <= 2);
        check("FADD: no wait gap before ready", !t.wait_gap);
        check("FADD: ready_first == 0", t.ready_first == 0);
        check("FADD: ready pulses exactly once", t.ready_count == 1);
        check("FADD: pcpi_wr high during ready", t.wr_ok);
        check("FADD: pcpi_rd == 0x4200", t.rd_ok);
        check("FADD: no re-trigger after ready while valid held", !t.retrigger);
    }

    // ---- FSUB: expected ready_first = 0 ----
    std::printf("Test 2: FSUB.H (2.0 - 1.0 = 0x3C00), expected ready cycle 0\n");
    {
        Timing t = run_timing(dut, 0x0FB5008B, rs1_2, rs1_1, 0x3C00, 0, 0, 16);
        check("FSUB: ready_first == 0", t.ready_first == 0);
        check("FSUB: ready pulses exactly once", t.ready_count == 1);
        check("FSUB: pcpi_rd == 0x3C00", t.rd_ok);
    }

    // ---- FMUL: expected ready_first = 0 ----
    std::printf("Test 3: FMUL.H (2.0 * 3.0 = 0x4600), expected ready cycle 0\n");
    {
        Timing t = run_timing(dut, 0x10B5008B, rs1_2, rs1_3, 0x4600, 0, 0, 16);
        check("FMUL: ready_first == 0", t.ready_first == 0);
        check("FMUL: ready pulses exactly once", t.ready_count == 1);
        check("FMUL: pcpi_rd == 0x4600", t.rd_ok);
    }

    // ---- FDIV: fixed 12-cycle start-gated SRT. The `start` pulse latches
    // the operands on the accept edge; the SRT emits `done` 11 cycles later
    // and the quotient is stable the cycle after, so ready lands exactly on
    // loop cycle 11. No early fire before cycle 11 (the first 11 cycles are
    // the SRT's fixed reload+8-iteration+emit schedule). ----
    std::printf("Test 4: FDIV.H (3.0 / 2.0 = 0x3E00), fixed 12-cycle ready\n");
    {
        Timing t = run_timing(dut, 0x12B5008B, rs1_3, rs1_2, 0x3E00, 11, 11, 24);
        check("FDIV: wait asserted within 2 cycles", t.wait_first >= 0 && t.wait_first <= 2);
        check("FDIV: no wait gap before ready", !t.wait_gap);
        check("FDIV: NO EARLY FIRE (ready stays low through cycle 10)", !t.early_fire);
        check("FDIV: ready_first == 11 (fixed 12-cycle latency)", t.ready_first == 11);
        check("FDIV: ready pulses exactly once", t.ready_count == 1);
        check("FDIV: pcpi_wr high during ready", t.wr_ok);
        check("FDIV: pcpi_rd == 0x3E00", t.rd_ok);
        check("FDIV: no re-trigger after ready while valid held", !t.retrigger);
    }

    // ---- standard-Zhinx encodings hit the same handshake ----
    std::printf("Test 5: standard fadd.h (funct3=000) via same handshake timing\n");
    {
        Timing t = run_timing(dut, 0x04B500D3, rs1_1, rs1_2, 0x4200, 0, 0, 16);
        check("std fadd f3=000: ready_first == 0", t.ready_first == 0);
        check("std fadd f3=000: pcpi_rd == 0x4200", t.rd_ok);
        t = run_timing(dut, 0x04B570D3, rs1_1, rs1_2, 0x4200, 0, 0, 16);
        check("std fadd f3=111: ready_first == 0", t.ready_first == 0);
        check("std fadd f3=111: pcpi_rd == 0x4200", t.rd_ok);
    }

    // ---- unsupported encoding: wrapper must NOT enter compute ----
    std::printf("Test 6: unsupported fmin.h stays idle (no state-machine entry)\n");
    {
        Timing t = run_timing(dut, 0x0AB500D3, rs1_1, rs1_2, 0, 0, 0, 16);
        check("unsupported: wait never asserted", t.wait_first == -1);
        check("unsupported: ready never asserted", t.ready_first == -1);
        check("unsupported: no ready pulses", t.ready_count == 0);
    }

    std::printf("----\n");
    std::printf("SUMMARY: %d/%d checks passed, %d failed\n", checks - fails, checks, fails);
    if (fails == 0) {
        std::printf("STATUS: PASS\n");
    } else {
        std::printf("STATUS: FAIL\n");
    }
    delete dut;
    return fails == 0 ? 0 : 1;
}
