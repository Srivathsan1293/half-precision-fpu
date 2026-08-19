// tb/tb_pcpi_handshake.cpp
//
// Wrapper-only PCPI handshake test for src/fpu_pcpi.sv.
//
// Instantiates the fpu_pcpi module directly (no CPU, no SoC, no firmware)
// and drives the PCPI bus the way PicoRV32 does. It checks the handshake
// protocol rules from pcpi_wrapper_spec.md §2 and §5:
//
//   1. After reset: pcpi_wait/pcpi_ready/pcpi_wr low, pcpi_rd[31:16] = 0
//   2. Idle (pcpi_valid low): all outputs stay deasserted
//   3. Accept cycle (pcpi_valid first high): pcpi_ready must be LOW (no
//      immediate bogus commit; PicoRV32 samples pcpi_ready combinationally
//      in the same cycle it asserts pcpi_valid)
//   4. pcpi_wait asserts within <= 2 cycles of accept (timeout suppression)
//   5. pcpi_wait stays high continuously until pcpi_ready
//   6. pcpi_ready (+ pcpi_wr) pulses for exactly ONE cycle; pcpi_rd[31:16]=0
//   7. pcpi_rd[15:0] holds the correct FPU result: FADD(1.0, 2.0) = 3.0 =
//      0x4200 (option-1 handshake: ready asserted combinationally 1 cycle
//      after accept for FADD/FSUB/FMUL; FDIV is fixed 12-cycle, driven by the
//      start-gated sequential SRT `done` handshake)
//   8. No re-trigger: while pcpi_valid stays high after the ready pulse,
//      pcpi_ready/pcpi_wr must stay low (spec §2.2 req 4). pcpi_wait
//      legitimately stays high while pcpi_valid is held (pcpi_wait = busy |
//      start_compute); the wrapper re-arms only after pcpi_valid falls.
//   9. After pcpi_valid falls, the wrapper returns to idle (outputs low)
//  10. Reset asserted mid-operation deasserts all outputs within 1 cycle

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

// One clock cycle: drive inputs (as the CPU's registered outputs would),
// sample the wrapper's registered outputs, then clock a rising edge.
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

struct Handshake {
    int wait_first, ready_first, ready_count;
    bool wait_gap, wr_ok, rd_ok, retrigger, accept_ready_low;
};

