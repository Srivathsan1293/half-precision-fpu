// tb/fpu_test_fmul_wrap.sv
//
// Build shim for the per-module MUL exhaustive testbench (tb_fMUL.cpp).
// That testbench was written for a single-cycle interface on an early fpu_test
// top (it drives only a/b/clk, reads `ans`, and includes "Vfpu_test.h"). This
// wrapper provides exactly that port set while still exercising the real FMUL
// datapath (fpu_FMUL.sv) with the shared NaN/Inf/zero flags computed locally
// (as the combined fpu_test top does).
//
// NOTE: the FMUL datapath is registered, so this single-cycle harness is only
// a build/interface shim for the per-module sweep; the timing-correct
// exhaustive coverage for all four ops comes from tb/tb_fpu.cpp against the
// old aligned fpu_test.sv.
module fpu_test (
    input logic [15:0] a, b,
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

    FMUL multiplier (
        .a(a), .b(b), .clk(clk),
        .nanA(nanA), .nanB(nanB), .infinA(infinA), .infinB(infinB),
        .A0(A0), .B0(B0),
        .ans(ans)
    );
endmodule
