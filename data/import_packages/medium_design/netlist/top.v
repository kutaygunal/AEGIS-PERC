module medium_top (
  input clk,
  input rst,
  input a,
  input b,
  output out
);

  wire ff1_q;
  wire ff2_q;
  wire and_y;

  reg ff1;
  reg ff2;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      ff1 <= 0;
      ff2 <= 0;
    end else begin
      ff1 <= a;
      ff2 <= b;
    end
  end

  assign ff1_q = ff1;
  assign ff2_q = ff2;
  assign and_y = ff1_q & ff2_q;
  assign out = and_y | ff1_q;

endmodule