// Run a full handshake for one op: assert pcpi_valid, collect the wait/ready
// window, then hold valid a few more cycles to probe for re-trigger. The first
// sampled cycle is the accept cycle itself.
static Handshake run_handshake(Vfpu_pcpi* dut, uint32_t insn,
                               uint32_t rs1, uint32_t rs2, uint32_t exp) {
    Handshake h = {-1, -1, 0, false, true, true, false, true};
    int window_end = -1;
    for (int cyc = 0; cyc < 16; cyc++) {
        Obs o = cycle(dut, 1, 1, insn, rs1, rs2);
        if (cyc == 0 && o.ready) h.accept_ready_low = false;
        if (o.wait && h.wait_first < 0) h.wait_first = cyc;
        if (h.ready_first >= 0 && cyc > window_end) {
            // pcpi_wait stays high while pcpi_valid is held; only a second
            // ready/wr pulse counts as an illegal re-trigger.
            if (o.ready || o.wr) h.retrigger = true;
        }
        if (o.ready) {
            if (h.ready_first < 0) h.ready_first = cyc;
            h.ready_count++;
            window_end = cyc;
            if (!o.wr) h.wr_ok = false;
            if (o.rd != exp) h.rd_ok = false;
        } else if (h.ready_first >= 0) {
            break;
        }
        if (h.ready_first < 0 && h.wait_first >= 0 && !o.wait) h.wait_gap = true;
    }
    for (int i = 0; i < 4; i++) {                     // re-trigger probe
        Obs o = cycle(dut, 1, 1, insn, rs1, rs2);
        if (o.ready || o.wr) h.retrigger = true;
    }
    cycle(dut, 0, 1, 0, 0, 0);                        // drop valid -> back to idle
    return h;
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

    const uint32_t insn_fadd = 0x0CB5008B;  // custom0 FADD x1,x10,x11
    const uint32_t rs1 = 0x00003C00;        // 1.0
    const uint32_t rs2 = 0x00004000;        // 2.0
    const uint32_t exp  = 0x00004200;       // 3.0

    std::printf("PCPI handshake protocol test (fpu_pcpi wrapper only)\n");

    // ---- Test A: reset + idle ----
    std::printf("Test A: reset and idle\n");
    bool a_ok = true;
    for (int i = 0; i < 4; i++) {
        Obs o = cycle(dut, 0, 0, 0, 0, 0);       // during reset
        if (o.wait || o.ready || o.wr) a_ok = false;
    }
    check("during reset: wait/ready/wr deasserted", a_ok);
    for (int i = 0; i < 6; i++) {
        Obs o = cycle(dut, 0, 1, 0, 0, 0);       // idle after reset
        if (o.wait || o.ready || o.wr) a_ok = false;
        if ((o.rd >> 16) != 0) a_ok = false;
    }
    check("idle (valid=0): wait/ready/wr low, rd[31:16]=0", a_ok);

    // ---- Test B: single FADD handshake ----
    std::printf("Test B: FADD(1.0, 2.0) handshake\n");
    Handshake hb = run_handshake(dut, insn_fadd, rs1, rs2, exp);
    check("accept cycle: pcpi_ready is LOW (no immediate commit)", hb.accept_ready_low);
    check("wait asserts within 2 cycles of accept", hb.wait_first >= 0 && hb.wait_first <= 2);
    check("wait held continuously until ready", !hb.wait_gap);
    check("ready pulses for exactly one cycle", hb.ready_count == 1);
    check("pcpi_wr high during ready pulse", hb.wr_ok);
    check("pcpi_rd = {16'b0, 0x4200} at ready", hb.rd_ok);
    check("no re-trigger while valid stays high after ready", !hb.retrigger);

    // Drop valid, expect clean idle.
    bool idle_ok = false;
    for (int i = 0; i < 5; i++) {
        Obs o = cycle(dut, 0, 1, 0, 0, 0);
        if (!o.wait && !o.ready && !o.wr) { idle_ok = true; break; }
    }
    check("returns to idle (all outputs low) after valid falls", idle_ok);

    // ---- Test C: reset mid-operation ----
    std::printf("Test C: reset mid-operation\n");
    cycle(dut, 1, 1, insn_fadd, rs1, rs2);                  // accept
    Obs mid = cycle(dut, 1, 1, insn_fadd, rs1, rs2);        // computing
    check("mid-operation: wait asserted", mid.wait);
    cycle(dut, 1, 0, insn_fadd, rs1, rs2);                  // apply reset
    Obs after_rst = cycle(dut, 0, 0, 0, 0, 0);              // sample post-reset
    check("reset deasserts wait/ready/wr within 1 cycle",
          !after_rst.wait && !after_rst.ready && !after_rst.wr);
    for (int i = 0; i < 3; i++) cycle(dut, 0, 0, 0, 0, 0);
    bool post_ok = false;
    for (int i = 0; i < 4; i++) {
        Obs o = cycle(dut, 0, 1, 0, 0, 0);
        if (!o.wait && !o.ready && !o.wr) { post_ok = true; break; }
    }
    check("post-reset: wrapper returns to idle", post_ok);
    bool new_op_ok = false;
    for (int i = 0; i < 16; i++) {
        Obs o = cycle(dut, 1, 1, insn_fadd, rs1, rs2);
        if (o.ready && o.rd == exp) { new_op_ok = true; break; }
    }
    check("post-reset: fresh op completes with correct result", new_op_ok);

    // ---- Test D: unsupported FP encoding stays unclaimed ----
    // fmin.h (funct7=0x05, funct3=000, opcode 0x53) is a real standard-Zhinx
    // encoding that the wrapper intentionally does NOT claim: it is emulated at
    // 0x800 instead. The wrapper must sit idle (no wait/ready/wr) so the CPU's
    // pcpi_timeout fires and the trap-to-emulator path can take over.
    std::printf("Test D: unsupported FP encoding (fmin.h) stays idle\n");
    const uint32_t insn_fmin = 0x0AB500D3;   // fmin.h f1,f10,f11 (emulated, not HW)
    const uint32_t rs1_fmin = 0x00003C00;    // 1.0
    const uint32_t rs2_fmin = 0x00004000;    // 2.0
    bool unsup_ok = true;
    cycle(dut, 0, 1, 0, 0, 0);                          // ensure back to idle
    cycle(dut, 1, 1, insn_fmin, rs1_fmin, rs2_fmin);    // accept-looking cycle
    for (int i = 0; i < 12; i++) {
        Obs o = cycle(dut, 1, 1, insn_fmin, rs1_fmin, rs2_fmin);
        if (o.wait || o.ready || o.wr) unsup_ok = false;
    }
    check("unsupported encoding: no wait/ready/wr for 12 cycles", unsup_ok);
    bool unsup_idle = false;
    for (int i = 0; i < 5; i++) {
        Obs o = cycle(dut, 0, 1, 0, 0, 0);
        if (!o.wait && !o.ready && !o.wr) { unsup_idle = true; break; }
    }
    check("unsupported encoding: returns to idle after valid falls", unsup_idle);

    // ---- Test E: standard Zhinx FADD, funct3=000 (static RNE) ----
    std::printf("Test E: standard fadd.h funct3=000 handshake\n");
    const uint32_t insn_std_f3_000 = 0x04B500D3;   // fadd.h f1,f10,f11, rne
    Handshake he = run_handshake(dut, insn_std_f3_000, rs1, rs2, exp);
    check("std f3=000: claimed (wait+ready window present)", he.ready_count == 1);
    check("std f3=000: ready within 8 cycles of accept",
          he.ready_first >= 0 && he.ready_first <= 8);
    check("std f3=000: pcpi_wr high, rd = 0x4200", he.wr_ok && he.rd_ok);

    // ---- Test F: standard Zhinx FADD, funct3=111 (dynamic rounding, clang) ----
    std::printf("Test F: standard fadd.h funct3=111 handshake\n");
    const uint32_t insn_std_f3_111 = 0x04B570D3;   // fadd.h f1,f10,f11, dynamic
    Handshake hf = run_handshake(dut, insn_std_f3_111, rs1, rs2, exp);
    check("std f3=111: claimed (wait+ready window present)", hf.ready_count == 1);
    check("std f3=111: ready within 8 cycles of accept",
          hf.ready_first >= 0 && hf.ready_first <= 8);
    check("std f3=111: pcpi_wr high, rd = 0x4200", hf.wr_ok && hf.rd_ok);

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
