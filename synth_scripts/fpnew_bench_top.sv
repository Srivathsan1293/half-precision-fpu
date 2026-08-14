/* fpnew_bench_top.sv — FPNew config wrapper for the FP16 benchmark.
 *
 * Semantic match to this project's FPU (fpu_test): binary16 only, IEEE-754
 * with RNE rounding, subnormals + inf/NaN handled, and ONLY
 * FADD/FSUB/FMUL/FDIV.
 *
 * fpnew_top always instantiates all four opgroups (ADDMUL, DIVSQRT, NONCOMP,
 * CONV). To keep the comparison apples-to-apples we instantiate only the
 * ADDMUL and DIVSQRT opgroup blocks and arbitrate between them.
 *
 * Concrete parameters (fixed by this wrapper):
 *   - Width          = 16 (binary16 data path)
 *   - EnableVectors  = 0  (scalar only, like the single-lane fpu_test)
 *   - EnableNanBox   = 0  (our convention: plain 16-bit halves, no boxing)
 *   - FpFmtMask      = 5'b00100  (FP16 bit only)
 *   - IntFmtMask     = 4'b0000   (no integer conversions)
 *   - PipeRegs       = 0, PipeConfig = BEFORE  (combinational, cycle 1)
 *   - UnitTypes      = ADDMUL PARALLEL, DIVSQRT MERGED
 *   - DivSqrtSel     = PULP  (small iterative divider; supports FP16)
 *
 * Combinational data path (out_valid_o shown for handshake completeness),
 * matching how fpu_test is measured (reg-to-reg LTP).
 *
 * SPDX-License-Identifier: SHL-0.51 (inherited from fpnew sources)
 */

