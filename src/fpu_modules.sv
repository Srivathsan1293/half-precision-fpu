// src/fpu_modules.sv
/* verilator lint_off DECLFILENAME */

module mux2x1 #(parameter WIDTH = 16)(
    input  logic [WIDTH-1:0] in0,   // Selected when sel == 0
    input  logic [WIDTH-1:0] in1,   // Selected when sel == 1
    input  logic             sel, // Selection control bit
    output logic [WIDTH-1:0] out
);

    always @(*) begin
        out = in0;
        if (sel) out = in1;
    end

endmodule

module mux4x1 #(parameter WIDTH = 16)(
    input logic [WIDTH-1:0] in0,
    input logic [WIDTH-1:0] in1,
    input logic [WIDTH-1:0] in2,
    input logic [WIDTH-1:0] in3,
    input logic [1:0] sel,
    output logic [WIDTH-1:0] out
);
    always @(*) begin
        case (sel)
            2'b00: out = in0;
            2'b01: out = in1;
            2'b10: out = in2;
            2'b11: out = in3;
            default: out = in0;
        endcase
    end

endmodule

module fadd (
    input logic  a, b, cin,
    output logic cout, Sum
);

    assign cout = (a&b) | (cin & (a^b));
    assign Sum = cin ^ a ^ b;

endmodule

module add #(parameter WIDTH = 16)(
    input logic [WIDTH-1:0] a,
    input logic [WIDTH-1:0] b,
    output logic cout,
    output logic [WIDTH-1:0] Sum
);
    wire [WIDTH:0] carry;
    assign carry[0] = 1'b0;
    assign cout = carry[WIDTH];

    fadd adder [WIDTH-1:0] (.a(a), .b(b), .cin(carry[WIDTH-1:0]), .cout(carry[WIDTH:1]), .Sum(Sum));
endmodule

module sub #(parameter WIDTH = 16)(
    input logic [WIDTH-1:0] a,
    input logic [WIDTH-1:0] b,
    output logic cout,
    output logic [WIDTH-1:0] Sum
);
    wire [WIDTH:0] carry;
    assign carry[0] = 1'b1;
    assign cout = carry[WIDTH];

    fadd subtractor [WIDTH-1:0] (.a(a), .b(~b), .cin(carry[WIDTH-1:0]), .cout(carry[WIDTH:1]), .Sum(Sum));
endmodule


module left #(parameter WIDTH = 16)(
    input logic [WIDTH-1:0] value,
    input logic [$clog2(WIDTH)-1:0] shift_amt,
    output logic [WIDTH-1:0] ans
);

    wire [WIDTH-1:0] intermediary [$clog2(WIDTH):0];
    wire [WIDTH-1:0] shifted [$clog2(WIDTH)-1:0];
    assign intermediary[0] = value;

    genvar i;

    generate
        for (i = 0; i < $clog2(WIDTH); i = i + 1) begin: gen_stage
            assign shifted[i] = intermediary[i] << (1 << i);
            mux2x1 #(.WIDTH(WIDTH)) shifter (.in0(intermediary[i]), .in1(shifted[i]), .sel(shift_amt[i]), .out(intermediary[i+1]));
        end
    endgenerate

    assign ans = intermediary[$clog2(WIDTH)];
endmodule


module right #(parameter WIDTH = 16)(
    input logic [WIDTH-1:0] value,
    input logic [$clog2(WIDTH)-1:0] shift_amt,
    output logic [WIDTH-1:0] ans
);

    wire [WIDTH-1:0] intermediary [$clog2(WIDTH):0];
    wire [WIDTH-1:0] shifted [$clog2(WIDTH)-1:0];
    assign intermediary[0] = value;

    genvar i;

    generate
        for (i = 0; i < $clog2(WIDTH); i = i + 1) begin: gen_stage
            assign shifted[i] = intermediary[i] >> (1 << i);
            mux2x1 #(.WIDTH(WIDTH)) shifter (.in0(intermediary[i]), .in1(shifted[i]), .sel(shift_amt[i]), .out(intermediary[i+1]));
        end
    endgenerate

    assign ans = intermediary[$clog2(WIDTH)];
endmodule

module booth_decode (
    input logic [12:0] A,
    input logic [2:0] B,
    output logic [12:0] PP,
    output logic carry
);

    always_comb begin
        case (B)
            3'b000: begin PP = 13'h0000; carry = 1'b0; end
            3'b001: begin PP = A; carry = 1'b0; end
            3'b010: begin PP = A; carry = 1'b0; end
            3'b011: begin PP = A << 1; carry = 1'b0; end
            3'b100: begin PP = ~(A << 1); carry = 1'b1; end
            3'b101: begin PP = ~A; carry = 1'b1; end
            3'b110: begin PP = ~A; carry = 1'b1; end
            3'b111: begin PP = 13'h0000; carry = 1'b0; end
            default: begin PP = 13'h0000;carry = 1'b0; end
        endcase
    end
endmodule


