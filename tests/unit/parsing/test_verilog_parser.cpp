#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/parser_interface.hpp"
#include "aegis/parsing/verilog_parser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace aegis::parsing;

namespace {

fs::path write_temp_verilog(const std::string& content)
{
    const auto path = fs::temp_directory_path() /
        ("aegis_verilog_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".v");
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

void remove_temp(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
}

struct RecordingCallbacks : public IParserCallbacks {
    fs::path begin_path;
    std::vector<Progress> progresses;
    std::vector<ParsedCell> cells;
    std::vector<ParsedNet> nets;
    std::vector<ParsedPin> pins;
    std::vector<std::pair<std::string, std::optional<int>>> errors;
    std::optional<bool> end_success;

    void on_begin(const fs::path& p) override { begin_path = p; }
    void on_progress(const Progress& p) override { progresses.push_back(p); }
    void on_cell(const ParsedCell& c) override { cells.push_back(c); }
    void on_net(const ParsedNet& n) override { nets.push_back(n); }
    void on_pin(const ParsedPin& p) override { pins.push_back(p); }
    void on_geometry(const ParsedGeometry&) override {}
    void on_error(const std::string& message, std::optional<int> line) override { errors.emplace_back(message, line); }
    void on_end(bool success) override { end_success = success; }
};

const char* kGateLevelNetlist = R"(
module top (
  input A,
  input B,
  output Y
);
  wire n1;
  wire dangling;
  INVX1 u_inv (.A(A), .Y(n1));
  NOR2X1 u_nor (.A(n1), .B(B), .Y(Y));
endmodule
)";

const char* kMixedNetlist = R"(
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
)";

} // namespace

TEST_CASE("VerilogParser format_name returns Verilog", "[parsing][Verilog][fast]")
{
    VerilogParser parser;
    REQUIRE(parser.format_name() == "Verilog");
}

TEST_CASE("VerilogParser streaming parse emits normalized ports nets and instances", "[parsing][Verilog][fast]")
{
    const auto path = write_temp_verilog(kGateLevelNetlist);
    VerilogParser parser;
    RecordingCallbacks callbacks;
    NullCancellationToken token;

    const bool ok = parser.parse(path, callbacks, token);
    remove_temp(path);

    REQUIRE(ok);
    REQUIRE(callbacks.end_success == true);
    REQUIRE(callbacks.begin_path == path);
    REQUIRE(callbacks.errors.empty());
    REQUIRE(callbacks.progresses.size() >= 2);
    REQUIRE(callbacks.cells.size() == 2);
    REQUIRE(callbacks.pins.size() == 3);

    REQUIRE(callbacks.cells[0].name == "u_inv");
    REQUIRE(callbacks.cells[0].properties.at("type") == "INVX1");
    REQUIRE(callbacks.cells[1].name == "u_nor");
    REQUIRE(callbacks.cells[1].properties.at("type") == "NOR2X1");

    bool saw_dangling = false;
    for (const auto& net : callbacks.nets) {
        if (net.name == "dangling") {
            saw_dangling = true;
            REQUIRE(net.pin_names.empty());
        }
    }
    REQUIRE(saw_dangling);
}

TEST_CASE("VerilogParser parse_to_layout_ir normalizes gate-level connectivity", "[parsing][Verilog][fast]")
{
    const auto path = write_temp_verilog(kGateLevelNetlist);
    VerilogParser parser;

    const LayoutIR ir = parser.parse_to_layout_ir(path);
    remove_temp(path);

    REQUIRE(ir.design_name == "top");
    REQUIRE(ir.devices.size() == 2);
    REQUIRE(ir.ports.size() == 3);
    REQUIRE(ir.metadata.at("source_format") == "verilog");
    REQUIRE(ir.metadata.at("top_module") == "top");

    REQUIRE(ir.devices[0].name == "u_inv");
    REQUIRE(ir.devices[0].type == "INVX1");
    REQUIRE(ir.devices[0].pins.at("A") == "A");
    REQUIRE(ir.devices[0].pins.at("Y") == "n1");

    auto dangling = std::find_if(ir.nets.begin(), ir.nets.end(), [](const auto& net) {
        return net.name == "dangling";
    });
    REQUIRE(dangling != ir.nets.end());
    REQUIRE(dangling->pin_names.empty());
}

TEST_CASE("VerilogParser accepts shipped mixed-style sample netlist and preserves assign connectivity", "[parsing][Verilog][fast]")
{
    const auto path = write_temp_verilog(kMixedNetlist);
    VerilogParser parser;

    const LayoutIR ir = parser.parse_to_layout_ir(path);
    remove_temp(path);

    REQUIRE(ir.design_name == "simple_design");
    REQUIRE(ir.ports.size() == 5);

    auto assign_it = std::find_if(ir.devices.begin(), ir.devices.end(), [](const auto& device) {
        return device.type == "ASSIGN";
    });
    REQUIRE(assign_it != ir.devices.end());
    REQUIRE(assign_it->pins.at("lhs") == "out");
    REQUIRE(assign_it->pins.at("rhs") == "reg_out");
}

TEST_CASE("VerilogParser reports malformed instance connectivity with line context", "[parsing][Verilog][fast]")
{
    const auto path = write_temp_verilog(
        "module top(input A, output Y);\n"
        "  INVX1 u1 (.A(A), .Y(Y);\n"
        "endmodule\n");
    VerilogParser parser;
    RecordingCallbacks callbacks;
    NullCancellationToken token;

    const bool ok = parser.parse(path, callbacks, token);
    remove_temp(path);

    REQUIRE_FALSE(ok);
    REQUIRE(callbacks.end_success == false);
    REQUIRE_FALSE(callbacks.errors.empty());
    REQUIRE(callbacks.errors.front().first.find("Line 2") != std::string::npos);
}

TEST_CASE("VerilogParser supports the shipped sample netlist file", "[parsing][Verilog][fast]")
{
    const fs::path sample = fs::path(AEGIS_SOURCE_DIR) / "data" / "import_packages" / "openframe_simple_design" / "netlist" / "top.v";
    REQUIRE(fs::exists(sample));

    VerilogParser parser;
    const LayoutIR ir = parser.parse_to_layout_ir(sample);

    REQUIRE(ir.design_name == "simple_design");
    REQUIRE(ir.ports.size() == 5);
    REQUIRE_FALSE(ir.devices.empty());
}
