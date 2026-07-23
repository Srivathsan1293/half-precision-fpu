// src/fpu_test.sv
module fpu_test (
    input logic [15:0] a,b,
    output logic[15:0] ans
);




    //add #(.WIDTH(4)) add_4bit(.a(a), .b(b), .cout(cout), .Sum(ans));
    //sub #(.WIDTH(4)) sub_4bit(.a(a), .b(b), .cout(cout), .Sum(ans));
    //left #(.WIDTH(4)) left_4bit(.value(a), .shift_amt(b), .ans(ans));
    //right #(.WIDTH(4)) right_4bit(.value(a), .shift_amt(b), .ans(ans));
    //MUL mul (.A(a), .B(b), .ans(ans));
    //FMUL fpu (.a(a), .b(b), .ans(ans));
    addsub fpu (.a(a), .b(b), .sub(sub), .ans(ans));
    //DIV fpu (.a(a), .b(b), .out(ans));
endmodule
