// tb/fpu_test_addsub_wrap.sv
//
// Build shim for the per-module ADDSUB exhaustive testbench (tb_fADDSUB.cpp).
// That testbench was written for a single-cycle interface with a direct `sub`
// input (early fpu_test design), so it drives a/b/sub/clk and reads `ans` and
// includes "Vfpu_test.h". This wrapper provides exactly that port set while
// still exercising the real addsub datapath (fpu_FADDSUB.sv) with the shared
// NaN/Inf/zero flags computed locally (as the combined fpu_test top does).
//
// NOTE: the addsub datapath is registered, so this single-cycle harness is
// only a build/interface shim for the per-module sweep; the timing-correct
// exhaustive coverage for all four ops comes from tb/tb_fpu.cpp against the
// old aligned fpu_test.sv.
module fpu_test (
    input logic [15:0] a, b,
    input logic sub,
    input logic clk,
    output logic [15:0] ans
);
    wire expA_zero = (a[14:10] == 5'd0);
    wire expB_zero = (b[14:10] == 5'd0);
    wire expA_max  = (a[14:10] == 5'b11111);
    wire expB_max  = (b[14:10] == 5'b11111);
    wire manA_zero = (a[9:0] == 10'd0);
    wire manB_zero = (b[9:0] == 10'd0);
    wire nanA   = expA_max & (~manA_zero);
    wire nanB   = expB_max & (~manB_zero);
    wire infinA = expA_max & manA_zero;
    wire infinB = expB_max & manB_zero;
    wire A0     = expA_zero & manA_zero;
    wire B0     = expB_zero & manB_zero;

    addsub adder (
        .a(a), .b(b), .clk(clk), .sub(sub),
        .nanA(nanA), .nanB(nanB), .infinA(infinA), .infinB(infinB),
        .A0(A0), .B0(B0),
        .ans(ans)
    );
endmodule
