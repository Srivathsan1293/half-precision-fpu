// src/fpu_FMUL.sv
/* verilator lint_off DECLFILENAME */
module FMUL (
    input logic [15:0] a,b,
    input logic clk,
    input logic nanA, nanB, infinA, infinB, A0, B0,
    output logic [15:0] ans
);
    //check for subnormality
    wire expA_zero = (a[14:10] == 5'd0);
    wire expB_zero = (b[14:10] == 5'd0);
    wire manA_zero = (a[9:0] == 10'd0);
    wire manB_zero = (b[9:0] == 10'd0);

    // Shared sign bit from top
    wire sign_bit = a[15] ^ b[15];

    // 1. Exception Pipeline Registers (shared flag handling from top)
    // Encode special case into 2 bits to save flip-flops
    logic [1:0] special_type;
    wire special = nanA | nanB | infinA | infinB | A0 | B0;
    always_comb begin
        if (nanA || nanB || (A0 && infinB) || (infinA && B0)) begin
            special_type = 2'b10; // NaN
        end else if (infinA || infinB) begin
            special_type = 2'b01; // Infinity
        end else if (!special) begin
            special_type = 2'b00; // Zero
        end else begin
            // Handle subnormal case as Zero output for consistency
            special_type = 2'b00;
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

    wire [15:0] ans_corrected_0;
    assign ans_corrected_0[15] = sign_bit;

    // 2. Consolidate Exponent Adders
    // Calculate a single base exponent to eliminate duplicate arithmetic logic
    reg [6:0] base_exp;
    /* verilator lint_off UNUSEDSIGNAL */
    reg [21:0] prod;
    /* verilator lint_on UNUSEDSIGNAL */

    always_ff @(posedge clk) begin
        base_exp <= expA + expB - 7'd15;
        prod <= manA * manB;
    end

    // Combinationally adjust if mantissa overflowed
    wire [6:0] exp_passed = prod[21] ? (base_exp + 7'd1) : base_exp;

    //choosing right bits for mantissa
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] mantissa_adj;
    /* verilator lint_on UNUSEDSIGNAL */

    assign mantissa_adj = prod[21] ? prod : {prod[20:0], 1'b0};

    //detect if number is subnormal
    wire is_pre_round_subnormal = exp_passed[6] | (exp_passed == 7'd0);
    wire [6:0] denorm_shift_amt;

    // Shift amount = 1 - Calculated Exponent
    assign denorm_shift_amt = 7'd1 - exp_passed;

    // Cap the shift at 22 so Verilog doesn't throw warnings for shifting beyond width
    wire [6:0] safe_shift = (denorm_shift_amt >= 7'd22) ? 7'd22 : denorm_shift_amt;

    // Shift the full 22-bit product
    wire [21:0] denorm_shifted_man = mantissa_adj >> (is_pre_round_subnormal ? safe_shift : 7'd0);

    /* verilator lint_off UNUSEDSIGNAL */
    wire [6:0] mask_shift = is_pre_round_subnormal ? safe_shift : 7'd0;

    // 3. Eliminate Parallel Comparators
    // Shift-based bitmask replaces the 23-iteration for-loop
    wire [22:0] shift_mask_full;
    assign shift_mask_full = ~(23'h7FFFFF << mask_shift);
    /* verilator lint_on UNUSEDSIGNAL */

    wire dropped_sticky = |(mantissa_adj & shift_mask_full[21:0]);

    // The true, subnormal-adjusted mantissa ready for rounding
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] pre_round_man = denorm_shifted_man | {21'd0, dropped_sticky};
    /* verilator lint_on UNUSEDSIGNAL */
    wire [6:0] pre_round_exp = is_pre_round_subnormal ? 7'd0 : exp_passed;

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

    // Pipeline only the minimal 3-bit state rather than a 17-bit vector
    reg [1:0] special_type_reg;
    reg special_reg;
    reg sign_bit_reg;
    always_ff @(posedge clk) begin
        special_type_reg <= special_type;
        special_reg <= special;
        sign_bit_reg <= sign_bit;
    end

    // Reconstruct the 16-bit special answer combinationally at the output
    logic [15:0] ans_special_out;
    always_comb begin
        ans_special_out[15] = sign_bit_reg;
        case (special_type_reg)
            2'b10: ans_special_out[14:0] = 15'b111111000000000; // NaN
            2'b01: ans_special_out[14:0] = 15'b111110000000000; // Infinity
            default: ans_special_out[14:0] = 15'b000000000000000; // Zero
        endcase
    end

    assign ans = special_reg ? ans_special_out : ans_corrected_0;

endmodule
