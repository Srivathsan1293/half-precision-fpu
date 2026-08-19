#include <iostream>
#include <cstdint>
#include <cstdio>
#include <verilated.h>
#include "Vsrt_comb.h"

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vsrt_comb* dut = new Vsrt_comb;
    uint64_t fails = 0, stick_fails = 0;
    for (uint32_t m = 0; m < (1u << 22); m++) {
        uint16_t manA = 1024 + (m & 1023);
        uint16_t manB = 1024 + ((m >> 10) & 1023);
        dut->manA = manA; dut->manB = manB;
        dut->eval();
        uint16_t got = dut->Quotient;
        uint16_t exp = (uint16_t)(((uint32_t)manA * 8192) / manB);
        if (got != exp) {
            fails++;
            if (fails <= 20) printf("manA=%u manB=%u got=%u exp=%u\n", manA, manB, got, exp);
        }
        uint8_t exp_stick = (((uint32_t)manA * 8192) % manB) != 0;
        if (dut->sticky != exp_stick) {
            stick_fails++;
            if (stick_fails <= 20) printf("STICKY manA=%u manB=%u got=%u exp=%u\n", manA, manB, (unsigned)dut->sticky, exp_stick);
        }
    }
    printf("srt_comb quotient fails=%llu sticky fails=%llu\n",
           (unsigned long long)fails, (unsigned long long)stick_fails);
    delete dut;
    return (fails || stick_fails) ? 1 : 0;
}