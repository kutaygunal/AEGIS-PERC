module geometry_rich_top (
  input clk,
  input rst,
  input data_in,
  input scan_en,
  output data_out,
  output scan_out
);

  wire q0, q1, q2, q3;
  wire and_out_0, and_out_1, or_out;
  wire mux_out_0, mux_out_1;

  reg dff_0, dff_1, dff_2, dff_3;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      dff_0 <= 0;
      dff_1 <= 0;
      dff_2 <= 0;
      dff_3 <= 0;
    end else begin
      dff_0 <= data_in;
      dff_1 <= q0;
      dff_2 <= q1;
      dff_3 <= q2;
    end
  end

  assign q0 = dff_0;
  assign q1 = dff_1;
  assign q2 = dff_2;
  assign q3 = dff_3;

  assign and_out_0 = q0 & q1;
  assign and_out_1 = q2 & q3;
  assign or_out = q2 | q3;

  assign mux_out_0 = scan_en ? and_out_0 : q0;
  assign mux_out_1 = scan_en ? and_out_1 : q3;

  assign data_out = mux_out_0 | mux_out_1;
  assign scan_out = or_out;

endmodule
