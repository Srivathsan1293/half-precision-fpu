// tb/soc_fpu_top.sv
//
// PicoRV32 + RAM SoC for exercising the half-precision FPU through the
// Pico Co-Processor Interface (PCPI).
//
// The CPU is configured with ENABLE_PCPI=1. Any custom0 (0x0b) instruction
// that is not a base RV32I instruction is presented on the PCPI bus
// (pcpi_valid/pcpi_insn/pcpi_rs1/pcpi_rs2) and the core stalls until a
// coprocessor asserts pcpi_ready.
//
// Connecting the FPU coprocessor (PCPI wrapper):
//   Define HAS_FPU_PCPI when building this top. The wrapper module is
//   expected to expose exactly this interface:
//
//     module fpu_pcpi (
//         input  logic clk, resetn,
//         input  logic        pcpi_valid,
//         input  logic [31:0] pcpi_insn,
//         input  logic [31:0] pcpi_rs1,
//         input  logic [31:0] pcpi_rs2,
//         output logic        pcpi_wr,
//         output logic [31:0] pcpi_rd,
//         output logic        pcpi_wait,
//         output logic        pcpi_ready
//     );
//
//   With HAS_FPU_PCPI undefined the PCPI result/handshake inputs are tied
//   to 0 so the core can run a plain RV32I smoke test without the wrapper.
//
// Custom-instruction encoding (see tb/firmware/fpu_macros.h):
//     | funct7      | rs2 | rs1 | funct3 | rd | opcode    |
//     | 0000110..09 | reg | reg | 000    | reg| 0001011   |
//   funct7: 0000110=FADD 0000111=FSUB 0001000=FMUL 0001001=FDIV

module soc_fpu_top (
    input  logic clk,
    input  logic resetn,
    output logic trap
);

    // 16 KB synchronous RAM (4096 words) at 0x0000.
    localparam int DEPTH = 4096;

    logic [31:0] ram [0:DEPTH-1] /* verilator public_flat_rd */;

    // ------------------------------------------------------------------
    // Native memory interface
    // ------------------------------------------------------------------
    logic        mem_valid, mem_instr, mem_ready;
    logic [31:0] mem_addr, mem_wdata, mem_rdata;
    logic [3:0]  mem_wstrb;

    // Look-ahead memory interface (unused, tied off)
    logic        mem_la_read, mem_la_write;
    logic [31:0] mem_la_addr, mem_la_wdata;
    logic [3:0]  mem_la_wstrb;

    // ------------------------------------------------------------------
    // PCPI interface
    // ------------------------------------------------------------------
    logic        pcpi_valid, pcpi_wr, pcpi_wait, pcpi_ready;
    logic [31:0] pcpi_insn, pcpi_rs1, pcpi_rs2, pcpi_rd;

    // Unused CPU diagnostics (tied off)
    logic [31:0] eoi;
    logic        trace_valid;
    logic [35:0] trace_data;

    picorv32 #(
        .ENABLE_COUNTERS   (1'b0),
        .ENABLE_REGS_16_31 (1'b1),
        .ENABLE_PCPI       (1'b1),
        .ENABLE_MUL        (1'b0),
        .ENABLE_FAST_MUL   (1'b0),
        .ENABLE_DIV        (1'b0),
        .CATCH_ILLINSN     (1'b1),
        .PROGADDR_RESET    (32'h 0000_0000),
        .STACKADDR         (32'h 0000_2000)
    ) u_cpu (
        .clk        (clk),
        .resetn     (resetn),
        .trap       (trap),
        .mem_valid  (mem_valid),
        .mem_instr  (mem_instr),
        .mem_ready  (mem_ready),
        .mem_addr   (mem_addr),
        .mem_wdata  (mem_wdata),
        .mem_wstrb  (mem_wstrb),
        .mem_rdata  (mem_rdata),
        .mem_la_read (mem_la_read),
        .mem_la_write(mem_la_write),
        .mem_la_addr (mem_la_addr),
        .mem_la_wdata(mem_la_wdata),
        .mem_la_wstrb(mem_la_wstrb),
        .pcpi_valid (pcpi_valid),
        .pcpi_insn  (pcpi_insn),
        .pcpi_rs1   (pcpi_rs1),
        .pcpi_rs2   (pcpi_rs2),
        .pcpi_wr    (pcpi_wr),
        .pcpi_rd    (pcpi_rd),
        .pcpi_wait  (pcpi_wait),
        .pcpi_ready (pcpi_ready),
        .irq        (32'h 0),
        .eoi        (eoi),
        .trace_valid(trace_valid),
        .trace_data (trace_data)
    );

    `ifdef HAS_FPU_PCPI
        // Instantiate your PCPI wrapper here. It will be wired to the
        // internal pcpi_* signals above. Expected interface is documented
        // in the header comment.
        fpu_pcpi u_fpu_pcpi (
            .clk        (clk),
            .resetn     (resetn),
            .pcpi_valid (pcpi_valid),
            .pcpi_insn  (pcpi_insn),
            .pcpi_rs1   (pcpi_rs1),
            .pcpi_rs2   (pcpi_rs2),
            .pcpi_wr    (pcpi_wr),
            .pcpi_rd    (pcpi_rd),
            .pcpi_wait  (pcpi_wait),
            .pcpi_ready (pcpi_ready)
        );
    `else
        // No coprocessor: never claim an instruction, never write back.
        assign pcpi_wr    = 1'b0;
        assign pcpi_wait  = 1'b0;
        assign pcpi_ready = 1'b0;
        assign pcpi_rd    = 32'h 0;
    `endif

    // ------------------------------------------------------------------
    // RAM: synchronous byte writes, async reads, always ready.
    // ------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (mem_valid && mem_wstrb[0]) ram[mem_addr[13:2]][ 7: 0] <= mem_wdata[ 7: 0];
        if (mem_valid && mem_wstrb[1]) ram[mem_addr[13:2]][15: 8] <= mem_wdata[15: 8];
        if (mem_valid && mem_wstrb[2]) ram[mem_addr[13:2]][23:16] <= mem_wdata[23:16];
        if (mem_valid && mem_wstrb[3]) ram[mem_addr[13:2]][31:24] <= mem_wdata[31:24];
    end

    assign mem_ready = 1'b1;
    assign mem_rdata = ram[mem_addr[13:2]];

    initial begin
        $readmemh("tb/firmware/firmware.hex", ram);
    end

endmodule
