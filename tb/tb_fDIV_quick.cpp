#include <iostream>
#include <cstdint>
#include <cstring>
#include <random>
#include <verilated.h>
#include "Vfpu_test.h"

uint16_t golden(uint16_t a, uint16_t b) {
    uint16_t expA = (a >> 10) & 0x1F, expB = (b >> 10) & 0x1F;
    uint16_t manA = a & 0x03FF,      manB = b & 0x03FF;
    bool a_nan=(expA==31&&manA!=0), b_nan=(expB==31&&manB!=0);
    bool a_zero=(expA==0&&manA==0),  b_zero=(expB==0&&manB==0);
    bool a_inf=(expA==31&&manA==0),  b_inf=(expB==31&&manB==0);
    uint16_t fs = ((a>>15)^(b>>15))&1;
    if (a_nan||b_nan||(a_zero&&b_zero)||(a_inf&&b_inf)) return (fs<<15)|0x7E00;
    if (a_inf||b_zero) return (fs<<15)|0x7C00;
    if (a_zero||b_inf) return (fs<<15)|0x0000;
    _Float16 fa, fb, f_ans;
    std::memcpy(&fa, &a, 2); std::memcpy(&fb, &b, 2);
    f_ans = fa / fb;
    uint16_t r; std::memcpy(&r, &f_ans, 2);
    return r;
}

int main() {
    Verilated::commandArgs(1, (const char*[]){""});
    Vfpu_test* dut = new Vfpu_test;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);

    uint16_t histA[3] = {0,0,0}, histB[3] = {0,0,0};
    uint64_t tested = 0, failed = 0;
    auto feed = [&](uint16_t a, uint16_t b){
        dut->a = a; dut->b = b;
        dut->clk = 0; dut->eval();
        uint16_t exp = golden(histA[2], histB[2]);
        if (dut->ans != exp && tested >= 3) {
            if (failed < 20)
                std::cout << "FAIL a=0x" << std::hex << histA[2] << " b=0x" << histB[2]
                          << " exp=0x" << exp << " got=0x" << dut->ans << std::dec << "\n";
            failed++;
        }
        tested++;
        dut->clk = 1; dut->eval();
        histA[2]=histA[1]; histA[1]=histA[0]; histA[0]=a;
        histB[2]=histB[1]; histB[1]=histB[0]; histB[0]=b;
    };

    std::cout << "Testing 4.2M random pairs + all special combos...\n";
    for (uint64_t i = 0; i < 4000000ULL; i++) feed(dist(rng), dist(rng));

    uint16_t specials[] = {
        0x0000, 0x8000, 0x7C00, 0xFC00, 0x7E00, 0xFE00,
        0x0001, 0x8001, 0x03FF, 0x83FF, 0x0400, 0x7BFF, 0x8C00, 0xFBFF
    };
    for (auto a : specials) for (auto b : specials) feed(a, b);

    for (int k = 0; k < 3; k++) {
        dut->clk = 0; dut->eval();
        if (dut->ans != golden(histA[2], histB[2])) { failed++; }
        dut->clk = 1; dut->eval();
        histA[2]=histA[1]; histA[1]=histA[0];
        histB[2]=histB[1]; histB[1]=histB[0];
    }

    std::cout << "Tested " << tested << " pairs, failures = " << failed << "\n";
    delete dut;
    return (failed == 0) ? 0 : 1;
}
