// Sub-modules for DIV datapath PPA profiling
/* verilator lint_off DECLFILENAME */

// Radix-4 SRT quotient selection logic with parallel threshold evaluation.
module QSL (
    input  logic signed [7:0]  P_trunc,  // 8-bit signed
    input  logic [10:0]        divs,     // divisor (manB), index = divs[9:6]
    output logic signed [15:0] Muxout,   // q * D
    output logic signed [2:0]  out
);
    wire signed [15:0] D = $signed({5'b0, divs}) <<< 2;

    // Parallel threshold lookup table
    logic signed [7:0] t_p2, t_p1, t_m1, t_m2;

    always_comb begin
        case (divs[9:6])
            4'b0000: {t_p2, t_p1, t_m1, t_m2} = {8'sd26, 8'sd10, -8'sd6,  -8'sd23};
            4'b0001: {t_p2, t_p1, t_m1, t_m2} = {8'sd28, 8'sd11, -8'sd6,  -8'sd24};
            4'b0010: {t_p2, t_p1, t_m1, t_m2} = {8'sd30, 8'sd12, -8'sd7,  -8'sd26};
            4'b0011: {t_p2, t_p1, t_m1, t_m2} = {8'sd31, 8'sd12, -8'sd7,  -8'sd27};
            4'b0100: {t_p2, t_p1, t_m1, t_m2} = {8'sd33, 8'sd13, -8'sd7,  -8'sd28};
            4'b0101: {t_p2, t_p1, t_m1, t_m2} = {8'sd35, 8'sd14, -8'sd8,  -8'sd30};
            4'b0110: {t_p2, t_p1, t_m1, t_m2} = {8'sd36, 8'sd14, -8'sd8,  -8'sd31};
            4'b0111: {t_p2, t_p1, t_m1, t_m2} = {8'sd38, 8'sd15, -8'sd8,  -8'sd32};
            4'b1000: {t_p2, t_p1, t_m1, t_m2} = {8'sd40, 8'sd16, -8'sd9,  -8'sd34};
            4'b1001: {t_p2, t_p1, t_m1, t_m2} = {8'sd41, 8'sd16, -8'sd9,  -8'sd35};
            4'b1010: {t_p2, t_p1, t_m1, t_m2} = {8'sd43, 8'sd17, -8'sd9,  -8'sd36};
            4'b1011: {t_p2, t_p1, t_m1, t_m2} = {8'sd45, 8'sd18, -8'sd10, -8'sd38};
            4'b1100: {t_p2, t_p1, t_m1, t_m2} = {8'sd46, 8'sd18, -8'sd10, -8'sd39};
            4'b1101: {t_p2, t_p1, t_m1, t_m2} = {8'sd48, 8'sd19, -8'sd10, -8'sd40};
            4'b1110: {t_p2, t_p1, t_m1, t_m2} = {8'sd50, 8'sd20, -8'sd11, -8'sd42};
            default: {t_p2, t_p1, t_m1, t_m2} = {8'sd51, 8'sd20, -8'sd11, -8'sd43};
        endcase
    end

    // Parallel comparison structure to eliminate cascaded priority multiplexers
    always_comb begin
        if      (P_trunc >= t_p2) begin out =  3'sd2;  Muxout =  (D <<< 1); end
        else if (P_trunc >= t_p1) begin out =  3'sd1;  Muxout =  D;         end
        else if (P_trunc >= t_m1) begin out =  3'sd0;  Muxout =  16'sd0;    end
        else if (P_trunc >= t_m2) begin out = -3'sd1;  Muxout = -D;         end
        else                      begin out = -3'sd2;  Muxout = -(D <<< 1); end
    end
endmodule

// Radix-4 SRT division core computing floor(manA * 8192 / manB)
module srt (
    input  logic [10:0] manA, manB,
    input  logic clk,
    input  logic start,   // one-cycle pulse: begin a division from manA/manB
    output logic [13:0] Quotient,
    output logic        sticky,   // 1 iff division inexact at 14 quotient bits
    output logic        done      // pulses one cycle after Quotient is refreshed
);
    logic signed [15:0] R;
    logic [15:0] Q, QM;
    logic [3:0] counter;
    logic active;

    // Shifted remainder (16-bit signed holds |4R| <= 21424 without overflow)
    wire signed [15:0] shifted = R <<< 2;

    // 8-bit arithmetic truncation of 4R (floor toward -inf)
    wire signed [7:0] P_trunc = shifted[15:8];

    wire signed [15:0] Muxout;
    wire signed [2:0]  q;

    QSL selector (
        .P_trunc(P_trunc),
        .divs(manB),
        .Muxout(Muxout),
        .out(q)
    );

    // R_next = 4*R - q*D  (Muxout = q*D)
    wire signed [15:0] next_rem = shifted - Muxout;

    always_ff @(posedge clk) begin
        if (start) begin
            active  <= 1'b1;
            counter <= 4'd0;
            done    <= 1'b0;
        end else if (active) begin
            if (counter == 4'd0) begin
                R  <= {5'b0, manA};
                Q  <= 16'd0;
                QM <= 16'd0;
            end else if (counter <= 4'd8) begin
                R <= next_rem;

                case (q)
                    3'sd2:  begin Q <= {Q[13:0],  2'b10}; QM <= {Q[13:0],  2'b01}; end
                    3'sd1:  begin Q <= {Q[13:0],  2'b01}; QM <= {Q[13:0],  2'b00}; end
                    3'sd0:  begin Q <= {Q[13:0],  2'b00}; QM <= {QM[13:0], 2'b11}; end
                    -3'sd1: begin Q <= {QM[13:0], 2'b11}; QM <= {QM[13:0], 2'b10}; end
                    -3'sd2: begin Q <= {QM[13:0], 2'b10}; QM <= {QM[13:0], 2'b01}; end
                    default:;
                endcase
            end

            if (counter == 4'd9) begin
                Quotient <= (R < 0) ? (14'(QM >> 1)) : (14'(Q >> 1));
                sticky <= (R != 16'd0);
                done   <= 1'b1;
            end

            if (counter == 4'd10) begin
                done   <= 1'b0;
                active <= 1'b0;
            end

            counter <= counter + 1'b1;
        end
    end
endmodule
