// src/fpu_FMUL.sv
/* verilator lint_off DECLFILENAME */
module FMUL (
    input logic [15:0] a,b,
    output logic [15:0] ans
);
    //check for special cases 0, infinity, Nan;
    wire nanA, nanB, infinA, infinB, A0, B0;

    assign nanA = (& a[14:10]) & (| a[9:0]);
    assign nanB = (& b[14:10]) & (| b[9:0]);

    assign infinA = (& a[14:10]) & (& (~ a[9:0]));
    assign infinB = (& b[14:10]) & (& (~ b[9:0]));

    assign A0 = (& (~ a[14:10])) & (& (~ a[9:0]));
    assign B0 = (& (~ b[14:10])) & (& (~ b[9:0]));

    logic [15:0] ans_corrected_0, ans_corrected_1;

    always_comb begin
        ans_corrected_1[15] = ans_corrected_0[15];

        if (nanA || nanB) begin //answer is NAN, inputs were NAN
            ans_corrected_1[14:0] = 15'b111111000000000;
        end else if ((A0 && infinB) || (infinA && B0)) begin //answer is NAN.
            // invalid operation
            ans_corrected_1[14:0] = 15'b111111000000000;
        end else if (infinA || infinB) begin //answer is infinity, one input was infinity
            ans_corrected_1[14:0] = 15'b111110000000000;
        end else if (A0 || B0) begin //answer is 0, input was 0
            ans_corrected_1[14:0] = 15'b000000000000000;
        end else begin
            ans_corrected_1[14:0] = 15'b000000000000000;
        end
    end

    //splitting inputs
    wire [0:0] signA, signB;
    wire [10:0] manA, manB;
    wire [6:0] expA, expB; //bit larger than 5 bit exponent to better handle overflow

    //check for subnormality
    wire subA, subB;
    wire [9:0] sub_man_a, sub_man_b;
    wire [6:0] sub_exp_a, sub_exp_b;

    assign subA = &(~a[14:10]) & (| a[9:0]); //checking subnormality when exp is 0 and man is non-zero
    assign subB = &(~b[14:10]) & (| b[9:0]);

    subnormal_fix sub_a (.a(a[9:0]), .adj_a(sub_man_a), .adj_exp_a(sub_exp_a));//apply subnormality fixes
    subnormal_fix sub_b (.a(b[9:0]), .adj_a(sub_man_b), .adj_exp_a(sub_exp_b));
    //choose whether to pass on subnormal adjusted value or normal
    mux2x1 #(.WIDTH(7)) sub_expA (.in0({2'b00, a[14:10]}), .in1(sub_exp_a), .sel(subA), .out(expA));
    mux2x1 #(.WIDTH(7)) sub_expB (.in0({2'b00, b[14:10]}), .in1(sub_exp_b), .sel(subB), .out(expB));

    mux2x1 #(.WIDTH(11)) sub_manA (.in0({1'b1, a[9:0]}), .in1({1'b1, sub_man_a}), .sel(subA), .out(manA));
    mux2x1 #(.WIDTH(11)) sub_manB (.in0({1'b1, b[9:0]}), .in1({1'b1, sub_man_b}), .sel(subB), .out(manB));

    assign signA = a[15]; assign signB = b[15];
    //find sign
    assign ans_corrected_0[15] = signA ^ signB;

    //find exponent
    //add 2 exponents
    wire [6:0] new_exp [3:0];
    /* verilator lint_off UNUSEDSIGNAL */
    logic ignoring_carry [5:0];
    /* verilator lint_on UNUSEDSIGNAL */
    //performing exp = expA + expB - 15
    add #(.WIDTH(7)) exp_add (.a(expA), .b(expB), .cout(ignoring_carry[0]), .Sum(new_exp[0]));
    sub #(.WIDTH(7)) exp_add_adj (.a(new_exp[0]), .b(7'd15), .cout(ignoring_carry[2]), .Sum(new_exp[2]));
    //get version of exponent when there is mantissa overflow
    add #(.WIDTH(7)) exp_add_1 (.a(new_exp[2]), .b(7'b0000001), .cout(ignoring_carry[1]), .Sum(new_exp[1]));

    //multiply both mantissa
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] prod;
    /* verilator lint_on UNUSEDSIGNAL */
    MUL mantissa (.A(manA), .B(manB), .ans(prod)); //standard 11x11 MUL

    //choosing normal exponent new_exp[2] or the overflowed exponent new_exp[1]
    mux2x1 #(.WIDTH(7)) exp_sel (.in0(new_exp[2]), .in1(new_exp[1]), .sel(prod[21]), .out(new_exp[3]));

    //choosing right bits for mantissa
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] mantissa_adj;
    /* verilator lint_on UNUSEDSIGNAL */
    //mantissa is either 1.000... or 10.00000 so adjust accordingly
    mux2x1 #(.WIDTH(22)) man_sel (.in0(prod << 1), .in1(prod), .sel(prod[21]), .out(mantissa_adj));

    //detect if number is subnormal
    wire is_pre_round_subnormal = new_exp[3][6] | (new_exp[3] == 7'd0);
    wire [6:0] denorm_shift_amt;

    /* verilator lint_off UNUSEDSIGNAL */
    wire denorm_shift_cout;
    /* verilator lint_on UNUSEDSIGNAL */

    // Shift amount = 1 - Calculated Exponent
    sub #(.WIDTH(7)) calc_denorm_shift (.a(7'd1), .b(new_exp[3]), .cout(denorm_shift_cout), .Sum(denorm_shift_amt));

    // Cap the shift at 22 so Verilog doesn't throw warnings for shifting beyond width
    wire [6:0] safe_shift = (denorm_shift_amt >= 7'd22) ? 7'd22 : denorm_shift_amt;

    // Shift the full 22-bit product
    wire [21:0] denorm_shifted_man = mantissa_adj >> (is_pre_round_subnormal ? safe_shift : 7'd0);

    // Any 1s that fall off the edge MUST be caught and OR'd into the sticky bit!
    /* verilator lint_off UNUSEDSIGNAL */
    wire [22:0] shift_mask_full = (23'd1 << (is_pre_round_subnormal ? safe_shift : 7'd0)) - 23'd1;
    /* verilator lint_on UNUSEDSIGNAL */
    wire dropped_sticky = |(mantissa_adj & shift_mask_full[21:0]);

    // The true, subnormal-adjusted mantissa ready for rounding
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] pre_round_man = denorm_shifted_man | {21'd0, dropped_sticky};
    /* verilator lint_on UNUSEDSIGNAL */
    wire [6:0] pre_round_exp = is_pre_round_subnormal ? 7'd0 : new_exp[3];

    // --- 2. APPLY RNTE ROUNDING ---
    wire G, R, S;
    assign G = pre_round_man[10];
    assign R = pre_round_man[9];
    assign S = | pre_round_man[8:0];

    logic [9:0] right_mantissa;
    logic [6:0] final_exp;
    wire [9:0] rounded_man;
    wire [6:0] rounded_exp;
    wire round_carry;

    /* verilator lint_off UNUSEDSIGNAL */
    wire exp_carry_out;
    /* verilator lint_on UNUSEDSIGNAL */

    add #(.WIDTH(10)) rounding (.a(pre_round_man[20:11]), .b(10'd1), .cout(round_carry), .Sum(rounded_man));
    add #(.WIDTH(7)) exp_add_2 (.a(pre_round_exp), .b(7'b0000001), .cout(exp_carry_out), .Sum(rounded_exp));

    always_comb begin
        if (G & (R | S | pre_round_man[11])) begin
            // We need to round up
            right_mantissa = rounded_man;
            if (round_carry) begin
                final_exp = rounded_exp; // Mantissa overflowed
            end else begin
                final_exp = pre_round_exp;
            end
        end else begin
            // No rounding needed
            right_mantissa = pre_round_man[20:11];
            final_exp = pre_round_exp;
        end
    end

    // --- 3. FINAL OVERFLOW CHECK ---
    wire overflow  = (~final_exp[6]) & (final_exp >= 7'd31);

    mux2x1 #(.WIDTH(5)) exp_corrected (.in0(final_exp[4:0]), .in1(5'b11111), .sel(overflow), .out(ans_corrected_0[14:10]));
    mux2x1 #(.WIDTH(10)) man_corrected (.in0(right_mantissa), .in1(10'd0), .sel(overflow), .out(ans_corrected_0[9:0]));

    //selecting right answer when invalid inputs
    mux2x1 #(.WIDTH(16)) ans_sel (.in0(ans_corrected_0), .in1(ans_corrected_1), .sel(nanA  | nanB | infinA | infinB | A0 | B0), .out(ans));

endmodule
