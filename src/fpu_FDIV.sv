// src/fpu_FDIV.sv
/* verilator lint_off DECLFILENAME */

module DIV(
    input logic [15:0] a, b,
    input logic clk,
    input logic nanA, nanB, infinA, infinB, A0, B0,
    output logic [15:0] out
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
    /* verilator lint_off UNUSEDSIGNAL */ wire [10:0] final_manA, final_manB; /* verilator lint_on UNUSEDSIGNAL */
    /* verilator lint_off UNUSEDSIGNAL */ reg [10:0] final_manA_reg, final_manB_reg; /* verilator lint_on UNUSEDSIGNAL */

    assign final_expA = subA ? sub_expA : {2'b00, a[14:10]};
    assign final_manA = subA ? sub_manA : {1'b1, a[9:0]};

    assign final_expB = subB ? sub_expB : {2'b00, b[14:10]};
    assign final_manB = subB ? sub_manB : {1'b1, b[9:0]};

    // 4. Find tentative exponent: ExpA - ExpB + 15.
    reg [3:0][6:0] tentative_exp;

    wire [13:0] reciprocalB;
    reg [13:0] reciprocalB_reg;
    /* verilator lint_off UNUSEDSIGNAL */ reg [24:0] prod, initial_prod; /* verilator lint_on UNUSEDSIGNAL */

    reciprocal_rom rec_rom_inst (.addr(final_manB[9:0]), .data_out(reciprocalB));

    always_ff @(posedge clk) begin
        final_manA_reg <= final_manA;
        final_manB_reg <= final_manB;
        tentative_exp[0] <= final_expA - final_expB + 7'd15;
        reciprocalB_reg <= reciprocalB;
    end

    /* verilator lint_off WIDTHEXPAND */
    /* verilator lint_off WIDTHTRUNC */
    /* verilator lint_off UNUSEDSIGNAL */
    // 6. Multiply & EXACT Back-Multiply Quotient Refinement
    logic signed [27:0] diff; // Kept width to avoid warning on calculation
    reg [25:0] shifted_A;
    reg [10:0] final_manB_reg2;

    always_ff  @(posedge clk) begin
        tentative_exp[1] <= tentative_exp[0];
        initial_prod <= reciprocalB_reg * final_manA_reg;
        shifted_A <= {2'b00, final_manA_reg, 13'd0};
        final_manB_reg2 <= final_manB_reg;
    end

    // --- PIPELINE STAGE JUST AFTER THE 14x11 MULTIPLY ---
    // Register the back-multiply product AND all quotient-refinement operands
    // so the correction logic reads exclusively from this stage (same input).
    reg [14:0] q_trial_reg;
    reg [25:0] trial_A_reg;
    reg [25:0] shifted_A_reg;
    reg [10:0] final_manB_reg3;

    always_ff  @(posedge clk) begin
        tentative_exp[2] <= tentative_exp[1];
        q_trial_reg <= initial_prod[24:10];
        trial_A_reg <= initial_prod[24:10] * final_manB_reg2;
        shifted_A_reg <= shifted_A;
        final_manB_reg3 <= final_manB_reg2;
    end

    // --- COMBINATIONAL QUOTIENT REFINEMENT ---
    logic signed [14:0] q_final_comb;
    logic sticky_final_comb;

    // Truncated comparator bounds to save logic area
    logic signed [14:0] diff_trunc;
    logic signed [14:0] B_align_small;
    logic signed [14:0] B2;
    logic signed [3:0] corr;
    logic ge2B, geB, gt0, eq0, genB, gen2B;

    always_comb begin
        diff = $signed({2'b00, shifted_A_reg}) - $signed({2'b00, trial_A_reg});

        // Mathematical bounds guarantee the error magnitude fits in 15 signed bits
        diff_trunc = diff[14:0];
        B_align_small = {4'b0000, final_manB_reg3};
        B2 = B_align_small <<< 1;

        // Parallel predicate encoding of the correction tree (single shared adder)
        ge2B = (diff_trunc >= B2);
        geB  = (diff_trunc >= B_align_small);
        gt0  = (diff_trunc > 0);
        eq0  = (diff_trunc == 0);
        genB = (diff_trunc >= -B_align_small);
        gen2B= (diff_trunc >= -B2);

        corr = ge2B ? 4'd2 : geB ? 4'd1 : gt0 ? 4'd0 : eq0 ? 4'd0
             : genB ? -4'sd1 : gen2B ? -4'sd2 : -4'sd3;
        q_final_comb = $signed({1'b0, q_trial_reg}) + corr;

        sticky_final_comb = ge2B ? (diff_trunc > B2)
                          : geB ? (diff_trunc > B_align_small)
                          : gt0 ? 1'b1 : eq0 ? 1'b0
                          : genB ? (diff_trunc != -B_align_small)
                          : gen2B ? (diff_trunc != -B2) : 1'b1;
    end

    // --- CYCLE 3 REGISTER ---
    always_ff @(posedge clk) begin
        tentative_exp[3] <= tentative_exp[2];

        if (sticky_final_comb) begin
            prod <= ({q_final_comb, 10'd0}) | 25'd1;
        end else begin
            prod <= {q_final_comb, 10'd0};
        end
    end

    // 7. PRE-SHIFT NORMALIZATION:
    wire [6:0] adjusted_exp, normalised_exp;
    wire [24:0] normalised_prod;

    assign adjusted_exp = tentative_exp[3] - 7'd1;

    assign normalised_exp = prod[23] ? tentative_exp[3] : adjusted_exp;
    assign normalised_prod = prod[23] ? prod : (prod << 1);

    // 8. UNDERFLOW CHECK & SHIFT:
    wire underflow;
    wire [6:0] underflow_amt;
    wire raw_tentative_S, tentative_S;

    /* verilator lint_off UNUSEDSIGNAL */ wire [24:0] underflow_prod, underflow_man; /* verilator lint_on UNUSEDSIGNAL */
    wire [6:0] underflow_exp;

    assign underflow = normalised_exp[6] | (~|normalised_exp);
    assign underflow_amt = normalised_exp[6] ? -normalised_exp : normalised_exp;

    // Shift-based mask replaces 25-iteration comparator loop
    wire [24:0] underflow_mask;
    assign underflow_mask = ~(25'h1FFFFFF << (underflow_amt + 1));

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
    assign normal_ans[15] = special_ans[3][15];
    assign normal_ans[14:10] = (ans_exp_0 > 7'd30) ? 5'b11111 : ans_exp_0[4:0];
    assign normal_ans[9:0] = (ans_exp_0 > 7'd30) ? 10'd0 : ans_man_1;

    // 13. FLAGS & OVERRIDES:
    reg [3:0][15:0] special_ans;
    reg [3:0] special_flag;

    always_ff @(posedge clk) begin
        special_flag[0] <= nanA | nanB | A0 | B0 | infinA | infinB;
        if (nanA | nanB | (A0 & B0) | (infinA & infinB)) begin
            special_ans[0] <= {final_sign, 15'b111111000000000};
        end else if (infinA | B0) begin
            special_ans[0] <= {final_sign, 15'b111110000000000};
        end else begin
            special_ans[0] <= {final_sign, 15'd0};
        end
    end

    always_ff @(posedge clk) begin
        special_ans[1] <= special_ans[0];
        special_flag[1] <= special_flag[0];
    end

    always_ff @(posedge clk) begin
        special_ans[2] <= special_ans[1];
        special_flag[2] <= special_flag[1];
    end

    always_ff @(posedge clk) begin
        special_ans[3] <= special_ans[2];
        special_flag[3] <= special_flag[2];
    end

    // 14. Choose between flags output or the calculated output.
    assign out = (special_flag[3]) ? special_ans[3] : normal_ans;
/* verilator lint_on UNUSEDSIGNAL */
/* verilator lint_on WIDTHEXPAND */
/* verilator lint_on WIDTHTRUNC */
endmodule
