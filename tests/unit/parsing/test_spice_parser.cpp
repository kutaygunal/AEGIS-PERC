#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/spice_parser.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Temp-file helpers
// ---------------------------------------------------------------------------

static fs::path write_temp_spice(const std::string& content) {
    auto tmp = fs::temp_directory_path() / ("aegis_spice_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sp");
    std::ofstream out(tmp, std::ios::binary);
    out << content;
    out.close();
    return tmp;
}

static void remove_temp(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

// ---------------------------------------------------------------------------
// Recording callbacks
// ---------------------------------------------------------------------------

struct RecordingCallbacks : public IParserCallbacks {
    fs::path             begin_path;
    std::vector<std::string> event_log;
    std::vector<Progress>    progresses;
    std::vector<ParsedCell>   cells;
    std::vector<ParsedNet>    nets;
    std::vector<ParsedPin>    pins;
    std::vector<ParsedGeometry> geometries;
    std::vector<std::pair<std::string, std::optional<int>>> errors;
    std::optional<bool>       end_success;

    void on_begin(const fs::path& p) override {
        begin_path = p;
        event_log.push_back("begin");
    }
    void on_progress(const Progress& p) override {
        progresses.push_back(p);
        event_log.push_back("progress");
    }
    void on_cell(const ParsedCell& c) override {
        cells.push_back(c);
        event_log.push_back("cell:" + c.name);
    }
    void on_net(const ParsedNet& n) override {
        nets.push_back(n);
        event_log.push_back("net:" + n.name);
    }
    void on_pin(const ParsedPin& p) override {
        pins.push_back(p);
        event_log.push_back("pin:" + p.name);
    }
    void on_geometry(const ParsedGeometry& g) override {
        geometries.push_back(g);
        event_log.push_back("geometry:" + g.layer);
    }
    void on_error(const std::string& msg, std::optional<int> line) override {
        errors.emplace_back(msg, line);
        event_log.push_back("error:" + msg);
    }
    void on_end(bool success) override {
        end_success = success;
        event_log.push_back("end:" + std::string(success ? "true" : "false"));
    }
};

// ---------------------------------------------------------------------------
// Valid fixture: simple inverter subcircuit (reduced SPICE)
// ---------------------------------------------------------------------------

static const char* kValidSpice = R"(
* Inverter SPICE netlist
.SUBCKT inverter in out vdd vss
MN1 out in vss vss NMOS w=0.5u l=0.18u
MP1 out in vdd vdd PMOS w=1.0u l=0.18u
.ENDS inverter

* Testbench elements
Vin in 0 DC 1.8
Vdd vdd 0 DC 1.8
Vss vss 0 DC 0.0
Rload out gnd 1k
Cload out gnd 10f
Ibias test_ref 0 1u
)";

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("SpiceParser format_name returns SPICE-like",
          "[parsing][SpiceParser][fast]")
{
    SpiceParser parser;
    REQUIRE(parser.format_name() == "SPICE-like");
}

TEST_CASE("SpiceParser streaming parse emits all expected events",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(kValidSpice);
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(ok);
    REQUIRE(cb.end_success.value_or(false));

    // begin + end
    REQUIRE(cb.begin_path == tmp);
    REQUIRE(cb.event_log.front() == "begin");
    REQUIRE(cb.event_log.back()  == "end:true");

    // 2 devices in subckt + 6 testbench devices = 8 cells
    REQUIRE(cb.cells.size() == 8);

    // Devices in subckt
    REQUIRE(cb.cells[0].name == "N1");
    REQUIRE(cb.cells[0].properties.at("type") == "MOSFET");
    REQUIRE(cb.cells[0].properties.at("drain") == "out");
    REQUIRE(cb.cells[0].properties.at("gate") == "in");

    REQUIRE(cb.cells[1].name == "P1");
    REQUIRE(cb.cells[1].properties.at("type") == "MOSFET");
    REQUIRE(cb.cells[1].properties.at("model") == "PMOS");

    // Ports from .SUBCKT
    REQUIRE(cb.pins.size() == 4);
    REQUIRE(cb.pins[0].name == "in");
    REQUIRE(cb.pins[0].direction == "INOUT");
    REQUIRE(cb.pins[0].net_name == "in");

    REQUIRE(cb.pins[3].name == "vss");

    // No geometries in SPICE
    REQUIRE(cb.geometries.empty());

    // All nets
    REQUIRE(!cb.nets.empty());
    REQUIRE(cb.errors.empty());
}

