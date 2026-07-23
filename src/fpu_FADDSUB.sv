// src/fpu_FADDSUB.sv
/* verilator lint_off DECLFILENAME */

module addsub(
    input logic [15:0] a,b,
    input logic sub,
    output logic [15:0] ans
);

    //checking for flags NAN/0/INFIN
    wire nanA, nanB, zeroA, zeroB, infinA, infinB;
    assign nanA = (& a[14:10]) & (| a[9:0]);
    assign nanB = (& b[14:10]) & (| b[9:0]);

    assign infinA = (& a[14:10]) & (& (~ a[9:0]));
    assign infinB = (& b[14:10]) & (& (~ b[9:0]));

    assign zeroA = (& (~ a[14:10])) & (& (~ a[9:0]));
    assign zeroB = (& (~ b[14:10])) & (& (~ b[9:0]));

    //extracting bits
    wire signA, init_signB, signB;
    wire [6:0] init_expA, init_expB;
    wire [21:0] init_manA, init_manB;

    assign signA = a[15]; assign init_signB = b[15];

    // Zero-extend the unsigned 5-bit exponents
    assign init_expA = {2'b00, a[14:10]};
    assign init_expB = {2'b00, b[14:10]};

    assign init_manA[20:0] = {a[9:0], {11{1'b0}}}; assign init_manB[20:0] = {b[9:0], {11{1'b0}}};

    //flipping sign of B if subtracting
    mux2x1 #(.WIDTH(1)) sign_flip (.in0(init_signB), .in1(~init_signB), .sel(sub), .out(signB));

    wire exp_zeroA = ~(| a[14:10]);
    wire exp_zeroB = ~(| b[14:10]);

    // Hidden bit is 1 for normals, 0 for subnormals and zero
    assign init_manA[21] = ~exp_zeroA;
    assign init_manB[21] = ~exp_zeroB;

    wire [6:0] int_expA, int_expB;
    /* verilator lint_off UNUSEDSIGNAL */
    wire [5:0] cout;
    /* verilator lint_on UNUSEDSIGNAL */
    //get unbiased signed value of exponents
    sub #(.WIDTH(7)) real_expA (.a(init_expA), .b(7'd15), .cout(cout[0]), .Sum(int_expA));
    sub #(.WIDTH(7)) real_expB (.a(init_expB), .b(7'd15), .cout(cout[1]), .Sum(int_expB));

    wire [6:0] corrected_expA, corrected_expB; // if subnormal exponent should be set to -14
    mux2x1 #(.WIDTH(7)) sub_expA (.in0(int_expA), .in1(7'b1110010), .sel(exp_zeroA), .out(corrected_expA));
    mux2x1 #(.WIDTH(7)) sub_expB (.in0(int_expB), .in1(7'b1110010), .sel(exp_zeroB), .out(corrected_expB));

    wire [6:0] difference; // find which number is larger
    sub #(.WIDTH(7)) comp (.a(corrected_expA), .b(corrected_expB), .cout(cout[2]), .Sum(difference));

    wire A_is_smaller = difference[6];
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] man_bigger  = A_is_smaller ? init_manB : init_manA;
    /* verilator lint_on UNUSEDSIGNAL */
    wire [21:0] man_smaller = A_is_smaller ? init_manA : init_manB;
    wire [6:0]  abs_diff    = A_is_smaller ? (~difference + 1'b1) : difference; //if B > A then difference is negative, to flip back to be used in shifting

    logic [21:0] aligned_smaller;
    logic align_sticky;
    always_comb begin
        if (abs_diff >= 22) begin
            aligned_smaller = 22'd0;
            align_sticky = |man_smaller; // Catch all dropped bits
        end else begin
            aligned_smaller = man_smaller >> abs_diff;
            align_sticky = |(man_smaller & ((22'd1 << abs_diff) - 22'd1)); // Bitmask dropped bits
        end
    end

    wire [6:0] final_exp; //choosing what exponent and sign to pass on for calculation
    wire final_sign;
    mux2x1 #(.WIDTH(7)) exp_sel (.in0(corrected_expA), .in1(corrected_expB), .sel(A_is_smaller), .out(final_exp));
    mux2x1 #(.WIDTH(1)) sign_sel (.in0(signA), .in1(signB), .sel(A_is_smaller), .out(final_sign));
    //if signs of input was different then we need to subtract bigger - smaller
    wire subtract = signA ^ signB;

    // Load full precision into ALU, combining captured sticky bits
    wire [13:0] alu_bigger  = {man_bigger[21:9], 1'b0};
    wire [13:0] alu_smaller = {aligned_smaller[21:9], align_sticky | (|aligned_smaller[8:0])};

    logic [14:0] addsub_man_norm;
    always_comb begin
        if (subtract) begin
            addsub_man_norm = {1'b0, alu_bigger} - {1'b0, alu_smaller};
        end else begin
            addsub_man_norm = {1'b0, alu_bigger} + {1'b0, alu_smaller};
        end
    end

    //check if the mantissa's sign is flipped when subtracting and fix that
    wire sign_temp;
    mux2x1 #(.WIDTH(1)) sign_fix (.in0(final_sign), .in1(~final_sign), .sel(subtract & addsub_man_norm[14]), .out(sign_temp));

    logic [14:0] addsub_man;
    mux2x1 #(.WIDTH(15)) man_fix (.in0(addsub_man_norm), .in1((~addsub_man_norm + 1'b1)), .sel(subtract & addsub_man_norm[14]), .out(addsub_man));

    // If effective subtraction results in exact zero, force positive sign (+0.0)
    wire sign = (addsub_man == 15'd0 && subtract) ? 1'b0 : sign_temp;

    wire [21:0] pre_norm_man = {addsub_man, 7'b0};
    logic [21:0] man_fixing;
    logic [6:0] exp_fixing;

    always_comb begin
        if (pre_norm_man == 22'd0) begin
            exp_fixing = -7'sd15;
            man_fixing = 22'd0;
        end else begin
            casez (pre_norm_man)
                22'b1zzzzzzzzzzzzzzzzzzzzz: begin exp_fixing = final_exp + 1; man_fixing = pre_norm_man >> 1; man_fixing[0] = pre_norm_man[0]; end
                22'b01zzzzzzzzzzzzzzzzzzzz: begin exp_fixing = final_exp; man_fixing = pre_norm_man; end
                22'b001zzzzzzzzzzzzzzzzzzz: begin exp_fixing = final_exp - 1; man_fixing = pre_norm_man << 1; end
                22'b0001zzzzzzzzzzzzzzzzzz: begin exp_fixing = final_exp - 2; man_fixing = pre_norm_man << 2; end
                22'b00001zzzzzzzzzzzzzzzzz: begin exp_fixing = final_exp - 3; man_fixing = pre_norm_man << 3; end
                22'b000001zzzzzzzzzzzzzzzz: begin exp_fixing = final_exp - 4; man_fixing = pre_norm_man << 4; end
                22'b0000001zzzzzzzzzzzzzzz: begin exp_fixing = final_exp - 5; man_fixing = pre_norm_man << 5; end
                22'b00000001zzzzzzzzzzzzzz: begin exp_fixing = final_exp - 6; man_fixing = pre_norm_man << 6; end
                22'b000000001zzzzzzzzzzzzz: begin exp_fixing = final_exp - 7; man_fixing = pre_norm_man << 7; end
                22'b0000000001zzzzzzzzzzzz: begin exp_fixing = final_exp - 8; man_fixing = pre_norm_man << 8; end
                22'b00000000001zzzzzzzzzzz: begin exp_fixing = final_exp - 9; man_fixing = pre_norm_man << 9; end
                22'b000000000001zzzzzzzzzz: begin exp_fixing = final_exp - 10; man_fixing = pre_norm_man << 10; end
                22'b0000000000001zzzzzzzzz: begin exp_fixing = final_exp - 11; man_fixing = pre_norm_man << 11; end
                22'b00000000000001zzzzzzzz: begin exp_fixing = final_exp - 12; man_fixing = pre_norm_man << 12; end
                22'b000000000000001zzzzzzz: begin exp_fixing = final_exp - 13; man_fixing = pre_norm_man << 13; end
                22'b0000000000000001zzzzzz: begin exp_fixing = final_exp - 14; man_fixing = pre_norm_man << 14; end
                22'b00000000000000001zzzzz: begin exp_fixing = final_exp - 15; man_fixing = pre_norm_man << 15; end
                default: begin exp_fixing = final_exp; man_fixing = pre_norm_man; end
            endcase
        end
    end

    //checking for subnormal answer

    /* verilator lint_off UNUSEDSIGNAL */
    logic [21:0] sub_man;
    /* verilator lint_on UNUSEDSIGNAL */
    logic [6:0] sub_exp;
    logic [6:0] shift_amt_signed;

    always_comb begin
        if ($signed(exp_fixing) < -7'sd14) begin
            shift_amt_signed = -7'sd14 - $signed(exp_fixing);
            sub_exp = -7'sd14;
            if (shift_amt_signed >= 22) begin
                sub_man = {21'd0, |man_fixing};
            end else begin
                sub_man = (man_fixing >> shift_amt_signed);
                sub_man[0] = sub_man[0] | (| (man_fixing & ((22'd1 << shift_amt_signed) - 22'd1)));
            end
        end else begin
            sub_man = man_fixing;
            sub_exp = exp_fixing;
            shift_amt_signed = 0;
        end
    end
    //applying rounding rules
    wire G_r = sub_man[9];
    wire R_r = sub_man[8];
    wire S_r = |sub_man[7:0];
    wire LSB_r = sub_man[10];

    wire round_up = G_r & (R_r | S_r | LSB_r);

    // Add 1 to combined hidden+fraction block to easily track carry overflow
    wire [11:0] rounded_fraction = sub_man[20:10] + 1'b1;

    logic [9:0] man;
    logic [6:0] exp;
    logic final_hidden_bit;

    always_comb begin
        if (round_up) begin
            if (rounded_fraction[11]) begin
                man = rounded_fraction[10:1];
                final_hidden_bit = rounded_fraction[11];
                exp = sub_exp + 1'b1;
            end else begin
                man = rounded_fraction[9:0];
                final_hidden_bit = rounded_fraction[10];
                exp = sub_exp;
            end
        end else begin
            man = sub_man[19:10];
            final_hidden_bit = sub_man[20];
            exp = sub_exp;
        end
    end

    wire overflow = ($signed(exp) > 7'sd15);

    logic [4:0] exp_packed;
    always_comb begin
        if ($signed(exp) <= -7'sd15) begin
            exp_packed = 5'd0; // True Zero
        end else if ($signed(exp) == -7'sd14 && final_hidden_bit == 1'b0) begin
            exp_packed = 5'd0; // Subnormal Number
        end else begin
            exp_packed = exp[4:0] + 5'd15; // Normal Number
        end
    end

    //sort between subnormal, normal, special case answers
    wire [15:0] ans_calculated;
    logic [15:0] ans_corrected;

    assign ans_calculated[15] = sign;
    mux2x1 #(.WIDTH(5)) exp_corrected (.in0(exp_packed), .in1(5'b11111), .sel(overflow), .out(ans_calculated[14:10]));
    mux2x1 #(.WIDTH(10)) man_corrected (.in0(man), .in1(10'd0), .sel(overflow), .out(ans_calculated[9:0]));

    always_comb begin
        ans_corrected = ans_calculated;

        if (nanA || nanB) begin
            ans_corrected = {1'b0, 5'b11111, 10'b1000000000};
        end else if (infinA && infinB && subtract) begin
            ans_corrected = {1'b0, 5'b11111, 10'b1000000000};
        end else if (infinA) begin
            ans_corrected = {a[15], 5'b11111, 10'd0};
        end else if (infinB) begin
            ans_corrected = {signB, 5'b11111, 10'd0};
        end else if (zeroA && zeroB) begin
            ans_corrected = {(signA & signB), 15'd0};
        end
    end

    assign ans = ans_corrected;

endmodule