module MUL (
    input  logic [10:0] A, B,
    output logic [21:0] ans
);
    // Setting up variables
    wire [12:0] MUL_A;
    wire [11:-1] MUL_B;

    // Use 1'b0 for strict bit-width padding
    assign MUL_A = {2'b00, A};
    assign MUL_B = {1'b0, B, 1'b0};

    // Radix-4 encoding
    genvar i;
    wire [12:0] partial_products [5:0];

    // Changed to 'logic' so it can be driven by both assign and always_comb
    logic [23:0] completed_partial_products [6:0];
    wire [5:0] carrys;

    generate
        for (i = 0; i < 6; i = i + 1) begin: partials_generation
            booth_decode decoders (
                .A(MUL_A),
                .B({MUL_B[2*i + 1], MUL_B[2*i], MUL_B[2*i - 1]}),
                .PP(partial_products[i]),
                .carry(carrys[i])
            );
            // Removed the rogue '};#' at the end
            assign completed_partial_products[i] = {{11{partial_products[i][12]}}, partial_products[i]} << (2*i);
        end
    endgenerate

    always_comb begin
        // Only zero out the 7th row (index 6)
        completed_partial_products[6] = 24'd0;

        for (int j = 0; j < 6; j=j+1) begin
            // Fixed loop variable: used 'j' instead of 'i'
            completed_partial_products[6][2*j] = carrys[j];
        end
    end

    // Wallace tree addition
    wire [23:0] Sum [5:0];
    wire [23:0] Carrys [4:0];

    // Stage 1: pp0+pp1+pp2, pp3+pp4+pp5
    // Added 'assign' keywords and fixed the missing << 1 on Carrys[0]
    assign Sum[0]    = completed_partial_products[0] ^ completed_partial_products[1] ^ completed_partial_products[2];
    assign Carrys[0] = ((completed_partial_products[0] & completed_partial_products[1]) |
                        (completed_partial_products[1] & completed_partial_products[2]) |
                        (completed_partial_products[0] & completed_partial_products[2])) << 1;

    // Fixed naming: Changed to Carrys[1]
    assign Sum[1]    = completed_partial_products[3] ^ completed_partial_products[4] ^ completed_partial_products[5];
    assign Carrys[1] = ((completed_partial_products[3] & completed_partial_products[4]) |
                        (completed_partial_products[4] & completed_partial_products[5]) |
                        (completed_partial_products[3] & completed_partial_products[5])) << 1;


    // Stage 2: S0 + C0 + S1
    assign Sum[2]    = Sum[0] ^ Sum[1] ^ Carrys[0];
    assign Carrys[2] = ((Sum[0] & Sum[1]) | (Sum[1] & Carrys[0]) | (Sum[0] & Carrys[0])) << 1;

    // Stage 3: S2 + C2 + C1
    assign Sum[3]    = Sum[2] ^ Carrys[2] ^ Carrys[1];
    assign Carrys[3] = ((Sum[2] & Carrys[2]) | (Carrys[2] & Carrys[1]) | (Sum[2] & Carrys[1])) << 1;

    // Stage 4: S3 + C3 + pp6
    assign Sum[4]    = Sum[3] ^ Carrys[3] ^ completed_partial_products[6];
    assign Carrys[4] = ((Sum[3] & Carrys[3]) | (Carrys[3] & completed_partial_products[6]) | (Sum[3] & completed_partial_products[6])) << 1;

    // Final answer
    assign Sum[5] = Sum[4] + Carrys[4];
    assign ans    = Sum[5][21:0];

endmodule

module subnormal_fix (
    input logic [9:0] a,
    output logic [9:0] adj_a,
    output logic [6:0] adj_exp_a
);
    logic [3:0] adjustment;
    always_comb begin
        casez (a[9:0])
            10'b1zzzzzzzzz: adjustment = 4'd0;
            10'b01zzzzzzzz: adjustment = 4'd1;
            10'b001zzzzzzz: adjustment = 4'd2;
            10'b0001zzzzzz: adjustment = 4'd3;
            10'b00001zzzzz: adjustment = 4'd4;
            10'b000001zzzz: adjustment = 4'd5;
            10'b0000001zzz: adjustment = 4'd6;
            10'b00000001zz: adjustment = 4'd7;
            10'b000000001z: adjustment = 4'd8;
            10'b0000000001: adjustment = 4'd9;
            default: adjustment = 4'd10;
        endcase
    end

    assign adj_a = a << (adjustment+1);
    /* verilator lint_off UNUSEDSIGNAL */
    wire cout;
    /* verilator lint_on UNUSEDSIGNAL */
    sub #(.WIDTH(7)) exp_adj (.a(7'd0), .b({3'b000, adjustment}), .cout(cout), .Sum(adj_exp_a));


endmodule


module reciprocal_rom(
    input  logic [9:0] addr,
    output logic [13:0] data_out
);

    // Declare the ROM array: 1024 entries of 14 bits each
    logic [13:0] rom_memory [0:1023];

    // Load the initialization file generated by Python
    initial begin
        $readmemb("src/reciprocal_rom.mem", rom_memory);
    end

    // Combinatorial read
    always_comb begin
        data_out = rom_memory[addr];
    end

endmodule
