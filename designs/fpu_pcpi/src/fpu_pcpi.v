module subnormal_fix (
	a,
	adj_a,
	adj_exp_a
);
	reg _sv2v_0;
	input wire [9:0] a;
	output wire [9:0] adj_a;
	output wire [6:0] adj_exp_a;
	reg [3:0] shift_amt;
	reg [6:0] exp_adj;
	always @(*) begin
		if (_sv2v_0)
			;
		casez (a)
			10'b1zzzzzzzzz: begin
				shift_amt = 4'd1;
				exp_adj = 7'd0;
			end
			10'b01zzzzzzzz: begin
				shift_amt = 4'd2;
				exp_adj = 7'd1;
			end
			10'b001zzzzzzz: begin
				shift_amt = 4'd3;
				exp_adj = 7'd2;
			end
			10'b0001zzzzzz: begin
				shift_amt = 4'd4;
				exp_adj = 7'd3;
			end
			10'b00001zzzzz: begin
				shift_amt = 4'd5;
				exp_adj = 7'd4;
			end
			10'b000001zzzz: begin
				shift_amt = 4'd6;
				exp_adj = 7'd5;
			end
			10'b0000001zzz: begin
				shift_amt = 4'd7;
				exp_adj = 7'd6;
			end
			10'b00000001zz: begin
				shift_amt = 4'd8;
				exp_adj = 7'd7;
			end
			10'b000000001z: begin
				shift_amt = 4'd9;
				exp_adj = 7'd8;
			end
			10'b0000000001: begin
				shift_amt = 4'd10;
				exp_adj = 7'd9;
			end
			default: begin
				shift_amt = 4'd11;
				exp_adj = 7'd10;
			end
		endcase
	end
	assign adj_a = a << shift_amt;
	assign adj_exp_a = 7'd0 - exp_adj;
	initial _sv2v_0 = 0;
endmodule
module reciprocal_rom (
	addr,
	data_out
);
	reg _sv2v_0;
	input wire [9:0] addr;
	output reg [13:0] data_out;
	reg [13:0] rom_memory [0:1023];
	initial $readmemb("/home/srivathsann/Documents/uni/DECA/FPU_project/src/reciprocal_rom.mem", rom_memory);
	always @(*) begin
		if (_sv2v_0)
			;
		data_out = rom_memory[addr];
	end
	initial _sv2v_0 = 0;
