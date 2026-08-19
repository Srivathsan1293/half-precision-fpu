#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <sys/stat.h>
#include <verilated.h>
#include "Vsrt.h"

uint16_t compute_golden_srt(uint16_t manA, uint16_t manB) {
    // Hardware calculates ((manA / 2) / manB) * 2^14 = (manA * 8192) / manB
    uint32_t expected = (static_cast<uint32_t>(manA) * 8192) / manB;
    return static_cast<uint16_t>(expected & 0x3FFF);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vsrt* dut = new Vsrt;

    #if defined(_WIN32)
    _mkdir("testing_results");
    #else
    mkdir("testing_results", 0777);
    #endif

    std::ofstream fail_log("testing_results/srt_failures.log");
    if (!fail_log.is_open()) {
        std::cerr << "Error: Could not open output log file." << std::endl;
        delete dut;
        return 1;
    }

    fail_log << "manA (Hex) | manB (Hex) | Expected (Dec) | Got (Dec) | Delta (Exp - Got)\n";
    fail_log << "----------------------------------------------------------------------\n";

    uint64_t total_combinations = 1024 * 1024;
    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    int bar_width = 50;
    uint64_t update_interval = total_combinations / 100;

    std::cout << "--- Starting Isolated Radix-4 SRT Core Test ---\n";
    std::cout << "Writing failures to testing_results/srt_failures.log...\n\n";

    for (uint32_t a = 1024; a < 2048; a++) {
        for (uint32_t b = 1024; b < 2048; b++) {

            if (total_tests % update_interval == 0 || total_tests == total_combinations) {
                float progress = static_cast<float>(total_tests) / total_combinations;
                int pos = static_cast<int>(bar_width * progress);

                std::cout << "\r[";
                for (int k = 0; k < bar_width; ++k) {
                    if (k < pos) std::cout << "=";
                    else if (k == pos) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << static_cast<int>(progress * 100.0) << " %" << std::flush;
            }

            dut->manA = a;
            dut->manB = b;

            // The SRT core is start-gated: present the mantissas with a
            // one-cycle `start` pulse, then wait for `done` (11 cycles later).
            // `done` pulses one cycle after Quotient is refreshed, so sample
            // Quotient on the cycle after the done edge.
            dut->clk = 0;
            dut->eval();
            dut->start = 1;
            dut->clk = 1;
            dut->eval();
            dut->start = 0;

            bool got = false;
            for (int cycle = 0; cycle < 20 && !got; cycle++) {
                dut->clk = 0;
                dut->eval();
                got = dut->done;
                dut->clk = 1;
                dut->eval();
            }
            dut->clk = 0;
            dut->eval();

            uint16_t expected_ans = compute_golden_srt(a, b);
            uint16_t hw_ans = dut->Quotient;

            if (hw_ans != expected_ans) {
                total_failed++;
                int32_t delta = static_cast<int32_t>(expected_ans) - static_cast<int32_t>(hw_ans);

                fail_log << "0x" << std::hex << std::setfill('0') << std::setw(4) << a
                << "     | 0x" << std::setw(4) << b
                << "     | " << std::dec << std::setfill(' ') << std::setw(14) << expected_ans
                << " | " << std::setw(9) << hw_ans
                << " | " << delta << "\n";
            }
            total_tests++;
        }
    }

    fail_log.close();

    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "           FINAL SRT HARDWARE SUMMARY        " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    std::cout << "Total Tests Evaluated : " << total_tests << "\n";
    std::cout << "Total Failures        : " << total_failed << "\n";

    if (total_failed == 0) {
        std::cout << "\nSTATUS: SUCCESS! The SRT core is mathematically perfect.\n";
    } else {
        std::cout << "\nSTATUS: FAILED. Check testing_results/srt_failures.log for details.\n";
    }

    delete dut;
    return (total_failed == 0) ? 0 : 1;
}
