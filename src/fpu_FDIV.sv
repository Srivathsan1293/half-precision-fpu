// src/fpu_FDIV.sv
/* verilator lint_off DECLFILENAME */

module DIV(
    input logic [15:0] a, b,
    output logic [15:0] out
);

    // 1. Find final sign: signA ^ signB.
    wire signA, signB;
    assign signA = a[15]; assign signB = b[15];
    wire final_sign = signA ^ signB;

    // 2. Check for subnormal inputs (exp == 5'b00000 & man != 10'b0). and normalize that
    wire subA, subB;
    assign subA = (&(~a[14:10])) & (|a[9:0]);
    assign subB = (&(~b[14:10])) & (|b[9:0]);
    logic [10:0] sub_manA, sub_manB;
    logic [4:0] shift_amtA, shift_amtB;

    //LZD for subnormals
    always_comb begin
        casez (a[9:0])
            10'b1zzzzzzzzz: begin sub_manA = {1'b1, (a[9:0] << 1)}; shift_amtA = 1; end
            10'b01zzzzzzzz: begin sub_manA = {1'b1, (a[9:0] << 2)}; shift_amtA = 2; end
            10'b001zzzzzzz: begin sub_manA = {1'b1, (a[9:0] << 3)}; shift_amtA = 3; end
            10'b0001zzzzzz: begin sub_manA = {1'b1, (a[9:0] << 4)}; shift_amtA = 4; end
            10'b00001zzzzz: begin sub_manA = {1'b1, (a[9:0] << 5)}; shift_amtA = 5; end
            10'b000001zzzz: begin sub_manA = {1'b1, (a[9:0] << 6)}; shift_amtA = 6; end
            10'b0000001zzz: begin sub_manA = {1'b1, (a[9:0] << 7)}; shift_amtA = 7; end
            10'b00000001zz: begin sub_manA = {1'b1, (a[9:0] << 8)}; shift_amtA = 8; end
            10'b000000001z: begin sub_manA = {1'b1, (a[9:0] << 9)}; shift_amtA = 9; end
            10'b0000000001: begin sub_manA = {1'b1, (a[9:0] << 10)}; shift_amtA = 10; end
            default:         begin sub_manA = {1'b1, a[9:0]}; shift_amtA = 0; end
        endcase
        casez (b[9:0])
            10'b1zzzzzzzzz: begin sub_manB = {1'b1, (b[9:0] << 1)}; shift_amtB = 1; end
            10'b01zzzzzzzz: begin sub_manB = {1'b1, (b[9:0] << 2)}; shift_amtB = 2; end
            10'b001zzzzzzz: begin sub_manB = {1'b1, (b[9:0] << 3)}; shift_amtB = 3; end
            10'b0001zzzzzz: begin sub_manB = {1'b1, (b[9:0] << 4)}; shift_amtB = 4; end
            10'b00001zzzzz: begin sub_manB = {1'b1, (b[9:0] << 5)}; shift_amtB = 5; end
            10'b000001zzzz: begin sub_manB = {1'b1, (b[9:0] << 6)}; shift_amtB = 6; end
            10'b0000001zzz: begin sub_manB = {1'b1, (b[9:0] << 7)}; shift_amtB = 7; end
            10'b00000001zz: begin sub_manB = {1'b1, (b[9:0] << 8)}; shift_amtB = 8; end
            10'b000000001z: begin sub_manB = {1'b1, (b[9:0] << 9)}; shift_amtB = 9; end
            10'b0000000001: begin sub_manB = {1'b1, (b[9:0] << 10)}; shift_amtB = 10; end
            default:         begin sub_manB = {1'b1, b[9:0]}; shift_amtB = 0; end
        endcase
    end

    //get new exponent (7-bit to capture sign bit and prevent overflow. Starts at biased 1)
    wire [6:0] sub_expA, sub_expB;
    assign sub_expA = 7'd1 - {2'b00, shift_amtA};
    assign sub_expB = 7'd1 - {2'b00, shift_amtB};

    //choose between subnormal and normal.
    wire [6:0] final_expA, final_expB;
    /* verilator lint_off UNUSEDSIGNAL */ wire [10:0] final_manA, final_manB; /* verilator lint_on UNUSEDSIGNAL */

    assign final_expA = subA ? sub_expA : {2'b00, a[14:10]};
    assign final_manA = subA ? sub_manA : {1'b1, a[9:0]};

    assign final_expB = subB ? sub_expB : {2'b00, b[14:10]};
    assign final_manB = subB ? sub_manB : {1'b1, b[9:0]};

    // 4. Find tentative exponent: ExpA - ExpB + 15.
    wire [6:0] tentative_exp;
    wire [6:0] A_minus_B;

    assign A_minus_B = final_expA - final_expB;
    assign tentative_exp = A_minus_B + 7'd15;

    wire [13:0] reciprocalB;
    /* verilator lint_off UNUSEDSIGNAL */ logic [24:0] initial_prod, prod; /* verilator lint_on UNUSEDSIGNAL */

    reciprocal_rom rec_rom_inst (
        .addr(final_manB[9:0]),
        .data_out(reciprocalB)
    );

    // 6. Multiply & EXACT Back-Multiply Quotient Refinement
    logic [14:0] q_trial;
    logic [25:0] trial_A;
    logic signed [27:0] diff;
    logic [25:0] shifted_A;
    logic signed [27:0] B_align_signed;
    logic [14:0] q_final;
    logic sticky_final;

    always_comb begin
        initial_prod = reciprocalB * final_manA;
        q_trial = initial_prod[24:10];
        trial_A = q_trial * final_manB;
        shifted_A = {2'b00, final_manA, 13'd0};
        diff = $signed({2'b00, shifted_A}) - $signed({2'b00, trial_A});
        B_align_signed = $signed({17'd0, final_manB});
        if (diff >= (B_align_signed <<< 1)) begin
            q_final = q_trial + 15'd2;
            sticky_final = (diff > (B_align_signed <<< 1));
        end else if (diff >= B_align_signed) begin
            q_final = q_trial + 15'd1;
            sticky_final = (diff > B_align_signed);
        end else if (diff > 0) begin
            q_final = q_trial;
            sticky_final = 1'b1;
        end else if (diff == 0) begin
            q_final = q_trial;
            sticky_final = 1'b0;
        end else if (diff >= -B_align_signed) begin
            q_final = q_trial - 15'd1;
            sticky_final = (diff != -B_align_signed);
        end else if (diff >= -(B_align_signed <<< 1)) begin
            q_final = q_trial - 15'd2;
            sticky_final = (diff != -(B_align_signed <<< 1));
        end else begin
            q_final = q_trial - 15'd3;
            sticky_final = 1'b1;
        end
        // Assemble 25-bit product
        prod = {q_final, 10'd0};
        if (sticky_final) prod = prod | 25'd1;
    end

    // 7. PRE-SHIFT NORMALIZATION:
    wire [6:0] adjusted_exp, normalised_exp;
    wire [24:0] normalised_prod;

    assign adjusted_exp = tentative_exp - 7'd1;

    assign normalised_exp = prod[23] ? tentative_exp : adjusted_exp;
    assign normalised_prod = prod[23] ? prod : (prod << 1);

    // 8. UNDERFLOW CHECK & SHIFT:
    wire underflow;
    wire [6:0] underflow_amt;
    wire raw_tentative_S, tentative_S;

    /* verilator lint_off UNUSEDSIGNAL */ wire [24:0] underflow_prod, underflow_man; /* verilator lint_on UNUSEDSIGNAL */
    wire [6:0] underflow_exp;

    assign underflow = normalised_exp[6] | (~|normalised_exp);
    assign underflow_amt = normalised_exp[6] ? -normalised_exp : normalised_exp;

    // FMUL-style mask: comparator loop instead of barrel shifter + subtractor
    reg [24:0] underflow_mask;
    always_comb begin
        underflow_mask = 25'd0;
        for (int i = 0; i < 25; i++) begin
            /* verilator lint_off WIDTHEXPAND */
            underflow_mask[i] = (i < (underflow_amt + 1));
            /* verilator lint_on WIDTHEXPAND */
        end
    end

    assign raw_tentative_S = |(normalised_prod & underflow_mask);
    assign tentative_S = underflow ? raw_tentative_S : 1'b0;

    assign underflow_prod = (normalised_prod) >> (underflow_amt + 1);

    assign underflow_exp = underflow ? 7'd0 : normalised_exp;
    assign underflow_man = underflow ? underflow_prod : normalised_prod;

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
    assign normal_ans[14:10] = (ans_exp_0 > 7'd30) ? 5'b11111 : ans_exp_0[4:0];
    assign normal_ans[9:0] = (ans_exp_0 > 7'd30) ? 10'd0 : ans_man_1;
    assign normal_ans[15] = final_sign;

    // 13. FLAGS & OVERRIDES:
    wire nanA, nanB, infinA, infinB, A0, B0;
    assign nanA = (& a[14:10]) & (| a[9:0]);
    assign nanB = (& b[14:10]) & (| b[9:0]);
    assign infinA = (& a[14:10]) & (& (~ a[9:0]));
    assign infinB = (& b[14:10]) & (& (~ b[9:0]));
    assign A0 = (& (~ a[14:10])) & (& (~ a[9:0]));
    assign B0 = (& (~ b[14:10])) & (& (~ b[9:0]));

    logic [15:0] special_ans;

    always_comb begin
        if (nanA | nanB | (A0 & B0) | (infinA & infinB)) begin
            special_ans = {final_sign, 15'b111111000000000};
        end else if (infinA | B0) begin
            special_ans = {final_sign, 15'b111110000000000};
        end else begin
            special_ans = {final_sign, 15'd0};
        end
    end

    // 14. Choose between flags output or the calculated output.
    assign out = (nanA | nanB | A0 | B0 | infinA | infinB) ? special_ans : normal_ans;
endmodule
