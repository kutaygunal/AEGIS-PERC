module simple_design (
  input clk,
  input rst,
  input a,
  input b,
  output out
);

  reg reg_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      reg_out <= 0;
    end else begin
      reg_out <= a | b;
    end
  end

  assign out = reg_out;

endmodule
