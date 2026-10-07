// src/fpu_modules.sv
/* verilator lint_off DECLFILENAME */
module subnormal_fix (
    input  logic [9:0] a,
    output logic [9:0] adj_a,
    output logic [6:0] adj_exp_a
);
    logic [3:0] shift_amt;
    logic [6:0] exp_adj;

    always_comb begin
        casez (a)
            // Format: shift_amt = adjustment + 1, exp_adj = two's complement equivalent
            10'b1zzzzzzzzz: begin shift_amt = 4'd1;  exp_adj = 7'd0;  end
            10'b01zzzzzzzz: begin shift_amt = 4'd2;  exp_adj = 7'd1;  end
            10'b001zzzzzzz: begin shift_amt = 4'd3;  exp_adj = 7'd2;  end
            10'b0001zzzzzz: begin shift_amt = 4'd4;  exp_adj = 7'd3;  end
            10'b00001zzzzz: begin shift_amt = 4'd5;  exp_adj = 7'd4;  end
            10'b000001zzzz: begin shift_amt = 4'd6;  exp_adj = 7'd5;  end
            10'b0000001zzz: begin shift_amt = 4'd7;  exp_adj = 7'd6;  end
            10'b00000001zz: begin shift_amt = 4'd8;  exp_adj = 7'd7;  end
            10'b000000001z: begin shift_amt = 4'd9;  exp_adj = 7'd8;  end
            10'b0000000001: begin shift_amt = 4'd10; exp_adj = 7'd9;  end
            default:        begin shift_amt = 4'd11; exp_adj = 7'd10; end
        endcase
    end

    // No adders or subtractors in the critical path:
    assign adj_a     = a << shift_amt;
    assign adj_exp_a = 7'd0 - exp_adj; // Fixed constant subtraction or ~exp_adj + 1
endmodule

