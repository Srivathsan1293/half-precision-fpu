#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>
#include <utility>
#include <verilated.h>
#include "Vfpu_test.h"

static uint16_t compute_golden(uint16_t a, uint16_t b) {
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
    uint16_t fs = ((a >> 15) ^ (b >> 15)) & 1;
    if (a_nan || b_nan || (a_zero && b_zero) || (a_inf && b_inf)) return (fs << 15) | 0x7E00;
    if (a_inf || b_zero) return (fs << 15) | 0x7C00;
    if (a_zero || b_inf) return (fs << 15) | 0x0000;
    _Float16 fa, fb, fans;
    std::memcpy(&fa, &a, sizeof(uint16_t));
    std::memcpy(&fb, &b, sizeof(uint16_t));
    fans = fa / fb;
    uint16_t bits;
    std::memcpy(&bits, &fans, sizeof(uint16_t));
    return bits;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfpu_test* dut = new Vfpu_test;
    dut->op = 3;
    mkdir("testing_results", 0777);

    uint64_t n_tests = 0, n_fail = 0;
    uint64_t cat_fail[5] = {0}, cat_tot[5] = {0};

    std::ofstream log("testing_results/fdiv_quick_failures.log");

    auto check = [&](uint16_t a, uint16_t b, uint16_t got) {
        uint16_t exp = compute_golden(a, b);
        uint16_t eA=(a>>10)&0x1F, eB=(b>>10)&0x1F, mA=a&0x3FF, mB=b&0x3FF;
        bool a_nan=(eA==31&&mA), b_nan=(eB==31&&mB), a_inf=(eA==31&&!mA), b_inf=(eB==31&&!mB);
        bool a_zero=(eA==0&&!mA), b_zero=(eB==0&&!mB), a_sub=(eA==0&&mA), b_sub=(eB==0&&mB);
        int cat;
        if (a_nan||b_nan) cat=0; else if (a_inf||b_inf) cat=1;
        else if (a_zero||b_zero) cat=2; else if (a_sub||b_sub) cat=3; else cat=4;
        cat_tot[cat]++;
        if (exp != got) {
            n_fail++;
            cat_fail[cat]++;
            if (cat_fail[cat] <= 20)
                log << "cat" << cat << " a=0x" << std::hex << std::setw(4) << std::setfill('0') << a
                    << " b=0x" << std::setw(4) << b << " exp=0x" << std::setw(4) << exp
                    << " got=0x" << std::setw(4) << got << std::dec << "\n";
        }
    };

    // Build the test vector: boundary/edge cross-product plus random samples.
    uint16_t edges[] = {0x0000,0x0001,0x03FF,0x0400,0x7BFF,0x7C00,0x7E00,0xFC00,
                        0x3800,0x3C00,0x4000,0x4200,0x57FF,0x5800,0x5BFF,0x5C00};
    std::vector<std::pair<uint16_t,uint16_t>> tests;
    tests.reserve(256 + 2000000);
    for (uint16_t a : edges) for (uint16_t b : edges) tests.emplace_back(a, b);

    srand(12345);
    const uint64_t SAMPLES = 2000000;
    for (uint64_t i = 0; i < SAMPLES; i++)
        tests.emplace_back(static_cast<uint16_t>(rand() & 0xFFFF),
                           static_cast<uint16_t>(rand() & 0xFFFF));

    // FDIV start-gated model (mirrors tb/tb_fdiv_seq.cpp): present each input
    // with a one-cycle `start` pulse, then wait for the SRT `done` pulse (11
    // cycles later); the DIV captures the quotient at the done edge, so the
    // answer is sampled on the following cycle. One test completes every 12
    // cycles.
    size_t n_presented = 0;
    bool wait_done = false;

    while (n_tests < tests.size()) {
        dut->a = tests[n_presented].first;
        dut->b = tests[n_presented].second;
        n_presented++;
        wait_done = true;

        dut->clk = 0;
        dut->eval();
        dut->start = 1;
        dut->clk = 1;
        dut->eval();
        dut->start = 0;

        while (wait_done) {
            dut->clk = 0;
            dut->eval();
            wait_done = !dut->done;
            dut->clk = 1;
            dut->eval();
        }
        dut->clk = 0;
        dut->eval();

        check(tests[n_presented - 1].first, tests[n_presented - 1].second, dut->ans);
        n_tests++;
    }

    std::cout << "tests=" << n_tests << " fails=" << n_fail << "\n";
    const char* cn[] = {"NaN","Inf","Zero","Subnormal","Normal"};
    for (int i = 0; i < 5; i++)
        std::cout << "  " << cn[i] << ": " << cat_fail[i] << " fail / " << cat_tot[i] << " tot\n";
    delete dut;
    return n_fail == 0 ? 0 : 1;
}
