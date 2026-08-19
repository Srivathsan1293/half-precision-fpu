// src/fpu_FDIV.sv
/* verilator lint_off DECLFILENAME */

// IEEE-754 half-precision divider using a sequential radix-4 SRT core.
//
// The SRT core (`srt` in fdiv_datapath_blocks.sv) is start-gated with a fixed
// 11-cycle schedule (reload, 8 iterations, emit, done). Operands are latched
// into the stage-1 registers on a `start` pulse, and the completed quotient is
// captured on the `srt_done` pulse 11 cycles later, giving a deterministic
// non-pipelined divider: one result every 12 cycles after `start` (11 cycles
// to the done capture edge, answer stable on the following cycle). The core is
// idle between divisions. Rounding / normalization is combinational from the
// captured values.
module DIV(
    input logic [15:0] a, b,
    input logic clk,
    input logic start,          // one-cycle pulse: begin a division of a/b
    input logic nanA, nanB, infinA, infinB, A0, B0,
    output logic [15:0] out,
    output logic done         // pulses when a new result is captured (srt_done)
);
    wire expA_zero = (a[14:10] == 5'd0);
    wire expB_zero = (b[14:10] == 5'd0);
    /* verilator lint_off UNUSEDSIGNAL */
    wire manA_zero = (a[9:0] == 10'd0);
    wire manB_zero = (b[9:0] == 10'd0);

    // Shared sign bits from top
    wire signA = a[15];
    wire signB = b[15];

    // 1. Find final sign: signA ^ signB.
    wire final_sign = signA ^ signB;

    // 2. Check for subnormal inputs
    wire subA, subB;
    assign subA = expA_zero & (~manA_zero);
    assign subB = expB_zero & (~manB_zero);

    // 3. Subnormal normalization utilizing shared module
    logic [10:0] sub_manA, sub_manB;
    wire [6:0] sub_expA, sub_expB;
    wire [9:0] adj_manA, adj_manB;

    subnormal_fix fixA (
        .a(a[9:0]),
        .adj_a(adj_manA),
        .adj_exp_a(sub_expA)
    );

    subnormal_fix fixB (
        .a(b[9:0]),
        .adj_a(adj_manB),
        .adj_exp_a(sub_expB)
    );

    assign sub_manA = {1'b1, adj_manA};
    assign sub_manB = {1'b1, adj_manB};

    // Choose between subnormal and normal.
    wire [6:0] final_expA, final_expB;
    wire [10:0] final_manA, final_manB;

    assign final_expA = subA ? sub_expA : {2'b00, a[14:10]};
    assign final_manA = subA ? sub_manA : {1'b1, a[9:0]};

    assign final_expB = subB ? sub_expB : {2'b00, b[14:10]};
    assign final_manB = subB ? sub_manB : {1'b1, b[9:0]};

    // Tentative exponent, aligned with the captured quotient.
    wire [6:0] tentative_exp = final_expA - final_expB + 7'd15;

    // 4. Sequential radix-4 SRT core, fed by the held stage-1 registers.
    wire [13:0] q_srt;
    wire srt_sticky, srt_done;

    srt srt_unit (
        .manA(final_manA_reg),
        .manB(final_manB_reg),
        .clk(clk),
        .start(start),
        .Quotient(q_srt),
        .sticky(srt_sticky),
        .done(srt_done)
    );

    // 5. START-GATED CAPTURE: on `start` the operands are latched into the
    //    stage-1 registers (the core reloads from them on the next counter==0)
    //    along with the exponent/sign/special metadata held in a "pending"
    //    register set. On `srt_done` (11 cycles later) the completed quotient
    //    is captured and the pending metadata is promoted to the aligned
    //    output registers. The core idles between divisions, so the latency
    //    from `start` to a valid `out` is fixed.
    reg [10:0] final_manA_reg;
    reg [10:0] final_manB_reg;
    reg [6:0]  tentative_exp_pending;
    reg        final_sign_pending;
    reg [15:0] special_ans_pending;
    reg        special_flag_pending;
    reg [6:0]  tentative_exp_reg;
    reg [13:0] q_reg;
    reg        sticky_reg;
    reg        final_sign_reg;
    reg [15:0] special_ans_reg;
    reg        special_flag_reg;

    always_ff @(posedge clk) begin
        if (start) begin
            final_manA_reg      <= final_manA;
            final_manB_reg      <= final_manB;
            tentative_exp_pending <= tentative_exp;
            final_sign_pending  <= final_sign;
            special_flag_pending <= nanA | nanB | A0 | B0 | infinA | infinB;
            if (nanA | nanB | (A0 & B0) | (infinA & infinB)) begin
                special_ans_pending <= {final_sign, 15'b111111000000000};
            end else if (infinA | B0) begin
                special_ans_pending <= {final_sign, 15'b111110000000000};
            end else begin
                special_ans_pending <= {final_sign, 15'd0};
            end
        end

        if (srt_done) begin
            q_reg              <= q_srt;
            sticky_reg         <= srt_sticky;
            tentative_exp_reg  <= tentative_exp_pending;
            final_sign_reg     <= final_sign_pending;
            special_ans_reg    <= special_ans_pending;
            special_flag_reg   <= special_flag_pending;
        end
    end

    // 6. PACKED PRODUCT: {q, 10'b0} with sticky at bit 0.
    wire [24:0] prod = sticky_reg ? (({1'b0, q_reg, 10'd0}) | 25'd1) : {1'b0, q_reg, 10'd0};

    // 7. PRE-SHIFT NORMALIZATION:
    wire [6:0] adjusted_exp = tentative_exp_reg - 7'd1;
    wire [6:0] normalised_exp = prod[23] ? tentative_exp_reg : adjusted_exp;
    wire [24:0] normalised_prod = prod[23] ? prod : (prod << 1);

    // 8. UNDERFLOW CHECK & SHIFT:
    wire underflow = normalised_exp[6] | (~|normalised_exp);
    wire [6:0] underflow_amt = normalised_exp[6] ? -normalised_exp : normalised_exp;

    wire [24:0] underflow_mask = ~(25'h1FFFFFF << (underflow_amt + 1));
    wire raw_tentative_S = |(normalised_prod & underflow_mask);
    wire tentative_S = underflow ? raw_tentative_S : 1'b0;

    wire [24:0] underflow_prod = (normalised_prod) >> (underflow_amt + 1);
    wire [6:0] underflow_exp = underflow ? 7'd0 : normalised_exp;
    wire [24:0] underflow_man = underflow ? underflow_prod : normalised_prod;

    // 9. EXTRACT ROUNDING BITS:
    wire G, R, S, round;
    wire [9:0] ans_man_0, ans_man_1;
    wire [6:0] ans_exp_0;

    assign ans_man_0 = underflow_man[22:13];
    assign G = underflow_man[12]; assign R = underflow_man[11]; assign S = | underflow_man[10:0];

    assign round = G & (R | S | tentative_S | ans_man_0[0]);

    // 10. ROUNDING:
    wire [10:0] rounding_full = ans_man_0 + {9'd0, round};
    wire [9:0] rounded = rounding_full[9:0];
    wire rounding_carry = rounding_full[10];

    // 11. POST-ROUND NORMALIZATION:
    assign ans_man_1 = rounding_carry ? 10'd0 : rounded;
    assign ans_exp_0 = rounding_carry ? underflow_exp + 1 : underflow_exp;

    // 12. OVERFLOW CHECK:
    wire [15:0] normal_ans;
    assign normal_ans[15] = final_sign_reg;
    assign normal_ans[14:10] = (ans_exp_0 > 7'd30) ? 5'b11111 : ans_exp_0[4:0];
    assign normal_ans[9:0] = (ans_exp_0 > 7'd30) ? 10'd0 : ans_man_1;

    // 13. Choose between flags output or the calculated output.
    assign out = (special_flag_reg) ? special_ans_reg : normal_ans;
    assign done = srt_done;
endmodule