`include "fpnew_pkg.sv"

module fpnew_bench_top #(
  parameter type TagType = logic
) (
  input  logic                  clk_i,
  input  logic                  rst_ni,
  input  logic [2:0][15:0]      operands_i,
  input  fpnew_pkg::roundmode_e rnd_mode_i,
  input  fpnew_pkg::operation_e op_i,
  input  logic                  op_mod_i,
  input  logic                  in_valid_i,
  output logic                  in_ready_o,
  input  logic                  flush_i,
  output logic [15:0]           result_o,
  output fpnew_pkg::status_t    status_o,
  output logic                  out_valid_o,
  input  logic                  out_ready_i,
  output logic                  busy_o,
  output logic                  early_valid_o
);

  localparam int unsigned WIDTH = 16;
  localparam logic ENABLE_VECT  = 1'b0;
  localparam fpnew_pkg::fmt_logic_t FPMASK  = 5'b00100;   /* FP16 only  */
  localparam fpnew_pkg::ifmt_logic_t INTMASK = 4'b0000;   /* no int     */
  localparam fpnew_pkg::divsqrt_unit_t DIVSEL = fpnew_pkg::PULP;

  localparam int unsigned NUM_FORMATS  = fpnew_pkg::NUM_FP_FORMATS;   /* 5 */
  localparam int unsigned NUM_OPERANDS = 3;

  /* ---- opgroup decode (ADDMUL group vs DIVSQRT group) ---- */
  // Written as explicit comparisons (no unpacked arrays / loops) so that the
  // sv2v conversion keeps the encoding correct.
  logic is_addmul, is_divsqrt;
  assign is_addmul  = (op_i == fpnew_pkg::FMADD) | (op_i == fpnew_pkg::FNMSUB) |
                      (op_i == fpnew_pkg::ADD)   | (op_i == fpnew_pkg::MUL)   |
                      (op_i == fpnew_pkg::ADDS);
  assign is_divsqrt = (op_i == fpnew_pkg::DIV) | (op_i == fpnew_pkg::SQRT);

  logic in_valid_a, in_valid_d;
  assign in_valid_a = in_valid_i & is_addmul;
  assign in_valid_d = in_valid_i & is_divsqrt;

  logic [WIDTH-1:0] res[1:0];
  fpnew_pkg::status_t st[1:0];
  logic grp_in_ready[1:0], grp_out_valid[1:0], grp_out_ready[1:0];
  logic grp_busy[1:0], grp_early[1:0];

  logic [NUM_FORMATS-1:0][NUM_OPERANDS-1:0] is_boxed;
  assign is_boxed = '1;   /* NaN-boxing disabled */

  /* -------------------- ADDMUL opgroup (FADD/FSUB/FMUL) ------------------- */
  fpnew_opgroup_block #(
    .OpGroup       ( fpnew_pkg::ADDMUL ),
    .Width         ( WIDTH ),
    .EnableVectors ( ENABLE_VECT ),
    .DivSqrtSel    ( DIVSEL ),
    .FpFmtMask     ( FPMASK ),
    .IntFmtMask    ( INTMASK ),
    .FmtPipeRegs   ( '{32'd0, 32'd0, 32'd0, 32'd0, 32'd0} ),
    .FmtUnitTypes  ( '{2'd1, 2'd1, 2'd1, 2'd1, 2'd1} ),   /* PARALLEL */
    .PipeConfig    ( fpnew_pkg::BEFORE ),
    .TagType       ( TagType )
  ) i_addmul (
    .clk_i, .rst_ni,
    .operands_i      ( operands_i ),
    .is_boxed_i      ( is_boxed ),
    .rnd_mode_i,
    .op_i, .op_mod_i,
    .src_fmt_i       ( fpnew_pkg::FP16 ),
    .dst_fmt_i       ( fpnew_pkg::FP16 ),
    .int_fmt_i       ( fpnew_pkg::INT16 ),
    .vectorial_op_i  ( 1'b0 ),
    .tag_i           ( '0 ),
    .simd_mask_i     ( '0 ),
    .in_valid_i      ( in_valid_a ),
    .in_ready_o      ( grp_in_ready[0] ),
    .flush_i,
    .result_o        ( res[0] ),
    .status_o        ( st[0] ),
    .extension_bit_o ( ),
    .tag_o           ( ),
    .out_valid_o     ( grp_out_valid[0] ),
    .out_ready_i     ( grp_out_ready[0] ),
    .busy_o          ( grp_busy[0] ),
    .early_valid_o   ( grp_early[0] )
  );

  /* --------------------- DIVSQRT opgroup (FDIV) --------------------------- */
  fpnew_opgroup_block #(
    .OpGroup       ( fpnew_pkg::DIVSQRT ),
    .Width         ( WIDTH ),
    .EnableVectors ( ENABLE_VECT ),
    .DivSqrtSel    ( DIVSEL ),
    .FpFmtMask     ( FPMASK ),
    .IntFmtMask    ( INTMASK ),
    .FmtPipeRegs   ( '{32'd0, 32'd0, 32'd0, 32'd0, 32'd0} ),
    .FmtUnitTypes  ( '{2'd2, 2'd2, 2'd2, 2'd2, 2'd2} ),   /* MERGED */
    .PipeConfig    ( fpnew_pkg::BEFORE ),
    .TagType       ( TagType )
  ) i_divsqrt (
    .clk_i, .rst_ni,
    .operands_i      ( operands_i ),
    .is_boxed_i      ( is_boxed ),
    .rnd_mode_i,
    .op_i, .op_mod_i,
    .src_fmt_i       ( fpnew_pkg::FP16 ),
    .dst_fmt_i       ( fpnew_pkg::FP16 ),
    .int_fmt_i       ( fpnew_pkg::INT16 ),
    .vectorial_op_i  ( 1'b0 ),
    .tag_i           ( '0 ),
    .simd_mask_i     ( '0 ),
    .in_valid_i      ( in_valid_d ),
    .in_ready_o      ( grp_in_ready[1] ),
    .flush_i,
    .result_o        ( res[1] ),
    .status_o        ( st[1] ),
    .extension_bit_o ( ),
    .tag_o           ( ),
    .out_valid_o     ( grp_out_valid[1] ),
    .out_ready_i     ( grp_out_ready[1] ),
    .busy_o          ( grp_busy[1] ),
    .early_valid_o   ( grp_early[1] )
  );

  /* ----------------------- output arbitration ----------------------------- */
  assign grp_out_ready[0] = out_ready_i;
  assign grp_out_ready[1] = out_ready_i;

  always_comb begin : mux
    if (grp_out_valid[0]) begin
      result_o = res[0];
      status_o = st[0];
    end else begin
      result_o = res[1];
      status_o = st[1];
    end
  end

  assign in_ready_o  = is_addmul ? grp_in_ready[0] : grp_in_ready[1];
  assign out_valid_o = grp_out_valid[0] | grp_out_valid[1];
  assign busy_o      = grp_busy[0] | grp_busy[1];
  assign early_valid_o = grp_early[0] | grp_early[1];

endmodule