endmodule
module QSL (
	P_trunc,
	divs,
	Muxout,
	out
);
	reg _sv2v_0;
	input wire signed [7:0] P_trunc;
	input wire [10:0] divs;
	output reg signed [15:0] Muxout;
	output reg signed [2:0] out;
	wire signed [15:0] D = $signed({5'b00000, divs}) <<< 2;
	reg signed [7:0] t_p2;
	reg signed [7:0] t_p1;
	reg signed [7:0] t_m1;
	reg signed [7:0] t_m2;
	always @(*) begin
		if (_sv2v_0)
			;
		case (divs[9:6])
			4'b0000: {t_p2, t_p1, t_m1, t_m2} = {16'h1a0a, -8'sd6, -8'sd23};
			4'b0001: {t_p2, t_p1, t_m1, t_m2} = {16'h1c0b, -8'sd6, -8'sd24};
			4'b0010: {t_p2, t_p1, t_m1, t_m2} = {16'h1e0c, -8'sd7, -8'sd26};
			4'b0011: {t_p2, t_p1, t_m1, t_m2} = {16'h1f0c, -8'sd7, -8'sd27};
			4'b0100: {t_p2, t_p1, t_m1, t_m2} = {16'h210d, -8'sd7, -8'sd28};
			4'b0101: {t_p2, t_p1, t_m1, t_m2} = {16'h230e, -8'sd8, -8'sd30};
			4'b0110: {t_p2, t_p1, t_m1, t_m2} = {16'h240e, -8'sd8, -8'sd31};
			4'b0111: {t_p2, t_p1, t_m1, t_m2} = {16'h260f, -8'sd8, -8'sd32};
			4'b1000: {t_p2, t_p1, t_m1, t_m2} = {16'h2810, -8'sd9, -8'sd34};
			4'b1001: {t_p2, t_p1, t_m1, t_m2} = {16'h2910, -8'sd9, -8'sd35};
			4'b1010: {t_p2, t_p1, t_m1, t_m2} = {16'h2b11, -8'sd9, -8'sd36};
			4'b1011: {t_p2, t_p1, t_m1, t_m2} = {16'h2d12, -8'sd10, -8'sd38};
			4'b1100: {t_p2, t_p1, t_m1, t_m2} = {16'h2e12, -8'sd10, -8'sd39};
			4'b1101: {t_p2, t_p1, t_m1, t_m2} = {16'h3013, -8'sd10, -8'sd40};
			4'b1110: {t_p2, t_p1, t_m1, t_m2} = {16'h3214, -8'sd11, -8'sd42};
			default: {t_p2, t_p1, t_m1, t_m2} = {16'h3314, -8'sd11, -8'sd43};
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if (P_trunc >= t_p2) begin
			out = 3'sd2;
			Muxout = D <<< 1;
		end
		else if (P_trunc >= t_p1) begin
			out = 3'sd1;
			Muxout = D;
		end
		else if (P_trunc >= t_m1) begin
			out = 3'sd0;
			Muxout = 16'sd0;
		end
		else if (P_trunc >= t_m2) begin
			out = -3'sd1;
			Muxout = -D;
		end
		else begin
			out = -3'sd2;
			Muxout = -(D <<< 1);
		end
	end
	initial _sv2v_0 = 0;
endmodule
module srt (
	manA,
	manB,
	clk,
	start,
	Quotient,
	sticky,
	done
);
	input wire [10:0] manA;
	input wire [10:0] manB;
	input wire clk;
	input wire start;
	output reg [13:0] Quotient;
	output reg sticky;
	output reg done;
	reg signed [15:0] R;
	reg [15:0] Q;
	reg [15:0] QM;
	reg [3:0] counter;
	reg active;
	wire signed [15:0] shifted = R <<< 2;
	wire signed [7:0] P_trunc = shifted[15:8];
	wire signed [15:0] Muxout;
	wire signed [2:0] q;
	QSL selector(
		.P_trunc(P_trunc),
		.divs(manB),
		.Muxout(Muxout),
		.out(q)
	);
	wire signed [15:0] next_rem = shifted - Muxout;
	function automatic [13:0] sv2v_cast_14;
		input reg [13:0] inp;
		sv2v_cast_14 = inp;
	endfunction
	always @(posedge clk)
		if (start) begin
			active <= 1'b1;
			counter <= 4'd0;
			done <= 1'b0;
		end
		else if (active) begin
			if (counter == 4'd0) begin
				R <= {5'b00000, manA};
				Q <= 16'd0;
				QM <= 16'd0;
			end
			else if (counter <= 4'd8) begin
				R <= next_rem;
				case (q)
					3'sd2: begin
						Q <= {Q[13:0], 2'b10};
						QM <= {Q[13:0], 2'b01};
					end
					3'sd1: begin
						Q <= {Q[13:0], 2'b01};
						QM <= {Q[13:0], 2'b00};
					end
					3'sd0: begin
						Q <= {Q[13:0], 2'b00};
						QM <= {QM[13:0], 2'b11};
					end
					-3'sd1: begin
						Q <= {QM[13:0], 2'b11};
						QM <= {QM[13:0], 2'b10};
					end
					-3'sd2: begin
						Q <= {QM[13:0], 2'b10};
						QM <= {QM[13:0], 2'b01};
					end
					default:
						;
				endcase
			end
			if (counter == 4'd9) begin
				Quotient <= (R < 0 ? sv2v_cast_14(QM >> 1) : sv2v_cast_14(Q >> 1));
				sticky <= R != 16'd0;
				done <= 1'b1;
			end
			if (counter == 4'd10) begin
				done <= 1'b0;
				active <= 1'b0;
			end
			counter <= counter + 1'b1;
		end
endmodule
module FMUL (
	a,
	b,
	clk,
	nanA,
	nanB,
	infinA,
	infinB,
	A0,
	B0,
	ans
);
	reg _sv2v_0;
	input wire [15:0] a;
	input wire [15:0] b;
	input wire clk;
	input wire nanA;
	input wire nanB;
	input wire infinA;
	input wire infinB;
	input wire A0;
	input wire B0;
	output wire [15:0] ans;
	wire expA_zero = a[14:10] == 5'd0;
	wire expB_zero = b[14:10] == 5'd0;
	wire manA_zero = a[9:0] == 10'd0;
	wire manB_zero = b[9:0] == 10'd0;
	wire sign_bit = a[15] ^ b[15];
	reg [1:0] special_type;
	wire special = ((((nanA | nanB) | infinA) | infinB) | A0) | B0;
	always @(*) begin
		if (_sv2v_0)
			;
		if (((nanA || nanB) || (A0 && infinB)) || (infinA && B0))
			special_type = 2'b10;
		else if (infinA || infinB)
			special_type = 2'b01;
		else if (!special)
			special_type = 2'b00;
		else
			special_type = 2'b00;
	end
	wire [9:0] sub_man_a;
	wire [9:0] sub_man_b;
	wire [6:0] sub_exp_a;
	wire [6:0] sub_exp_b;
	wire subA = expA_zero & ~manA_zero;
	wire subB = expB_zero & ~manB_zero;
	subnormal_fix sub_a(
		.a(a[9:0]),
		.adj_a(sub_man_a),
		.adj_exp_a(sub_exp_a)
	);
	subnormal_fix sub_b(
		.a(b[9:0]),
		.adj_a(sub_man_b),
		.adj_exp_a(sub_exp_b)
	);
	wire [6:0] expA = (subA ? sub_exp_a : {2'b00, a[14:10]});
	wire [6:0] expB = (subB ? sub_exp_b : {2'b00, b[14:10]});
	wire [10:0] manA = (subA ? {1'b1, sub_man_a} : {1'b1, a[9:0]});
	wire [10:0] manB = (subB ? {1'b1, sub_man_b} : {1'b1, b[9:0]});
	wire [15:0] ans_corrected_0;
	reg sign_bit_reg;
	assign ans_corrected_0[15] = sign_bit_reg;
	reg [6:0] base_exp;
	reg [21:0] prod;
	always @(posedge clk) begin
		base_exp <= (expA + expB) - 7'd15;
		prod <= manA * manB;
	end
	wire [6:0] exp_passed = (prod[21] ? base_exp + 7'd1 : base_exp);
	wire [21:0] mantissa_adj;
	assign mantissa_adj = (prod[21] ? prod : {prod[20:0], 1'b0});
	wire is_pre_round_subnormal = exp_passed[6] | (exp_passed == 7'd0);
	wire [6:0] denorm_shift_amt;
	assign denorm_shift_amt = 7'd1 - exp_passed;
	wire [6:0] safe_shift = (denorm_shift_amt >= 7'd22 ? 7'd22 : denorm_shift_amt);
	wire [21:0] denorm_shifted_man = mantissa_adj >> (is_pre_round_subnormal ? safe_shift : 7'd0);
	wire [6:0] mask_shift = (is_pre_round_subnormal ? safe_shift : 7'd0);
	wire [22:0] shift_mask_full;
	assign shift_mask_full = ~(23'h7fffff << mask_shift);
	wire dropped_sticky = |(mantissa_adj & shift_mask_full[21:0]);
	wire [21:0] pre_round_man = denorm_shifted_man | {21'd0, dropped_sticky};
	wire [6:0] pre_round_exp = (is_pre_round_subnormal ? 7'd0 : exp_passed);
	wire G;
	wire R;
	wire S;
	assign G = pre_round_man[10];
	assign R = pre_round_man[9];
	assign S = |pre_round_man[8:0];
	wire [10:0] rounded_man = {1'b0, pre_round_man[20:11]} + 11'd1;
	wire [6:0] rounded_exp = pre_round_exp + 7'd1;
	wire round_up = G & ((R | S) | pre_round_man[11]);
	wire [9:0] right_mantissa = (round_up ? (rounded_man[10] ? 10'd0 : rounded_man[9:0]) : pre_round_man[20:11]);
	wire [6:0] final_exp = (round_up && rounded_man[10] ? rounded_exp : pre_round_exp);
	wire overflow = ~final_exp[6] & (final_exp[5] | &final_exp[4:0]);
	assign ans_corrected_0[14:10] = (overflow ? 5'b11111 : final_exp[4:0]);
	assign ans_corrected_0[9:0] = (overflow ? 10'd0 : right_mantissa);
	reg [1:0] special_type_reg;
	reg special_reg;
	always @(posedge clk) begin
		special_type_reg <= special_type;
		special_reg <= special;
		sign_bit_reg <= sign_bit;
	end
	reg [15:0] ans_special_out;
	always @(*) begin
		if (_sv2v_0)
			;
		ans_special_out[15] = sign_bit_reg;
		case (special_type_reg)
			2'b10: ans_special_out[14:0] = 15'b111111000000000;
			2'b01: ans_special_out[14:0] = 15'b111110000000000;
			default: ans_special_out[14:0] = 15'b000000000000000;
		endcase
	end
	assign ans = (special_reg ? ans_special_out : ans_corrected_0);
	initial _sv2v_0 = 0;
endmodule
module addsub (
	a,
	b,
	clk,
	sub,
	nanA,
	nanB,
	infinA,
	infinB,
	A0,
	B0,
	ans
);
	reg _sv2v_0;
	input wire [15:0] a;
	input wire [15:0] b;
	input wire clk;
	input wire sub;
	input wire nanA;
	input wire nanB;
	input wire infinA;
	input wire infinB;
	input wire A0;
	input wire B0;
	output wire [15:0] ans;
	wire expA_zero = a[14:10] == 5'd0;
	wire expB_zero = b[14:10] == 5'd0;
	wire manA_zero = a[9:0] == 10'd0;
	wire manB_zero = b[9:0] == 10'd0;
	wire signA = a[15];
	wire signB = (sub ? ~b[15] : b[15]);
	wire [6:0] init_expA = {2'b00, a[14:10]};
	wire [6:0] init_expB = {2'b00, b[14:10]};
	wire [21:0] init_manA = {~expA_zero, a[9:0], {11 {1'b0}}};
	wire [21:0] init_manB = {~expB_zero, b[9:0], {11 {1'b0}}};
	wire [6:0] corrected_expA = (expA_zero ? 7'b1110010 : init_expA - 7'd15);
	wire [6:0] corrected_expB = (expB_zero ? 7'b1110010 : init_expB - 7'd15);
	wire [6:0] difference = corrected_expA - corrected_expB;
	wire A_is_smaller = difference[6];
	wire [21:0] man_bigger = (A_is_smaller ? init_manB : init_manA);
	wire [21:0] man_smaller = (A_is_smaller ? init_manA : init_manB);
	wire [6:0] abs_diff = (A_is_smaller ? -difference : difference);
	reg [21:0] aligned_smaller;
	reg align_sticky;
	always @(*) begin
		if (_sv2v_0)
			;
		if (abs_diff > 7'd13) begin
			aligned_smaller = man_smaller >> 7'd13;
			align_sticky = |(man_smaller & ~(22'h3fffff << 7'd13));
		end
		else begin
			aligned_smaller = man_smaller >> abs_diff;
			align_sticky = |(man_smaller & ~(22'h3fffff << abs_diff));
		end
	end
	wire final_sign = (A_is_smaller ? signB : signA);
	wire subtract = signA ^ signB;
	wire [13:0] alu_bigger = {man_bigger[21:9], 1'b0};
	wire [13:0] alu_smaller = {aligned_smaller[21:9], align_sticky | (|aligned_smaller[8:0])};
	wire [14:0] addsub_man_norm = (subtract ? {1'b0, alu_bigger} - {1'b0, alu_smaller} : {1'b0, alu_bigger} + {1'b0, alu_smaller});
	wire sign_temp = (subtract & addsub_man_norm[14] ? ~final_sign : final_sign);
	wire [14:0] addsub_man = (subtract & addsub_man_norm[14] ? -addsub_man_norm : addsub_man_norm);
	wire sign = ((addsub_man == 15'd0) && subtract ? 1'b0 : sign_temp);
	reg [21:0] pre_norm_man;
	reg [21:0] man_fixing;
	reg [6:0] exp_fixing;
	reg subtract_reg;
	reg sign_reg;
	reg [6:0] final_exp_reg;
	reg nanA_reg;
	reg nanB_reg;
	reg infinA_reg;
	reg infinB_reg;
	reg A0_reg;
	reg B0_reg;
	always @(posedge clk) begin
		subtract_reg <= subtract;
		sign_reg <= sign;
		final_exp_reg <= (A_is_smaller ? corrected_expB : corrected_expA);
		pre_norm_man <= {addsub_man, 7'b0000000};
		nanA_reg <= nanA;
		nanB_reg <= nanB;
		infinA_reg <= infinA;
		infinB_reg <= infinB;
		A0_reg <= A0;
		B0_reg <= B0;
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if (pre_norm_man == 22'd0) begin
			exp_fixing = -7'sd15;
			man_fixing = 22'd0;
		end
		else
			casez (pre_norm_man)
				22'b1zzzzzzzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg + 1;
					man_fixing = pre_norm_man >> 1;
					man_fixing[0] = pre_norm_man[0];
				end
				22'b01zzzzzzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg;
					man_fixing = pre_norm_man;
				end
				22'b001zzzzzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 1;
					man_fixing = pre_norm_man << 1;
				end
				22'b0001zzzzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 2;
					man_fixing = pre_norm_man << 2;
				end
				22'b00001zzzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 3;
					man_fixing = pre_norm_man << 3;
				end
				22'b000001zzzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 4;
					man_fixing = pre_norm_man << 4;
				end
				22'b0000001zzzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 5;
					man_fixing = pre_norm_man << 5;
				end
				22'b00000001zzzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 6;
					man_fixing = pre_norm_man << 6;
				end
				22'b000000001zzzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 7;
					man_fixing = pre_norm_man << 7;
				end
				22'b0000000001zzzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 8;
					man_fixing = pre_norm_man << 8;
				end
				22'b00000000001zzzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 9;
					man_fixing = pre_norm_man << 9;
				end
				22'b000000000001zzzzzzzzzz: begin
					exp_fixing = final_exp_reg - 10;
					man_fixing = pre_norm_man << 10;
				end
				22'b0000000000001zzzzzzzzz: begin
					exp_fixing = final_exp_reg - 11;
					man_fixing = pre_norm_man << 11;
				end
				22'b00000000000001zzzzzzzz: begin
					exp_fixing = final_exp_reg - 12;
					man_fixing = pre_norm_man << 12;
				end
				22'b000000000000001zzzzzzz: begin
					exp_fixing = final_exp_reg - 13;
					man_fixing = pre_norm_man << 13;
				end
				22'b0000000000000001zzzzzz: begin
					exp_fixing = final_exp_reg - 14;
					man_fixing = pre_norm_man << 14;
				end
				22'b00000000000000001zzzzz: begin
					exp_fixing = final_exp_reg - 15;
					man_fixing = pre_norm_man << 15;
				end
				default: begin
					exp_fixing = final_exp_reg;
					man_fixing = pre_norm_man;
				end
			endcase
	end
	reg [21:0] sub_man;
	reg [6:0] sub_exp;
	reg [6:0] shift_amt_signed;
	always @(*) begin
		if (_sv2v_0)
			;
		if ($signed(exp_fixing) < -7'sd14) begin
			shift_amt_signed = -(7'sd14 + $signed(exp_fixing));
			sub_exp = -7'sd14;
			if (shift_amt_signed > 7'd12) begin
				sub_man = man_fixing >> 7'd12;
				sub_man[9] = 1'b0;
			end
			else
				sub_man = (man_fixing >> shift_amt_signed) | {21'd0, |(man_fixing << (7'd21 - shift_amt_signed))};
		end
		else begin
			sub_man = man_fixing;
			sub_exp = exp_fixing;
			shift_amt_signed = 0;
		end
	end
	wire G_r = sub_man[9];
	wire R_r = sub_man[8];
	wire S_r = |sub_man[7:0];
	wire LSB_r = sub_man[10];
	wire round_up = G_r & ((R_r | S_r) | LSB_r);
	wire [11:0] rounded_fraction = sub_man[20:10] + 1'b1;
	reg [9:0] man;
	reg [6:0] exp;
	reg final_hidden_bit;
	always @(*) begin
		if (_sv2v_0)
			;
		if (round_up) begin
			if (rounded_fraction[11]) begin
				man = rounded_fraction[10:1];
				final_hidden_bit = rounded_fraction[11];
				exp = sub_exp + 1'b1;
			end
			else begin
				man = rounded_fraction[9:0];
				final_hidden_bit = rounded_fraction[10];
				exp = sub_exp;
			end
		end
		else begin
			man = sub_man[19:10];
			final_hidden_bit = sub_man[20];
			exp = sub_exp;
		end
	end
	wire overflow = ~exp[6] & (exp[5] | exp[4]);
	reg [4:0] exp_packed;
	always @(*) begin
		if (_sv2v_0)
			;
		if ($signed(exp) <= -7'sd15)
			exp_packed = 5'd0;
		else if (($signed(exp) == -7'sd14) && (final_hidden_bit == 1'b0))
			exp_packed = 5'd0;
		else
			exp_packed = exp[4:0] + 5'd15;
	end
	wire [15:0] ans_calculated;
	reg [15:0] ans_corrected;
	assign ans_calculated[15] = sign_reg;
	assign ans_calculated[14:10] = (overflow ? 5'b11111 : exp_packed);
	assign ans_calculated[9:0] = (overflow ? 10'd0 : man);
	reg signA_reg;
	reg signB_reg;
	always @(posedge clk) begin
		signA_reg <= signA;
		signB_reg <= signB;
	end
	always @(*) begin
		if (_sv2v_0)
			;
		ans_corrected = ans_calculated;
		if (nanA_reg || nanB_reg)
			ans_corrected = 16'b0111111000000000;
		else if ((infinA_reg && infinB_reg) && subtract_reg)
			ans_corrected = 16'b0111111000000000;
		else if (infinA_reg)
			ans_corrected = {signA_reg, 15'h7c00};
		else if (infinB_reg)
			ans_corrected = {signB_reg, 15'h7c00};
		else if (A0_reg && B0_reg)
			ans_corrected = {signA_reg & signB_reg, 15'd0};
	end
	assign ans = ans_corrected;
	initial _sv2v_0 = 0;
endmodule
module DIV (
	a,
	b,
	clk,
	start,
	nanA,
	nanB,
	infinA,
	infinB,
	A0,
	B0,
	out,
	done
);
	input wire [15:0] a;
	input wire [15:0] b;
	input wire clk;
	input wire start;
	input wire nanA;
	input wire nanB;
	input wire infinA;
	input wire infinB;
	input wire A0;
	input wire B0;
	output wire [15:0] out;
	output wire done;
	wire expA_zero = a[14:10] == 5'd0;
	wire expB_zero = b[14:10] == 5'd0;
	wire manA_zero = a[9:0] == 10'd0;
	wire manB_zero = b[9:0] == 10'd0;
	wire signA = a[15];
	wire signB = b[15];
	wire final_sign = signA ^ signB;
	wire subA;
	wire subB;
	assign subA = expA_zero & ~manA_zero;
	assign subB = expB_zero & ~manB_zero;
	wire [10:0] sub_manA;
	wire [10:0] sub_manB;
	wire [6:0] sub_expA;
	wire [6:0] sub_expB;
	wire [9:0] adj_manA;
	wire [9:0] adj_manB;
	subnormal_fix fixA(
		.a(a[9:0]),
		.adj_a(adj_manA),
		.adj_exp_a(sub_expA)
	);
	subnormal_fix fixB(
		.a(b[9:0]),
		.adj_a(adj_manB),
		.adj_exp_a(sub_expB)
	);
	assign sub_manA = {1'b1, adj_manA};
	assign sub_manB = {1'b1, adj_manB};
	wire [6:0] final_expA;
	wire [6:0] final_expB;
	wire [10:0] final_manA;
	wire [10:0] final_manB;
	assign final_expA = (subA ? sub_expA : {2'b00, a[14:10]});
	assign final_manA = (subA ? sub_manA : {1'b1, a[9:0]});
	assign final_expB = (subB ? sub_expB : {2'b00, b[14:10]});
	assign final_manB = (subB ? sub_manB : {1'b1, b[9:0]});
	wire [6:0] tentative_exp = (final_expA - final_expB) + 7'd15;
	wire [13:0] q_srt;
	wire srt_sticky;
	wire srt_done;
	reg [10:0] final_manA_reg;
	reg [10:0] final_manB_reg;
	srt srt_unit(
		.manA(final_manA_reg),
		.manB(final_manB_reg),
		.clk(clk),
		.start(start),
		.Quotient(q_srt),
		.sticky(srt_sticky),
		.done(srt_done)
	);
	reg [6:0] tentative_exp_pending;
	reg final_sign_pending;
	reg [15:0] special_ans_pending;
	reg special_flag_pending;
	reg [6:0] tentative_exp_reg;
	reg [13:0] q_reg;
	reg sticky_reg;
	reg final_sign_reg;
	reg [15:0] special_ans_reg;
	reg special_flag_reg;
	always @(posedge clk) begin
		if (start) begin
			final_manA_reg <= final_manA;
			final_manB_reg <= final_manB;
			tentative_exp_pending <= tentative_exp;
			final_sign_pending <= final_sign;
			special_flag_pending <= ((((nanA | nanB) | A0) | B0) | infinA) | infinB;
			if (((nanA | nanB) | (A0 & B0)) | (infinA & infinB))
				special_ans_pending <= {final_sign, 15'b111111000000000};
			else if (infinA | B0)
				special_ans_pending <= {final_sign, 15'b111110000000000};
			else
				special_ans_pending <= {final_sign, 15'd0};
		end
		if (srt_done) begin
			q_reg <= q_srt;
			sticky_reg <= srt_sticky;
			tentative_exp_reg <= tentative_exp_pending;
			final_sign_reg <= final_sign_pending;
			special_ans_reg <= special_ans_pending;
			special_flag_reg <= special_flag_pending;
		end
	end
	wire [24:0] prod = (sticky_reg ? {1'b0, q_reg, 10'd0} | 25'd1 : {1'b0, q_reg, 10'd0});
	wire [6:0] adjusted_exp = tentative_exp_reg - 7'd1;
	wire [6:0] normalised_exp = (prod[23] ? tentative_exp_reg : adjusted_exp);
	wire [24:0] normalised_prod = (prod[23] ? prod : prod << 1);
	wire underflow = normalised_exp[6] | ~|normalised_exp;
	wire [6:0] underflow_amt = (normalised_exp[6] ? -normalised_exp : normalised_exp);
	wire [24:0] underflow_mask = ~(25'h1ffffff << (underflow_amt + 1));
	wire raw_tentative_S = |(normalised_prod & underflow_mask);
	wire tentative_S = (underflow ? raw_tentative_S : 1'b0);
	wire [24:0] underflow_prod = normalised_prod >> (underflow_amt + 1);
	wire [6:0] underflow_exp = (underflow ? 7'd0 : normalised_exp);
	wire [24:0] underflow_man = (underflow ? underflow_prod : normalised_prod);
	wire G;
	wire R;
	wire S;
	wire round;
	wire [9:0] ans_man_0;
	wire [9:0] ans_man_1;
	wire [6:0] ans_exp_0;
	assign ans_man_0 = underflow_man[22:13];
	assign G = underflow_man[12];
	assign R = underflow_man[11];
	assign S = |underflow_man[10:0];
	assign round = G & (((R | S) | tentative_S) | ans_man_0[0]);
	wire [10:0] rounding_full = ans_man_0 + {9'd0, round};
	wire [9:0] rounded = rounding_full[9:0];
	wire rounding_carry = rounding_full[10];
	assign ans_man_1 = (rounding_carry ? 10'd0 : rounded);
	assign ans_exp_0 = (rounding_carry ? underflow_exp + 1 : underflow_exp);
	wire [15:0] normal_ans;
	assign normal_ans[15] = final_sign_reg;
	assign normal_ans[14:10] = (ans_exp_0 > 7'd30 ? 5'b11111 : ans_exp_0[4:0]);
	assign normal_ans[9:0] = (ans_exp_0 > 7'd30 ? 10'd0 : ans_man_1);
	assign out = (special_flag_reg ? special_ans_reg : normal_ans);
	assign done = srt_done;
endmodule
module fpu_test (
	a,
	b,
	op,
	op_q,
	clk,
	start,
	ans,
	done
);
	input wire [15:0] a;
	input wire [15:0] b;
	input wire [1:0] op;
	input wire [1:0] op_q;
	input wire clk;
	input wire start;
	output wire [15:0] ans;
	output wire done;
	wire expA_zero = a[14:10] == 5'd0;
	wire expB_zero = b[14:10] == 5'd0;
	wire expA_max = a[14:10] == 5'b11111;
	wire expB_max = b[14:10] == 5'b11111;
	wire manA_zero = a[9:0] == 10'd0;
	wire manB_zero = b[9:0] == 10'd0;
	wire nanA = expA_max & ~manA_zero;
	wire nanB = expB_max & ~manB_zero;
	wire infinA = expA_max & manA_zero;
	wire infinB = expB_max & manB_zero;
	wire A0 = expA_zero & manA_zero;
	wire B0 = expB_zero & manB_zero;
	wire [15:0] FADDSUB_out;
	wire [15:0] FMUL_out;
	wire [15:0] FDIV_out;
	addsub adder(
		.a(a),
		.b(b),
		.clk(clk),
		.sub(op[0]),
		.nanA(nanA),
		.nanB(nanB),
		.infinA(infinA),
		.infinB(infinB),
		.A0(A0),
		.B0(B0),
		.ans(FADDSUB_out)
	);
	FMUL multiplier(
		.a(a),
		.b(b),
		.clk(clk),
		.nanA(nanA),
		.nanB(nanB),
		.infinA(infinA),
		.infinB(infinB),
		.A0(A0),
		.B0(B0),
		.ans(FMUL_out)
	);
	DIV divider(
		.a(a),
		.b(b),
		.clk(clk),
		.start(start),
		.nanA(nanA),
		.nanB(nanB),
		.infinA(infinA),
		.infinB(infinB),
		.A0(A0),
		.B0(B0),
		.out(FDIV_out),
		.done(done)
	);
	assign ans = (op_q[1] ? (op_q[0] ? FDIV_out : FMUL_out) : FADDSUB_out);
endmodule
module fpu_pcpi (
	clk,
	resetn,
	pcpi_valid,
	pcpi_insn,
	pcpi_rs1,
	pcpi_rs2,
	pcpi_wr,
	pcpi_wait,
	pcpi_ready,
	pcpi_rd
);
	input wire clk;
	input wire resetn;
	input wire pcpi_valid;
	input wire [31:0] pcpi_insn;
	input wire [31:0] pcpi_rs1;
	input wire [31:0] pcpi_rs2;
	output wire pcpi_wr;
	output wire pcpi_wait;
	output wire pcpi_ready;
	output wire [31:0] pcpi_rd;
	reg busy;
	reg done_q;
	reg [1:0] cyc;
	reg div_stage;
	reg [1:0] fpu_op_q;
	wire div_done;
	wire is_custom0 = pcpi_insn[6:0] == 7'b0001011;
	wire is_fpu_f3 = pcpi_insn[14:12] == 3'b000;
	wire instr_fdiv = (is_custom0 && is_fpu_f3) && (pcpi_insn[31:25] == 7'b0001001);
	wire instr_fmul = (is_custom0 && is_fpu_f3) && (pcpi_insn[31:25] == 7'b0001000);
	wire instr_fsub = (is_custom0 && is_fpu_f3) && (pcpi_insn[31:25] == 7'b0000111);
	wire is_std_f3 = (pcpi_insn[14:12] == 3'b000) | (pcpi_insn[14:12] == 3'b111);
	wire is_std_fp = pcpi_insn[6:0] == 7'b1010011;
	wire instr_std_fdiv = (is_std_fp && is_std_f3) && (pcpi_insn[31:25] == 7'b0001110);
	wire instr_std_fmul = (is_std_fp && is_std_f3) && (pcpi_insn[31:25] == 7'b0001010);
	wire instr_std_fsub = (is_std_fp && is_std_f3) && (pcpi_insn[31:25] == 7'b0000110);
	wire [1:0] fpu_op = (instr_fdiv | instr_std_fdiv ? 2'b11 : (instr_fmul | instr_std_fmul ? 2'b10 : (instr_fsub | instr_std_fsub ? 2'b01 : 2'b00)));
	wire is_fdiv = fpu_op == 2'b11;
	wire answer_valid = (is_fdiv ? div_stage : cyc == 2'd0);
	wire instr_fadd = (is_custom0 && is_fpu_f3) && (pcpi_insn[31:25] == 7'b0000110);
	wire instr_std_fadd = (is_std_fp && is_std_f3) && (pcpi_insn[31:25] == 7'b0000010);
	wire claimed = ((((((instr_fadd | instr_fsub) | instr_fmul) | instr_fdiv) | instr_std_fadd) | instr_std_fsub) | instr_std_fmul) | instr_std_fdiv;
	wire start_compute = pcpi_valid & claimed;
	wire fpu_start = ((is_fdiv & start_compute) & !busy) & !done_q;
	assign pcpi_wait = busy | start_compute;
	assign pcpi_wr = busy & answer_valid;
	assign pcpi_ready = busy & answer_valid;
	always @(posedge clk)
		if (!resetn) begin
			busy <= 1'b0;
			done_q <= 1'b0;
			cyc <= 2'd0;
			div_stage <= 1'b0;
			fpu_op_q <= 2'b00;
		end
		else if ((start_compute && !busy) && !done_q) begin
			busy <= 1'b1;
			cyc <= 2'd0;
			div_stage <= 1'b0;
			fpu_op_q <= fpu_op;
		end
		else if (busy) begin
			if (is_fdiv) begin
				if (div_done)
					div_stage <= 1'b1;
				if (div_stage) begin
					busy <= 1'b0;
					done_q <= 1'b1;
				end
			end
			else begin
				cyc <= cyc + 2'd1;
				if (answer_valid) begin
					busy <= 1'b0;
					done_q <= 1'b1;
				end
			end
		end
		else if (!pcpi_valid)
			done_q <= 1'b0;
	fpu_test u_fpu_test(
		.a(pcpi_rs1[15:0]),
		.b(pcpi_rs2[15:0]),
		.op(fpu_op),
		.op_q(fpu_op_q),
		.clk(clk),
		.start(fpu_start),
		.ans(pcpi_rd[15:0]),
		.done(div_done)
	);
	assign pcpi_rd[31:16] = 16'd0;
endmodule