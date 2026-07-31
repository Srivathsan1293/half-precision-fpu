// src/fpu_FADDSUB.sv
/* verilator lint_off DECLFILENAME */
module addsub(
    input logic [15:0] a,b,
    input logic clk,
    input logic sub,
    output logic [15:0] ans
);
    //gettin flags for special case answers
    wire expA_zero = (a[14:10] == 5'd0);
    wire expB_zero = (b[14:10] == 5'd0);
    wire expA_max  = (a[14:10] == 5'b11111);
    wire expB_max  = (b[14:10] == 5'b11111);
    wire manA_zero = (a[9:0] == 10'd0);
    wire manB_zero = (b[9:0] == 10'd0);

    reg nanA, nanB, infinA, infinB, zeroA, zeroB;

    always_ff @(posedge clk) begin
        nanA   <= expA_max & (~manA_zero);
        nanB   <= expB_max & (~manB_zero);
        infinA <= expA_max & manA_zero;
        infinB <= expB_max & manB_zero;
        zeroA  <= expA_zero & manA_zero;
        zeroB  <= expB_zero & manB_zero;
    end

    //if subtraction then flip sign B
    wire signA = a[15]; wire signB = sub ? ~b[15] : b[15];

    //splitting inputs
    wire [6:0] init_expA = {2'b00, a[14:10]};
    wire [6:0] init_expB = {2'b00, b[14:10]};

    wire [21:0] init_manA = { ~expA_zero, a[9:0], {11{1'b0}}};
    wire [21:0] init_manB = { ~expB_zero, b[9:0], {11{1'b0}}};
    //getting unbiased exponential
    wire [6:0] corrected_expA = expA_zero ? 7'b1110010 : (init_expA - 7'd15);
    wire [6:0] corrected_expB = expB_zero ? 7'b1110010 : (init_expB - 7'd15);

    wire [6:0] difference = corrected_expA - corrected_expB;
    wire A_is_smaller = difference[6];
    //choosing which values are passed on
     /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] man_bigger  = A_is_smaller ? init_manB : init_manA;
     /* verilator lint_on UNUSEDSIGNAL */
    wire [21:0] man_smaller = A_is_smaller ? init_manA : init_manB;
    wire [6:0]  abs_diff    = A_is_smaller ? (-difference) : difference;
    //adjusting mantissa's to match and catching sticky bits
    logic [21:0] aligned_smaller;
    logic align_sticky;
    always_comb begin
        if (abs_diff >= 22) begin
            aligned_smaller = 22'd0;
            align_sticky = |man_smaller;
        end else begin
            aligned_smaller = man_smaller >> abs_diff;
            align_sticky = 1'b0;
            for (int i = 0; i < 22; i++) begin
                if (i < abs_diff && man_smaller[i]) align_sticky = 1'b1;
            end
        end
    end

    wire [6:0] final_exp = A_is_smaller ? corrected_expB : corrected_expA;
    wire final_sign = A_is_smaller ? signB : signA;
    wire subtract = signA ^ signB;
    //finding manA - manB and normalising
    wire [13:0] alu_bigger  = {man_bigger[21:9], 1'b0};
    wire [13:0] alu_smaller = {aligned_smaller[21:9], align_sticky | (|aligned_smaller[8:0])};

    wire [14:0] addsub_man_norm = subtract ? ({1'b0, alu_bigger} - {1'b0, alu_smaller}) : ({1'b0, alu_bigger} + {1'b0, alu_smaller});
    //checking if sign flipped from subtraction
    wire sign_temp = (subtract & addsub_man_norm[14]) ? ~final_sign : final_sign;
    wire [14:0] addsub_man = (subtract & addsub_man_norm[14]) ? (-addsub_man_norm) : addsub_man_norm;
    //checking if answer is 0 set sign to 0 as per standard
    wire sign = (addsub_man == 15'd0 && subtract) ? 1'b0 : sign_temp;

    reg [21:0] pre_norm_man;
    logic [21:0] man_fixing;
    logic [6:0] exp_fixing;
    reg subtract_reg;
    reg sign_reg;


    always_ff @(posedge clk) begin
        subtract_reg <= subtract;
        sign_reg <= sign;
        pre_norm_man <= {addsub_man, 7'b0};
    end

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
     /* verilator lint_off UNUSEDSIGNAL */
    logic [21:0] sub_man;
    /* verilator lint_on UNUSEDSIGNAL */
    logic [6:0] sub_exp;
    logic [6:0] shift_amt_signed;
    // checking is answer is subnormal
    always_comb begin
        if ($signed(exp_fixing) < -7'sd14) begin
            shift_amt_signed = -7'sd14 - $signed(exp_fixing);
            sub_exp = -7'sd14;
            if (shift_amt_signed >= 22) begin
                sub_man = {21'd0, |man_fixing};
            end else begin
                sub_man = (man_fixing >> shift_amt_signed);
                for (int i = 0; i < 22; i++) begin
                    if (i < shift_amt_signed && man_fixing[i]) sub_man[0] = 1'b1;
                end
            end
        end else begin
            sub_man = man_fixing;
            sub_exp = exp_fixing;
            shift_amt_signed = 0;
        end
    end



    // applying RNTE rounding
    wire G_r = sub_man[9];
    wire R_r = sub_man[8];
    wire S_r = |sub_man[7:0];
    wire LSB_r = sub_man[10];

    wire round_up = G_r & (R_r | S_r | LSB_r);

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

    wire overflow = ~exp[6] & (exp[5] | exp[4]);

    logic [4:0] exp_packed;
    always_comb begin
        if ($signed(exp) <= -7'sd15) begin
            exp_packed = 5'd0;
        end else if ($signed(exp) == -7'sd14 && final_hidden_bit == 1'b0) begin
            exp_packed = 5'd0;
        end else begin
            exp_packed = exp[4:0] + 5'd15;
        end
    end

    wire [15:0] ans_calculated;
    logic [15:0] ans_corrected;

    assign ans_calculated[15] = sign_reg;
    assign ans_calculated[14:10] = overflow ? 5'b11111 : exp_packed;
    assign ans_calculated[9:0] = overflow ? 10'd0 : man;

    reg signA_reg, signB_reg;

    always_ff @(posedge clk) begin
        signA_reg <= signA;
        signB_reg <= signB;
    end


    always_comb begin
        ans_corrected = ans_calculated;

        if (nanA || nanB) begin
            ans_corrected = {1'b0, 5'b11111, 10'b1000000000};
        end else if (infinA && infinB && subtract_reg) begin
            ans_corrected = {1'b0, 5'b11111, 10'b1000000000};
        end else if (infinA) begin
            ans_corrected = {signA_reg, 5'b11111, 10'd0};
        end else if (infinB) begin
            ans_corrected = {signB_reg, 5'b11111, 10'd0};
        end else if (zeroA && zeroB) begin
            ans_corrected = {(signA_reg & signB_reg), 15'd0};
        end
    end

    assign ans = ans_corrected;

endmodule
