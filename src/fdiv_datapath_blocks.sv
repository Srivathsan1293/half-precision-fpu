// Sub-modules for DIV datapath PPA profiling

// Block 1: First Newton multiply + back-multiply + diff
module div_newton_backmul(
    input  logic [13:0] reciprocalB,
    input  logic [10:0] final_manA,
    input  logic [10:0] final_manB,
    output logic [14:0] q_trial,
    output logic [25:0] trial_A,
    output logic [25:0] shifted_A,
    output logic signed [27:0] diff,
    output logic signed [27:0] B_align_signed
);
    logic [24:0] initial_prod;
    assign initial_prod = reciprocalB * final_manA;
    assign q_trial = initial_prod[24:10];
    assign trial_A = q_trial * final_manB;
    assign shifted_A = {2'b00, final_manA, 13'd0};
    assign B_align_signed = $signed({17'd0, final_manB});
    assign diff = $signed({2'b00, shifted_A}) - $signed({2'b00, trial_A});
endmodule

// Block 2: Region comparison (7-branch refinement)
module div_region_compare(
    input  logic signed [27:0] diff,
    input  logic signed [27:0] B_align_signed,
    output logic signed [3:0]  q_adjust,
    output logic              sticky_final
);
    logic signed [27:0] B_x2;
    assign B_x2 = B_align_signed <<< 1;

    always_comb begin
        if (diff >= B_x2) begin
            q_adjust = 4'd2;
            sticky_final = (diff > B_x2);
        end else if (diff >= B_align_signed) begin
            q_adjust = 4'd1;
            sticky_final = (diff > B_align_signed);
        end else if (diff > 0) begin
            q_adjust = 4'd0;
            sticky_final = 1'b1;
        end else if (diff == 0) begin
            q_adjust = 4'd0;
            sticky_final = 1'b0;
        end else if (diff >= -B_align_signed) begin
            q_adjust = -4'd1;
            sticky_final = (diff != -B_align_signed);
        end else if (diff >= -B_x2) begin
            q_adjust = -4'd2;
            sticky_final = (diff != -B_x2);
        end else begin
            q_adjust = -4'd3;
            sticky_final = 1'b1;
        end
    end
endmodule

// Block 3: Quotient correction
module div_quot_correct(
    input  logic [14:0] q_trial,
    input  logic signed [3:0] q_adjust,
    input  logic               sticky_final,
    output logic [24:0] prod
);
    logic [14:0] q_final;
    assign q_final = q_trial + q_adjust;
    assign prod = {q_final, 9'd0, sticky_final};
endmodule

// Block 4: Pre-shift normalization + underflow
module div_norm(
    input  logic [24:0] prod,
    input  logic [6:0]  tentative_exp,
    output logic [6:0]  normalised_exp,
    output logic [24:0] normalised_prod,
    output logic        underflow,
    output logic [6:0]  underflow_amt,
    output logic        tentative_S,
    output logic [24:0] underflow_man
);
    assign normalised_exp = prod[23] ? tentative_exp : tentative_exp - 7'd1;
    assign normalised_prod = prod[23] ? prod : (prod << 1);

    assign underflow = normalised_exp[6] | (~|normalised_exp);
    assign underflow_amt = normalised_exp[6] ? -normalised_exp : normalised_exp;

    logic [24:0] underflow_mask;
    assign underflow_mask = (25'd1 << (underflow_amt + 1)) - 1'b1;

    logic raw_tentative_S;
    assign raw_tentative_S = |(normalised_prod & underflow_mask);
    assign tentative_S = underflow ? raw_tentative_S : 1'b0;

    logic [24:0] underflow_prod;
    assign underflow_prod = normalised_prod >> (underflow_amt + 1);
    assign underflow_man = underflow ? underflow_prod : normalised_prod;
endmodule
