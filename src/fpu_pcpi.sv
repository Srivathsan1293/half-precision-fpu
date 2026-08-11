module fpu_pcpi (
    input logic clk, resetn, pcpi_valid,
    input logic [31:0] pcpi_insn, pcpi_rs1, pcpi_rs2,
    output logic pcpi_wr, pcpi_wait, pcpi_ready,
    output logic [31:0] pcpi_rd
);
    reg [1:0] state_machine = 2'b00; //set to default
    reg ready; //stores when answer has been computed
    reg [1:0] counter;
    // 3 states idle (00), compute (01), done (10)
    //always pass operands and use flags to ignore wrong answers
    // resetn is acitve low
    wire is_fpu_f3 = (pcpi_insn[14:12] == 3'b000);
    // Standard-op rounding-mode field (funct3). clang emits the dynamic rounding
    // encoding (funct3=111) for fadd.h/fsub.h/fmul.h/fdiv.h by default; since the
    // frm CSR shadow is always RNE in this system (nothing writes fcsr), dynamic
    // rounding is equivalent to RNE (funct3=000). Claim both.
    wire is_std_f3 = (pcpi_insn[14:12] == 3'b000) | (pcpi_insn[14:12] == 3'b111);
    wire is_custom0 = (pcpi_insn[6:0] == 7'b0001011);
    // legacy custom0 (0x0b) encodings, funct7 0x06..0x09
    wire instr_fadd = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000110);
    wire instr_fsub = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000111);
    wire instr_fmul = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001000);
    wire instr_fdiv = is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001001);
    // standard Zhinx encodings (opcode 0x53), funct7 per spec
    wire is_std_fp = (pcpi_insn[6:0] == 7'b1010011);
    wire instr_std_fadd = is_std_fp && is_std_f3 && (pcpi_insn[31:25] == 7'b0000010);
    wire instr_std_fsub = is_std_fp && is_std_f3 && (pcpi_insn[31:25] == 7'b0000110);
    wire instr_std_fmul = is_std_fp && is_std_f3 && (pcpi_insn[31:25] == 7'b0001010);
    wire instr_std_fdiv = is_std_fp && is_std_f3 && (pcpi_insn[31:25] == 7'b0001110);
    // claimed: only enter the FSM for a recognized op. Unrecognized FP insns are
    // left unclaimed (no pcpi_wait/pcpi_ready) so PicoRV32's pcpi_timeout fires
    // and the ebreak-IRQ handler / emulator can take over.
    wire claimed = instr_fadd | instr_fsub | instr_fmul | instr_fdiv |
                   instr_std_fadd | instr_std_fsub | instr_std_fmul | instr_std_fdiv;
    wire start_compute = pcpi_valid & claimed;
    wire [1:0] fpu_op = (instr_fdiv | instr_std_fdiv) ? 2'b11
                  : (instr_fmul | instr_std_fmul) ? 2'b10
                  : (instr_fsub | instr_std_fsub) ? 2'b01 : 2'b00;


    always_ff @(posedge clk) begin
        //set up outputs of system first
        pcpi_wr <= (!state_machine[1]) & (state_machine[0]) & resetn & ready;
        pcpi_wait <= (!state_machine[1]) & ( ((!state_machine[0]) & resetn & start_compute) | (start_compute & !ready & resetn) );
        pcpi_ready <= (!state_machine[1]) & (state_machine[0]) & resetn & ready;

        state_machine[1] <= resetn & ((state_machine[1] & pcpi_valid) | ((!state_machine[1]) & state_machine[0] & ready));
        state_machine[0] <= (!state_machine[1]) & ( ((!state_machine[0]) & resetn & start_compute) | (start_compute & !ready & resetn) );

        if (pcpi_wait) begin
            counter <= counter + 1;
            // FDIV has a 3-register pipeline (4 cycles), FADDSUB/FMUL only 1
            // (2 cycles): assert ready as soon as each op's answer is valid.
            if (counter == 2'd3 && fpu_op == 2'b11) begin
                ready <= 1'd1;
            end else if (counter == 2'd1 && fpu_op != 2'b11) begin
                ready <= 1'd1;
            end
        end else begin
            counter <= 2'd0;
            ready <= 1'd0;
        end
    end



    fpu_test u_fpu_test(.a(pcpi_rs1[15:0]), .b(pcpi_rs2[15:0]), .op(fpu_op), .clk(clk), .ans(pcpi_rd[15:0]));
    assign pcpi_rd[31:16] = 16'd0;



endmodule