TEST_CASE("SpiceParser parse_to_layout_ir produces complete LayoutIR",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(kValidSpice);
    SpiceParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.design_name == "inverter");
    REQUIRE(ir.devices.size() == 8);

    // MOSFET
    REQUIRE(ir.devices[0].name == "N1");
    REQUIRE(ir.devices[0].type == "MOSFET");
    REQUIRE(ir.devices[0].pins.at("drain") == "out");
    REQUIRE(ir.devices[0].pins.at("gate") == "in");
    REQUIRE(ir.devices[0].pins.at("source") == "vss");
    REQUIRE(ir.devices[0].pins.at("bulk") == "vss");
    REQUIRE(ir.devices[0].properties.at("model") == "NMOS");
    REQUIRE(ir.devices[0].properties.at("w") == "0.5u");
    REQUIRE(ir.devices[0].properties.at("l") == "0.18u");

    // PMOS
    REQUIRE(ir.devices[1].name == "P1");
    REQUIRE(ir.devices[1].type == "MOSFET");
    REQUIRE(ir.devices[1].properties.at("model") == "PMOS");

    // Resistor
    auto it_r = std::find_if(ir.devices.begin(), ir.devices.end(),
        [](const Device& d) { return d.name == "load"; });
    REQUIRE(it_r != ir.devices.end());
    REQUIRE(it_r->type == "RESISTOR");
    REQUIRE(it_r->pins.at("pos") == "out");
    REQUIRE(it_r->pins.at("neg") == "gnd");
    REQUIRE(it_r->properties.at("value") == "1k");

    // Capacitor
    auto it_cap = std::find_if(ir.devices.begin(), ir.devices.end(),
        [](const Device& d) { return d.name == "load" && d.type == "CAPACITOR"; });
    REQUIRE(it_cap != ir.devices.end());
    REQUIRE(it_cap->type == "CAPACITOR");
    REQUIRE(it_cap->properties.at("value") == "10f");

    // Voltage source
    auto it_vin = std::find_if(ir.devices.begin(), ir.devices.end(),
        [](const Device& d) { return d.name == "in" && d.type == "VOLTAGE_SOURCE"; });
    REQUIRE(it_vin != ir.devices.end());
    REQUIRE(it_vin->properties.at("value") == "DC 1.8");

    // Current source
    auto it_i = std::find_if(ir.devices.begin(), ir.devices.end(),
        [](const Device& d) { return d.type == "CURRENT_SOURCE"; });
    REQUIRE(it_i != ir.devices.end());
    REQUIRE(it_i->name == "bias"); // Ibias -> name "bias"

    // Ports
    REQUIRE(ir.ports.size() == 4);
    REQUIRE(ir.ports[0].name == "in");
    REQUIRE(ir.ports[0].direction == "INOUT");

    // Nets
    REQUIRE(!ir.nets.empty());
    auto it_net_out = std::find_if(ir.nets.begin(), ir.nets.end(),
        [](const Net& n) { return n.name == "out"; });
    REQUIRE(it_net_out != ir.nets.end());
    REQUIRE(!it_net_out->pin_names.empty()); // N1:drain, P1:drain, Rload:pos, Cload:pos
}

TEST_CASE("SpiceParser round-trips through LayoutIR JSON",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(kValidSpice);
    SpiceParser parser;
    NullCancellationToken token;

    LayoutIR ir1 = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    std::string json = layout_ir_to_json_string(ir1);
    LayoutIR ir2 = layout_ir_from_json_string(json);

    REQUIRE(ir2.design_name == "inverter");
    REQUIRE(ir2.devices.size() == 8);
    REQUIRE(ir2.ports.size() == 4);
    REQUIRE(ir2.nets.size() == ir1.nets.size());
}

