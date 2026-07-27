// src/fpu_FMUL.sv
/* verilator lint_off DECLFILENAME */
module FMUL (
    input logic [15:0] a,b,
    output logic [15:0] ans
);
    //check for special cases 0, infinity, Nan;
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

    logic [15:0] ans_corrected_1;
    wire sign_bit = a[15] ^ b[15];

    always_comb begin
        ans_corrected_1[15] = sign_bit;
        if (nanA || nanB || (A0 && infinB) || (infinA && B0)) begin
            ans_corrected_1[14:0] = 15'b111111000000000; // NaN
        end else if (infinA || infinB) begin
            ans_corrected_1[14:0] = 15'b111110000000000; // Infinity
        end else begin
            ans_corrected_1[14:0] = 15'b000000000000000; // Zero
        end
    end


    //check for subnormality
    wire [9:0] sub_man_a, sub_man_b;
    wire [6:0] sub_exp_a, sub_exp_b;

    wire subA = expA_zero & (~manA_zero);
    wire subB = expB_zero & (~manB_zero);

    subnormal_fix sub_a (.a(a[9:0]), .adj_a(sub_man_a), .adj_exp_a(sub_exp_a));//apply subnormality fixes
    subnormal_fix sub_b (.a(b[9:0]), .adj_a(sub_man_b), .adj_exp_a(sub_exp_b));
    //choose whether to pass on subnormal adjusted value or normal
    wire [6:0] expA = subA ? sub_exp_a : {2'b00, a[14:10]};
    wire [6:0] expB = subB ? sub_exp_b : {2'b00, b[14:10]};

    wire [10:0] manA = subA ?  {1'b1, sub_man_a} : {1'b1, a[9:0]};
    wire [10:0] manB = subB ?  {1'b1, sub_man_b} : {1'b1, b[9:0]};

    //find sign
    wire [15:0] ans_corrected_0;
    wire signA = a[15]; wire signB = b[15];
    assign ans_corrected_0[15] = signA ^ signB;

    //find exponent
    //add 2 exponents
    wire [6:0] new_exp [3:1];
    //performing exp = expA + expB - 15
    assign new_exp[2] = expA + expB - 7'd15;
    //get version of exponent when there is mantissa overflow
    assign new_exp[1] = expA + expB - 7'd14;

    //multiply both mantissa
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] prod = manA * manB;
    /* verilator lint_on UNUSEDSIGNAL */

    //choosing normal exponent new_exp[2] or the overflowed exponent new_exp[1]
    assign new_exp[3] = prod[21] ? new_exp[1] : new_exp[2];

    //choosing right bits for mantissa
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] mantissa_adj;
    /* verilator lint_on UNUSEDSIGNAL */
    //mantissa is either 1.000... or 10.00000 so adjust accordingly

    assign mantissa_adj = prod[21] ? prod : {prod[20:0], 1'b0};

    //detect if number is subnormal
    wire is_pre_round_subnormal = new_exp[3][6] | (new_exp[3] == 7'd0);
    wire [6:0] denorm_shift_amt;

    // Shift amount = 1 - Calculated Exponent
    assign denorm_shift_amt = 7'd1 - new_exp[3];

    // Cap the shift at 22 so Verilog doesn't throw warnings for shifting beyond width
    wire [6:0] safe_shift = (denorm_shift_amt >= 7'd22) ? 7'd22 : denorm_shift_amt;

    // Shift the full 22-bit product
    wire [21:0] denorm_shifted_man = mantissa_adj >> (is_pre_round_subnormal ? safe_shift : 7'd0);

    // Any 1s that fall off the edge MUST be caught and OR'd into the sticky bit!
    /* verilator lint_off UNUSEDSIGNAL */
    wire [6:0] mask_shift = is_pre_round_subnormal ? safe_shift : 7'd0;
    reg [22:0] shift_mask_full;
    always_comb begin
        shift_mask_full = 23'd0;
        for (int i = 0; i < 23; i++) begin
            shift_mask_full[i] = (i < mask_shift);
        end
    end
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

    wire [10:0] rounded_man = {1'b0, pre_round_man[20:11]} + 11'd1;
    wire [6:0] rounded_exp =  pre_round_exp + 7'd1;


    wire round_up = G & (R | S | pre_round_man[11]);

    wire [9:0] right_mantissa = round_up ? (rounded_man[10] ? 10'd0 : rounded_man[9:0]) : pre_round_man[20:11];

    wire [6:0] final_exp = (round_up && rounded_man[10]) ? rounded_exp : pre_round_exp;

    // --- 3. FINAL OVERFLOW CHECK ---
    wire overflow = ~final_exp[6] & (final_exp[5] | (&final_exp[4:0]));

    assign ans_corrected_0[14:10] = overflow ? 5'b11111 : final_exp[4:0];
    assign ans_corrected_0[9:0] = overflow ? 10'd0 : right_mantissa;

    //selecting right answer when invalid inputs
    wire special = nanA | nanB | infinA | infinB | A0 | B0;
    assign ans = special ? ans_corrected_1 : ans_corrected_0;

endmodule
