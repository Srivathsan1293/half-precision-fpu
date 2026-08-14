// Adapter: present a fpu_pcpi-compatible wrapper that drives the FPNew bench
// wrapper (sv2v-converted fpnew_bench_top). This maps the PCPI interface used
// by soc_fpu_top to the FPNew bench top's simpler handshake.

module fpu_pcpi (
    input  logic        clk,
    input  logic        resetn,
    input  logic        pcpi_valid,
    input  logic [31:0] pcpi_insn,
    input  logic [31:0] pcpi_rs1,
    input  logic [31:0] pcpi_rs2,
    output logic        pcpi_wr,
    output logic        pcpi_wait,
    output logic        pcpi_ready,
    output logic [31:0] pcpi_rd
);

    // Minimal decode for custom0/Zhinx encodings (FADD/FSUB/FMUL/FDIV)
    wire is_fpu_f3 = (pcpi_insn[14:12] == 3'b000) || (pcpi_insn[14:12] == 3'b111);
    wire is_custom0 = (pcpi_insn[6:0] == 7'b0001011);
    wire is_std_fp = (pcpi_insn[6:0] == 7'b1010011);
    wire instr_fadd = (is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000110)) ||
                      (is_std_fp && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000010));
    wire instr_fsub = (is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000111)) ||
                      (is_std_fp && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0000110));
    wire instr_fmul = (is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001000)) ||
                      (is_std_fp && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001010));
    wire instr_fdiv = (is_custom0 && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001001)) ||
                      (is_std_fp && is_fpu_f3 && (pcpi_insn[31:25] == 7'b0001110));
    wire claimed = instr_fadd | instr_fsub | instr_fmul | instr_fdiv;

    // FPNew bench expects: operands_i [2:0][15:0], rnd_mode, op_i, op_mod, in_valid
    // We'll present operands[0]=rs1[15:0], operands[1]=rs2[15:0], operands[2]=0.

    // Operation codes from fpnew_pkg.operation_e (as in third_party fpnew):
    // FMADD=0, FNMSUB=1, ADD=2, MUL=3, DIV=4, SQRT=5, ... ADDS near end
    localparam [3:0] OP_ADD = 4'd2;
    localparam [3:0] OP_MUL = 4'd3;
    localparam [3:0] OP_DIV = 4'd4;

    // Round mode: RNE = 3'b000
    localparam [2:0] RNE = 3'b000;

    // Wires to fpnew bench
    logic [2:0][15:0] operands_i;
    logic [2:0]       op_i;         // will widen where needed by sv2v output
    logic [2:0]       op_i_small;   // temporary
    logic             op_mod_i;
    logic [2:0]       rnd_mode_i;   // RNE
    logic             in_valid_i;
    logic             in_ready_o;
    logic             flush_i;
    logic [15:0]      result_o;
    // status type and others are left unwired where not needed
    logic             out_valid_o;
    logic             out_ready_i;
    logic             busy_o;
    logic             early_valid_o;

    // Assign operands and control.
    // fpnew's FMA/divsqrt consume operands differently per operation:
    //   MUL/DIV  : result = f(operands[0], operands[1])   (operands[2] ignored)
    //   ADD/FSUB : result = 1.0 * operands[1] + operands[2] (op_mod flips c's sign)
    // so we map rs1/rs2 to the correct slots per operation.
    wire is_muldiv = instr_fmul | instr_fdiv;
    assign operands_i[0] = pcpi_rs1[15:0];
    assign operands_i[1] = is_muldiv ? pcpi_rs2[15:0] : pcpi_rs1[15:0];
    assign operands_i[2] = pcpi_rs2[15:0];
    assign rnd_mode_i = RNE;

    // op_i: choose ADD/MUL/DIV and op_mod for subtraction
    // Represent op_i as 4-bit in instantiation (fpnew expects operation_e width OP_BITS=4)
    logic [3:0] op_e;
    always_comb begin
        op_e = OP_ADD;
        op_mod_i = 1'b0;
        if (instr_fadd) begin
            op_e = OP_ADD;
            op_mod_i = 1'b0;
        end
        if (instr_fsub) begin
            op_e = OP_ADD; // subtraction encoded via op_mod
            op_mod_i = 1'b1;
        end
        if (instr_fmul) begin
            op_e = OP_MUL;
            op_mod_i = 1'b0;
        end
        if (instr_fdiv) begin
            op_e = OP_DIV;
            op_mod_i = 1'b0;
        end
    end

    // Simple request handshake state
    typedef enum logic [1:0] {IDLE, SENT, WAIT_RESP} state_t;
    state_t state;
    logic result_valid_d;
    logic [15:0] result_q;

    // Connect small in_ready/out_ready
    assign out_ready_i = 1'b1; // always accept result from fpnew

    // Instantiate fpnew bench (after sv2v conversion this will be available)
    // The module name is fpnew_bench_top and has the interface in synth_scripts.
    fpnew_bench_top u_fpnew_bench (
        .clk_i(clk),
        .rst_ni(resetn),
        .operands_i(operands_i),
        .rnd_mode_i(rnd_mode_i),
        .op_i(op_e),
        .op_mod_i(op_mod_i),
        .in_valid_i(in_valid_i),
        .in_ready_o(in_ready_o),
        .flush_i(flush_i),
        .result_o(result_o),
        .status_o(),
        .out_valid_o(out_valid_o),
        .out_ready_i(out_ready_i),
        .busy_o(busy_o),
        .early_valid_o(early_valid_o)
    );

    // Drive in_valid_i from PCPI when claimed and not already active.
    // fpnew is configured combinational (PipeConfig=BEFORE, 0 pipe regs), so
    // in_ready_o and out_valid_o (with result_o) are both available in the
    // SAME cycle that in_valid_i is asserted. We therefore capture the result
    // as soon as in_ready_o && out_valid_o, without de-asserting in_valid_i
    // first (that would drop the combinational result).
    always_ff @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            state <= IDLE;
            in_valid_i <= 1'b0;
            result_q <= 16'd0;
            result_valid_d <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    result_valid_d <= 1'b0;
                    if (pcpi_valid && claimed) begin
                        in_valid_i <= 1'b1;
                        state <= SENT;
                    end else begin
                        in_valid_i <= 1'b0;
                        // If the CPU presents an FP opcode but we didn't claim it, dump it
                        // once to help debugging and fail the sim so we capture the insn.
                        if (pcpi_valid && (pcpi_insn[6:0] == 7'b0001011 || pcpi_insn[6:0] == 7'b1010011) && !claimed) begin
                            $display("[fpnew_adapter] UNCLAIMED FP insn at time %0t insn=0x%08h rs1=0x%08h rs2=0x%08h", $time, pcpi_insn, pcpi_rs1, pcpi_rs2);
                            $finish(1);
                        end
                    end
                end
                SENT: begin
                    // Combinational fpnew: in_ready_o && out_valid_o arrive
                    // together in the same cycle the input is accepted. Capture
                    // result_o then, and clear in_valid_i. (Pipelined fpnew would
                    // assert in_ready_o first and out_valid_o later; we hold
                    // in_valid_i until the result appears via WAIT_RESP.)
                    if (in_ready_o && out_valid_o) begin
                        in_valid_i <= 1'b0;
                        result_q <= result_o;
                        result_valid_d <= 1'b1;
                        state <= IDLE;
                    end else if (in_ready_o) begin
                        // Pipelined fpnew accepted the input; wait for the result.
                        in_valid_i <= 1'b0;
                        state <= WAIT_RESP;
                    end
                end
                WAIT_RESP: begin
                    // Wait for output valid (pipelined fpnew path).
                    if (out_valid_o) begin
                        result_q <= result_o;
                        result_valid_d <= 1'b1;
                        state <= IDLE;
                    end
                end
            endcase
        end
    end

    // pcpi handshake outputs
    assign pcpi_wait = (state != IDLE);
    assign pcpi_ready = (result_valid_d);
    assign pcpi_wr = result_valid_d;
    // Present result in low 16 bits; upper bits zero
    assign pcpi_rd = {16'd0, result_q};

endmodule
