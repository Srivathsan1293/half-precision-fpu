#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sys/stat.h>
#include <verilated.h>
#include "Vfpu_test.h" // Verilated wrapper module

enum Category {
    CAT_NAN = 0,
    CAT_INF = 1,
    CAT_ZERO = 2,
    CAT_SUBNORM = 3,
    CAT_NORMAL = 4,
    CAT_COUNT = 5
};

const char* cat_filenames[] = {
    "testing_results/fdiv_fail_nan.txt",
    "testing_results/fdiv_fail_inf.txt",
    "testing_results/fdiv_fail_zero.txt",
    "testing_results/fdiv_fail_subnormal.txt",
    "testing_results/fdiv_fail_normal.txt"
};

const char* cat_names[] = {
    "Involves NaN              ",
    "Involves Infinity         ",
    "Involves True Zero        ",
    "Involves Subnormal(s)     ",
    "Normal Arithmetic (Normal)"
};

// Golden Model: Matches exact fpu_FDIV.sv overrides
uint16_t compute_golden_cpp_native(uint16_t a, uint16_t b) {
    uint16_t expA = (a >> 10) & 0x1F;
    uint16_t expB = (b >> 10) & 0x1F;
    uint16_t manA = a & 0x03FF;
    uint16_t manB = b & 0x03FF;

    bool a_nan  = (expA == 31 && manA != 0);
    bool b_nan  = (expB == 31 && manB != 0);
    bool a_zero = (expA == 0 && manA == 0);
    bool b_zero = (expB == 0 && manB == 0);
    bool a_inf  = (expA == 31 && manA == 0);
    bool b_inf  = (expB == 31 && manB == 0);

    uint16_t signA = (a >> 15) & 1;
    uint16_t signB = (b >> 15) & 1;
    uint16_t final_sign = signA ^ signB;

    // RTL hardware overrides
    if (a_nan || b_nan || (a_zero && b_zero) || (a_inf && b_inf)) {
        return (final_sign << 15) | 0x7E00; // 15'b111111000000000
    }
    if (a_inf || b_zero) {
        return (final_sign << 15) | 0x7C00; // 15'b111110000000000
    }
    if (a_zero || b_inf) {
        return (final_sign << 15) | 0x0000; // 15'd0
    }

    // Standard native IEEE FP16 Division
    _Float16 float_A, float_B, float_ans;
    std::memcpy(&float_A, &a, sizeof(uint16_t));
    std::memcpy(&float_B, &b, sizeof(uint16_t));

    float_ans = float_A / float_B;

    uint16_t golden_bits;
    std::memcpy(&golden_bits, &float_ans, sizeof(uint16_t));
    return golden_bits;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;

    // Select DIV operation on the combined FPU top-level (op=3)
    dut->op = 3;

    // Create results directory
    #if defined(_WIN32)
    _mkdir("testing_results");
    #else
    mkdir("testing_results", 0777);
    #endif

    std::ofstream log_files[CAT_COUNT];
    for (int i = 0; i < CAT_COUNT; ++i) {
        log_files[i].open(cat_filenames[i]);
        if (!log_files[i].is_open()) {
            std::cerr << "Error: Could not open output file " << cat_filenames[i] << std::endl;
            delete dut;
            return 1;
        }
        log_files[i] << "Category: " << cat_names[i] << "\n";
        log_files[i] << "Format: A | B | Expected | Got | Delta (Expected - Got)\n";
        log_files[i] << "-------------------------------------------------------------\n";
    }

    uint64_t total_combinations = 4294967296ULL;
    uint64_t total_tests = 0;
    uint64_t total_failed = 0;

    uint64_t cat_totals[CAT_COUNT] = {0};
    uint64_t cat_fails[CAT_COUNT]  = {0};

    uint64_t update_interval = total_combinations / 100;
    int bar_width = 50;

    std::cout << "--- Starting Start-Gated SRT FDIV Hardware Test ---" << std::endl;
    std::cout << "FDIV core: fixed 12-cycle SRT (start pulse -> done 11 cycles -> answer 12th cycle)\n";
    std::cout << "Testing 4,294,967,296 combinations...\n\n";

    // FDIV start-gated model: present each input with a one-cycle `start`
    // pulse, then wait for the SRT `done` pulse (11 cycles later). The DIV
    // captures the completed quotient at the done edge, so the answer is
    // sampled on the cycle after `done`.
    uint64_t n_presented = 0;
    bool wait_done = false;

    while (total_tests < total_combinations) {
        // Present the next input with a one-cycle start pulse
        dut->a = static_cast<uint16_t>((n_presented >> 16) & 0xFFFF);
        dut->b = static_cast<uint16_t>(n_presented & 0xFFFF);
        n_presented++;
        wait_done = true;

        dut->clk = 0;
        dut->eval();
        dut->start = 1;
        dut->clk = 1;
        dut->eval();
        dut->start = 0;

        // Wait for the SRT done pulse; the quotient is captured at the done
        // edge (posedge below), so `ans` is sampled on the following cycle.
        while (wait_done) {
            dut->clk = 0;
            dut->eval();
            wait_done = !dut->done;
            dut->clk = 1;
            dut->eval();
        }
        dut->clk = 0;
        dut->eval();

        uint16_t check_a = static_cast<uint16_t>((total_tests >> 16) & 0xFFFF);
        uint16_t check_b = static_cast<uint16_t>(total_tests & 0xFFFF);

        uint16_t expA = (check_a >> 10) & 0x1F;
        uint16_t manA = check_a & 0x03FF;
        uint16_t expB = (check_b >> 10) & 0x1F;
        uint16_t manB = check_b & 0x03FF;

        bool a_zero = (expA == 0 && manA == 0);
        bool b_zero = (expB == 0 && manB == 0);
        bool a_sub  = (expA == 0 && manA != 0);
        bool b_sub  = (expB == 0 && manB != 0);
        bool a_inf  = (expA == 31 && manA == 0);
        bool b_inf  = (expB == 31 && manB == 0);
        bool a_nan  = (expA == 31 && manA != 0);
        bool b_nan  = (expB == 31 && manB != 0);

        Category current_cat;
        if (a_nan || b_nan)         current_cat = CAT_NAN;
        else if (a_inf || b_inf)    current_cat = CAT_INF;
        else if (a_zero || b_zero)  current_cat = CAT_ZERO;
        else if (a_sub || b_sub)    current_cat = CAT_SUBNORM;
        else                        current_cat = CAT_NORMAL;

        cat_totals[current_cat]++;

        uint16_t expected_ans = compute_golden_cpp_native(check_a, check_b);
        uint16_t hw_ans = dut->ans; // quotient captured at the done edge

        if (hw_ans != expected_ans) {
            total_failed++;
            cat_fails[current_cat]++;

            int32_t delta = static_cast<int32_t>(expected_ans) - static_cast<int32_t>(hw_ans);

            log_files[current_cat]
            << "a=0x" << std::hex << std::setfill('0') << std::setw(4) << check_a
            << " | b=0x" << std::setw(4) << check_b
            << " | Expected=0x" << std::setw(4) << expected_ans
            << " | Got=0x" << std::setw(4) << hw_ans
            << " | Delta=" << std::dec << delta << "\n";
        }

        total_tests++;

        // Progress Indicator
        if (total_tests % update_interval == 0 || total_tests == total_combinations) {
            float progress = static_cast<float>(total_tests) / total_combinations;
            int pos = static_cast<int>(bar_width * progress);

            std::cout << "\r[";
            for (int bar = 0; bar < bar_width; ++bar) {
                if (bar < pos) std::cout << "=";
                else if (bar == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << static_cast<int>(progress * 100.0) << " %" << std::flush;
        }
    }

    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "           FINAL FDIV HARDWARE SUMMARY       " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    for (int i = 0; i < CAT_COUNT; i++) {
        log_files[i].close();
        double fail_pct = (cat_totals[i] > 0) ? ((double)cat_fails[i] / cat_totals[i]) * 100.0 : 0.0;
        std::cout << cat_names[i] << " : "
        << cat_fails[i] << " failed / " << cat_totals[i] << " total ("
        << std::fixed << std::setprecision(2) << fail_pct << "%)\n";
    }

    delete dut;
    return (total_failed == 0) ? 0 : 1;
}