TEST_CASE("SpiceParser reports error on missing .ENDS",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        ".SUBCKT missing_ends a b\n"
        "R1 a b 1k\n");
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find(".ENDS") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("SpiceParser reports error on .ENDS without .SUBCKT",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        ".ENDS foo\n");
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find(".ENDS without") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("SpiceParser reports line numbers for syntax errors",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        ".SUBCKT test a b\n"
        "R1 a\n"          // line 2: missing nodes and value
        ".ENDS test\n");
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(!cb.errors.empty());

    bool has_line_number = false;
    for (const auto& [msg, line_opt] : cb.errors) {
        if (line_opt.has_value() && line_opt.value() == 2) {
            has_line_number = true;
            break;
        }
    }
    REQUIRE(has_line_number);
}

TEST_CASE("SpiceParser rejects unknown statement",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        ".SUBCKT test a b\n"
        "D1 a b mydiode\n"  // D not supported in reduced subset
        ".ENDS test\n");
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("Unknown") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("SpiceParser respects cancellation token",
          "[parsing][SpiceParser][fast]")
{
    std::ostringstream oss;
    oss << ".SUBCKT big\n";
    for (int i = 0; i < 100; ++i) {
        oss << "R" << i << " n" << i << " n" << (i+1) << " 1k\n";
    }
    oss << ".ENDS big\n";

    auto tmp = write_temp_spice(oss.str());
    CancellationToken token;
    SpiceParser parser;
    RecordingCallbacks cb;

    token.cancel();

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());
    bool found_cancel = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("cancelled") != std::string::npos) {
            found_cancel = true;
            break;
        }
    }
    REQUIRE(found_cancel);
}

TEST_CASE("SpiceParser handles comments and empty lines",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        "* This is a comment\n"
        "  \n"
        ".SUBCKT with_comments a b\n"
        "* internal comment\n"
        "R1 a b 1k ; inline comment\n"
        "R2 a b 2k $ another comment\n"
        "C1 a b 3f // yet another\n"
        ".ENDS with_comments\n");
    SpiceParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.design_name == "with_comments");
    REQUIRE(ir.devices.size() == 3);
    REQUIRE(ir.ports.size() == 2);
}

TEST_CASE("SpiceParser handles line continuation",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        ".SUBCKT cont a b\n"
        "R1 a b 1k\n"
        "+ w=10u\n"
        "C1 a b 2f\n"
        ".ENDS cont\n");
    SpiceParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.devices.size() == 2);
    auto it_r = std::find_if(ir.devices.begin(), ir.devices.end(),
        [](const Device& d) { return d.name == "1" && d.type == "RESISTOR"; });
    REQUIRE(it_r != ir.devices.end());
    REQUIRE(it_r->properties.at("value") == "1k");
    REQUIRE(it_r->properties.at("w") == "10u");
}

TEST_CASE("SpiceParser reports continuation without preceding line",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        "+ 1k\n"
        ".SUBCKT bad a b\n"
        "R1 a b 1k\n"
        ".ENDS bad\n");
    SpiceParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("Continuation") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("SpiceParser handles no .SUBCKT (flat netlist)",
          "[parsing][SpiceParser][fast]")
{
    auto tmp = write_temp_spice(
        "R1 a b 1k\n"
        "C1 b c 10f\n"
        "V1 a 0 1.8\n");
    SpiceParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.devices.size() == 3);
    REQUIRE(ir.ports.empty());
    REQUIRE(ir.nets.size() == 4); // a, b, c, 0
}

TEST_CASE("SpiceParser handles 10K-line file acceptably",
          "[parsing][SpiceParser][slow]")
{
    std::ostringstream oss;
    oss << ".SUBCKT benchmark\n";
    for (int i = 0; i < 2500; ++i) {
        oss << "R" << i << " n" << i << " n" << (i+1) << " 1k\n";
        oss << "C" << i << " n" << i << " n" << (i+1) << " 1f\n";
        oss << "M" << i << " n" << i << " n" << (i+1)
            << " n" << (i+2) << " NMOS w=0.5u l=0.18u\n";
        oss << "V" << i << " n" << i << " 0 DC 1.8\n";
    }
    oss << ".ENDS benchmark\n";

    auto tmp = write_temp_spice(oss.str());
    SpiceParser parser;
    NullCancellationToken token;

    auto t0 = std::chrono::steady_clock::now();
    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    auto t1 = std::chrono::steady_clock::now();

    remove_temp(tmp);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    REQUIRE(ir.devices.size() == 10000);
    // .SUBCKT benchmark had no interface nodes declared, so no ports
    REQUIRE(ir.ports.empty());
    REQUIRE(ms < 10000);
}
