// tb/tb_pcpi_fsm.cpp
//
// Wrapper-only state-machine timing verification for src/fpu_pcpi.sv.
//
// Drives the PCPI bus exactly like PicoRV32 (no CPU, no firmware) and asserts
// the precise counter->ready timing of the 3-state FSM (idle/compute/done):
//
//   FADD/FSUB/FMUL (1 datapath stage):  counter reaches 1 on loop cycle 1,
//       ready asserted at that posedge, pcpi_ready follows one register later
//       -> pcpi_ready first observed high on loop cycle 3 after accept
//   FDIV (4-stage pipeline):            counter reaches 3 on loop cycle 3,
//       ready asserted at that posedge, pcpi_ready one register later
//       -> pcpi_ready first observed high on loop cycle 5 after accept
//   No early fire:                      pcpi_ready must stay low for every
//          cycle before the expected ready cycle
//   pcpi_ready + pcpi_wr pulse for exactly one cycle, then the FSM returns to
//   idle (no re-trigger while pcpi_valid is held high).
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
// whether ready/wr ever fired before the expected cycle.
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
                         uint32_t rs2, uint32_t exp, int expected_first) {
    Timing t;
    bool wait_hist[16] = {false};
    cycle(dut, 1, 1, insn, rs1, rs2);                 // accept cycle
    int window_end = -1;
    for (int cyc = 0; cyc < 16; cyc++) {
        Obs o = cycle(dut, 1, 1, insn, rs1, rs2);
        wait_hist[cyc] = o.wait;
        if (o.wait && t.wait_first < 0) t.wait_first = cyc;
        if (t.ready_first >= 0 && cyc > window_end) {
            if (o.wait || o.ready || o.wr) t.retrigger = true;
        }
        if (o.ready) {
            if (t.ready_first < 0) {
                t.ready_first = cyc;
                if (cyc < expected_first) t.early_fire = true;
                t.wr_ok = o.wr;
                t.rd_ok = (o.rd == exp);
            }
            t.ready_count++;
            window_end = cyc;
        }
    }
    // wait must stay high continuously from wait_first until the cycle before
    // ready (wait legitimately drops on the ready cycle itself).
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
    std::printf("FADD/FSUB/FMUL: ready on loop cycle 3, FDIV: ready on loop cycle 5\n");

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

    // ---- FADD: expected ready_first = 3 ----
    std::printf("Test 1: FADD.H (1.0 + 2.0 = 0x4200), expected ready cycle 3\n");
    {
        Timing t = run_timing(dut, 0x0CB5008B, rs1_1, rs1_2, 0x4200, 3);
        check("FADD: wait asserted within 2 cycles", t.wait_first >= 0 && t.wait_first <= 2);
        check("FADD: no wait gap before ready", !t.wait_gap);
        check("FADD: no early fire (ready before cycle 3)", !t.early_fire);
        check("FADD: ready_first == 3", t.ready_first == 3);
        check("FADD: ready pulses exactly once", t.ready_count == 1);
        check("FADD: pcpi_wr high during ready", t.wr_ok);
        check("FADD: pcpi_rd == 0x4200", t.rd_ok);
        check("FADD: no re-trigger after ready while valid held", !t.retrigger);
    }

    // ---- FSUB: expected ready_first = 3 ----
    std::printf("Test 2: FSUB.H (2.0 - 1.0 = 0x3C00), expected ready cycle 3\n");
    {
        Timing t = run_timing(dut, 0x0FB5008B, rs1_2, rs1_1, 0x3C00, 3);
        check("FSUB: no early fire (ready before cycle 3)", !t.early_fire);
        check("FSUB: ready_first == 3", t.ready_first == 3);
        check("FSUB: ready pulses exactly once", t.ready_count == 1);
        check("FSUB: pcpi_rd == 0x3C00", t.rd_ok);
    }

    // ---- FMUL: expected ready_first = 3 ----
    std::printf("Test 3: FMUL.H (2.0 * 3.0 = 0x4600), expected ready cycle 3\n");
    {
        Timing t = run_timing(dut, 0x10B5008B, rs1_2, rs1_3, 0x4600, 3);
        check("FMUL: no early fire (ready before cycle 3)", !t.early_fire);
        check("FMUL: ready_first == 3", t.ready_first == 3);
        check("FMUL: ready pulses exactly once", t.ready_count == 1);
        check("FMUL: pcpi_rd == 0x4600", t.rd_ok);
    }

    // ---- FDIV: expected ready_first = 5 (no early fire at cycles 0..4) ----
    std::printf("Test 4: FDIV.H (3.0 / 2.0 = 0x3E00), expected ready cycle 5\n");
    {
        Timing t = run_timing(dut, 0x12B5008B, rs1_3, rs1_2, 0x3E00, 5);
        check("FDIV: wait asserted within 2 cycles", t.wait_first >= 0 && t.wait_first <= 2);
        check("FDIV: no wait gap before ready", !t.wait_gap);
        check("FDIV: NO EARLY FIRE (ready stays low through cycle 4)", !t.early_fire);
        check("FDIV: ready_first == 5", t.ready_first == 5);
        check("FDIV: ready pulses exactly once", t.ready_count == 1);
        check("FDIV: pcpi_wr high during ready", t.wr_ok);
        check("FDIV: pcpi_rd == 0x3E00", t.rd_ok);
        check("FDIV: no re-trigger after ready while valid held", !t.retrigger);
    }

    // ---- standard-Zhinx encodings hit the same FSM ----
    std::printf("Test 5: standard fadd.h (funct3=000) via same FSM timing\n");
    {
        Timing t = run_timing(dut, 0x04B500D3, rs1_1, rs1_2, 0x4200, 3);
        check("std fadd f3=000: ready_first == 3", t.ready_first == 3);
        check("std fadd f3=000: pcpi_rd == 0x4200", t.rd_ok);
        t = run_timing(dut, 0x04B570D3, rs1_1, rs1_2, 0x4200, 3);
        check("std fadd f3=111: ready_first == 3", t.ready_first == 3);
        check("std fadd f3=111: pcpi_rd == 0x4200", t.rd_ok);
    }

    // ---- unsupported encoding: FSM must NOT enter compute ----
    std::printf("Test 6: unsupported fmin.h stays idle (no state-machine entry)\n");
    {
        Timing t = run_timing(dut, 0x0AB500D3, rs1_1, rs1_2, 0, 3);
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
