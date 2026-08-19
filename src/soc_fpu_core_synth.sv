// src/soc_fpu_core_synth.sv — PnR top: PicoRV32 + FPU PCPI, no testbench RAM.
// The external memory interface is exposed as top-level ports (the SoC RAM stays
// a testbench artifact) so that the CPU instruction stream is unconstrained and
// Yosys cannot constant-fold the FPU datapath away. The register file inside
// picorv32 and the FPU datapaths are the PnR core.
module soc_fpu_core (
    input  logic        clk,
    input  logic        resetn,
    input  logic        mem_ready,
    input  logic [31:0] mem_rdata,
    output logic        mem_valid,
    output logic        mem_instr,
    output logic [31:0] mem_addr,
    output logic [31:0] mem_wdata,
    output logic [3:0]  mem_wstrb,
    output logic        trap
);
    logic        mem_la_read, mem_la_write;
    logic [31:0] mem_la_addr, mem_la_wdata;
    logic [3:0]  mem_la_wstrb;

    logic        pcpi_valid, pcpi_wr, pcpi_wait, pcpi_ready;
    logic [31:0] pcpi_insn, pcpi_rs1, pcpi_rs2, pcpi_rd;
    logic [31:0] eoi;
    logic        trace_valid;
    logic [35:0] trace_data;

    picorv32 #(
        .ENABLE_COUNTERS   (1'b0),
        .ENABLE_REGS_16_31 (1'b1),
        .ENABLE_PCPI       (1'b1),
        .ENABLE_MUL        (1'b1),
        .ENABLE_FAST_MUL   (1'b1),
        .ENABLE_DIV        (1'b0),
        .CATCH_ILLINSN     (1'b1),
        .ENABLE_IRQ        (1'b1),
        .PROGADDR_RESET    (32'h 0000_0000),
        .PROGADDR_IRQ      (32'h 0000_0800),
        .STACKADDR         (32'h 0000_2000)
    ) u_cpu (
        .clk(clk), .resetn(resetn), .trap(trap),
        .mem_valid(mem_valid), .mem_instr(mem_instr), .mem_ready(mem_ready),
        .mem_addr(mem_addr), .mem_wdata(mem_wdata), .mem_wstrb(mem_wstrb),
        .mem_rdata(mem_rdata),
        .mem_la_read(mem_la_read), .mem_la_write(mem_la_write),
        .mem_la_addr(mem_la_addr), .mem_la_wdata(mem_la_wdata),
        .mem_la_wstrb(mem_la_wstrb),
        .pcpi_valid(pcpi_valid), .pcpi_insn(pcpi_insn),
        .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2),
        .pcpi_wr(pcpi_wr), .pcpi_rd(pcpi_rd),
        .pcpi_wait(pcpi_wait), .pcpi_ready(pcpi_ready),
        .irq(32'b0), .eoi(eoi), .trace_valid(trace_valid), .trace_data(trace_data)
    );

    fpu_pcpi u_fpu_pcpi (
        .clk(clk), .resetn(resetn),
        .pcpi_valid(pcpi_valid), .pcpi_insn(pcpi_insn),
        .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2),
        .pcpi_wr(pcpi_wr), .pcpi_rd(pcpi_rd),
        .pcpi_wait(pcpi_wait), .pcpi_ready(pcpi_ready)
    );
endmodule