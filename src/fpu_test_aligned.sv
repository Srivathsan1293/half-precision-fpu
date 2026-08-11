// src/fpu_test.sv
module fpu_test (
    input logic [15:0] a, b,
    input logic [1:0] op,           // 0=ADD, 1=SUB, 2=MUL, 3=DIV
    input logic clk,
    output logic [15:0] ans
);

    // === FLAG COMPUTATION (shared across all 3 modules) ===
    wire expA_zero = (a[14:10] == 5'd0);
    wire expB_zero = (b[14:10] == 5'd0);
    wire expA_max  = (a[14:10] == 5'b11111);
    wire expB_max  = (b[14:10] == 5'b11111);
    wire manA_zero = (a[9:0] == 10'd0);
    wire manB_zero = (b[9:0] == 10'd0);

    // Common flags shared across all 3 modules
    wire nanA   = expA_max & (~manA_zero);
    wire nanB   = expB_max & (~manB_zero);
    wire infinA = expA_max & manA_zero;
    wire infinB = expB_max & manB_zero;
    wire A0     = expA_zero & manA_zero;
    wire B0     = expB_zero & manB_zero;

    // === INTERMEDIATE OUTPUTS ===
    wire [15:0] FADDSUB_out, FMUL_out, FDIV_out;

    // === INSTANTIATE MODULES (shared flag handling) ===
    addsub adder (
        .a(a), .b(b), .clk(clk), .sub(op[0]),
        .nanA(nanA), .nanB(nanB), .infinA(infinA), .infinB(infinB),
        .A0(A0), .B0(B0),
        .ans(FADDSUB_out)
    );

    FMUL multiplier (
        .a(a), .b(b), .clk(clk),
        .nanA(nanA), .nanB(nanB), .infinA(infinA), .infinB(infinB),
        .A0(A0), .B0(B0),
        .ans(FMUL_out)
    );

    DIV divider (
        .a(a), .b(b), .clk(clk),
        .nanA(nanA), .nanB(nanB), .infinA(infinA), .infinB(infinB),
        .A0(A0), .B0(B0),
        .out(FDIV_out)
    );

    // === PIPELINE ALIGNMENT ===
    // FDIV is 4-stage pipeline, FMUL/FADDSUB datapaths are 1-stage each.
    // Store FMUL and FADDSUB in three extra registers to align all outputs to 4 cycles.
    
    reg [15:0] FADDSUB_r, FADDSUB_r2, FADDSUB_r3;
    reg [15:0] FMUL_r, FMUL_r2, FMUL_r3;

    always_ff @(posedge clk) begin
        FADDSUB_r <= FADDSUB_out;
        FMUL_r <= FMUL_out;
        FADDSUB_r2 <= FADDSUB_r;
        FMUL_r2 <= FMUL_r;
        FADDSUB_r3 <= FADDSUB_r2;
        FMUL_r3 <= FMUL_r2;
    end

    // === OUTPUT MUX ===
    // op[1] selects between MUL/DIV (high) and ADD/SUB (low)
    // op[0] within each pair: 0=ADD/SUB, 1=MUL/DIV
    assign ans = op[1] ? (op[0] ? FDIV_out : FMUL_r3) : FADDSUB_r3;

endmodule
