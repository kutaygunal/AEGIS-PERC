#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/def_parser.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Temp-file helpers
// ---------------------------------------------------------------------------

static fs::path write_temp_def(const std::string& content) {
    auto tmp = fs::temp_directory_path() / ("aegis_def_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".def");
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
// Recording callbacks (same pattern as test_parsing_minimal.cpp)
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
// Valid fixture: a simple NAND2-like design
// ---------------------------------------------------------------------------

static const char* kValidDef = R"(# NAND2 design in simplified DEF

DESIGN nand2

UNITS 1000

LAYER M1 metallization 0 #FF0000
LAYER DIFF diffusion 1 #00FF00

RECT M1 0 0 10 5
RECT DIFF 0 0 10 5

POLY DIFF 0 0 5 0 2.5 5

DEVICE M1 NMOS gate=nA source=GND drain=n_int w=0.5um
DEVICE M2 NMOS gate=nB source=n_int drain=nY w=0.5um
DEVICE M3 PMOS gate=nA source=nY drain=VDD w=1.0um
DEVICE M4 PMOS gate=nB source=nY drain=VDD w=1.0um

PORT A INPUT nA M1 2 2
PORT B INPUT nB M1 5 2
PORT Y OUTPUT nY M1 8 2

NET nA A width=0.5
NET nB B
NET nY Y

ANNOTATION note "initial sample" M1 1 1

END DESIGN
)";

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("DefParser format_name returns DEF-like",
          "[parsing][DefParser][fast]")
{
    DefParser parser;
    REQUIRE(parser.format_name() == "DEF-like");
}

TEST_CASE("DefParser streaming parse emits all expected events",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(kValidDef);
    DefParser parser;
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

    // 2 RECT + 1 POLY = 3 geometries
    REQUIRE(cb.geometries.size() == 3);
    REQUIRE(cb.geometries[0].layer == "M1");
    REQUIRE(cb.geometries[0].shape_type == "RECTANGLE");
    REQUIRE(cb.geometries[0].points.size() == 2);

    REQUIRE(cb.geometries[2].layer == "DIFF");
    REQUIRE(cb.geometries[2].shape_type == "POLYGON");
    REQUIRE(cb.geometries[2].points.size() == 3);

    // 4 devices -> cells
    REQUIRE(cb.cells.size() == 4);
    REQUIRE(cb.cells[0].name == "M1");
    REQUIRE(cb.cells[0].properties.at("type") == "NMOS");
    REQUIRE(cb.cells[0].properties.at("gate") == "nA");

    // 3 nets
    REQUIRE(cb.nets.size() == 3);
    REQUIRE(cb.nets[0].name == "nA");
    REQUIRE(cb.nets[0].pin_names == std::vector<std::string>{"A"});
    REQUIRE(cb.nets[0].properties.at("width") == "0.5");

    REQUIRE(cb.nets[1].name == "nB");
    REQUIRE(cb.nets[1].pin_names == std::vector<std::string>{"B"});

    // 3 ports -> pins in callback
    REQUIRE(cb.pins.size() == 3);
    REQUIRE(cb.pins[0].name == "A");
    REQUIRE(cb.pins[0].direction == "INPUT");
    REQUIRE(cb.pins[0].net_name == "nA");

    REQUIRE(cb.pins[2].name == "Y");
    REQUIRE(cb.pins[2].direction == "OUTPUT");

    // progress was reported at some point
    REQUIRE(!cb.progresses.empty());
    REQUIRE(cb.errors.empty());
}

TEST_CASE("DefParser parse_to_layout_ir produces complete LayoutIR",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(kValidDef);
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.design_name == "nand2");
    REQUIRE(ir.metadata.at("units") == "1000");
    REQUIRE(ir.layers.size() == 2);
    REQUIRE(ir.layers[0].name == "M1");
    REQUIRE(ir.layers[1].color == "#00FF00");

    REQUIRE(ir.geometries.size() == 3);

    REQUIRE(ir.devices.size() == 4);
    REQUIRE(ir.devices[0].name == "M1");
    REQUIRE(ir.devices[0].type == "NMOS");
    REQUIRE(ir.devices[0].pins.at("gate") == "nA");
    REQUIRE(ir.devices[0].properties.at("w") == "0.5um");

    REQUIRE(ir.ports.size() == 3);
    REQUIRE(ir.ports[0].name == "A");
    REQUIRE(ir.ports[0].direction == "INPUT");
    REQUIRE(ir.ports[0].net_name == "nA");
    REQUIRE(ir.ports[0].layer.has_value());
    REQUIRE(ir.ports[0].layer.value() == "M1");
    REQUIRE(ir.ports[0].location.has_value());
    REQUIRE(ir.ports[0].location->x == 2.0);
    REQUIRE(ir.ports[0].location->y == 2.0);

    REQUIRE(ir.nets.size() == 3);
    REQUIRE(ir.nets[0].name == "nA");
    REQUIRE(ir.nets[0].pin_names[0] == "A");

    REQUIRE(ir.annotations.size() == 1);
    REQUIRE(ir.annotations[0].key == "note");
    REQUIRE(ir.annotations[0].value == "initial sample");
    REQUIRE(ir.annotations[0].layer.has_value());
    REQUIRE(ir.annotations[0].layer.value() == "M1");
    REQUIRE(ir.annotations[0].position.has_value());
    REQUIRE(ir.annotations[0].position->x == 1.0);
    REQUIRE(ir.annotations[0].position->y == 1.0);
}

TEST_CASE("DefParser round-trips through LayoutIR JSON",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(kValidDef);
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir1 = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    // Serialize to JSON and back
    std::string json = layout_ir_to_json_string(ir1);
    LayoutIR ir2 = layout_ir_from_json_string(json);

    // Spot checks
    REQUIRE(ir2.design_name == "nand2");
    REQUIRE(ir2.layers.size() == 2);
    REQUIRE(ir2.geometries.size() == 3);
    REQUIRE(ir2.devices.size() == 4);
    REQUIRE(ir2.ports.size() == 3);
    REQUIRE(ir2.nets.size() == 3);
    REQUIRE(ir2.annotations.size() == 1);
}

TEST_CASE("DefParser rejects missing DESIGN",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "LAYER M1 metallization 0 #FF0000\n"
        "END DESIGN\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());
    REQUIRE(!cb.errors.empty());
    // Error should report missing DESIGN
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("DESIGN") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("DefParser rejects missing END DESIGN",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def("DESIGN test\nLAYER M1 m 0 #000\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("END DESIGN") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("DefParser reports line numbers for syntax errors",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN test\n"
        "RECT M1 bad_number 0 10 5\n"  // line 2: invalid number
        "END DESIGN\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(!cb.errors.empty());

    // At least one error should mention the line number
    bool has_line_number = false;
    for (const auto& [msg, line_opt] : cb.errors) {
        if (line_opt.has_value() && line_opt.value() == 2) {
            has_line_number = true;
            break;
        }
    }
    REQUIRE(has_line_number);
}

TEST_CASE("DefParser reports error on unknown command",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN test\n"
        "UNKNOWN_CMD foo bar\n"
        "END DESIGN\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(!cb.errors.empty());
    bool found = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("Unknown command") != std::string::npos) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("DefParser respects cancellation token",
          "[parsing][DefParser][fast]")
{
    // Build a large enough file so cancellation kicks in
    std::ostringstream oss;
    oss << "DESIGN big\n";
    for (int i = 0; i < 50; ++i) {
        oss << "RECT M1 0 0 10 5\n";
    }
    oss << "END DESIGN\n";

    auto tmp = write_temp_def(oss.str());

    CancellationToken token;
    DefParser parser;
    RecordingCallbacks cb;

    // Cancel immediately
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

TEST_CASE("DefParser handles empty and comment-only files gracefully",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "# This is a comment\n"
        "   \n"
        "; another comment style would fail\n"
        "DESIGN empty\n"
        "END DESIGN\n");
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.design_name == "empty");
    REQUIRE(ir.layers.empty());
    REQUIRE(ir.geometries.empty());
    REQUIRE(ir.devices.empty());
}

TEST_CASE("DefParser optional port layer is omitted when not provided",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN optional\n"
        "PORT X INPUT nX 1.0 2.0\n"      // 6 tokens: no layer
        "PORT Y OUTPUT nY M1 3.0 4.0\n"  // 7 tokens: with layer
        "END DESIGN\n");
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.ports.size() == 2);
    REQUIRE(!ir.ports[0].layer.has_value());
    REQUIRE(ir.ports[0].location.has_value());
    REQUIRE(ir.ports[0].location->x == 1.0);

    REQUIRE(ir.ports[1].layer.has_value());
    REQUIRE(ir.ports[1].layer.value() == "M1");
    REQUIRE(ir.ports[1].location->x == 3.0);
}

TEST_CASE("DefParser optional annotation layer is omitted when not provided",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN anno\n"
        "ANNOTATION key1 value1 10.0 20.0\n"       // no layer
        "ANNOTATION key2 value2 LAYER1 30.0 40.0\n" // with layer
        "END DESIGN\n");
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.annotations.size() == 2);
    REQUIRE(!ir.annotations[0].layer.has_value());
    REQUIRE(ir.annotations[0].position->x == 10.0);

    REQUIRE(ir.annotations[1].layer.has_value());
    REQUIRE(ir.annotations[1].layer.value() == "LAYER1");
    REQUIRE(ir.annotations[1].position->x == 30.0);
}

TEST_CASE("DefParser handles quoted annotation values",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN quoted\n"
        "ANNOTATION note \"multi word value\" M1 1 1\n"
        "END DESIGN\n");
    DefParser parser;
    NullCancellationToken token;

    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.annotations.size() == 1);
    REQUIRE(ir.annotations[0].value == "multi word value");
}

TEST_CASE("DefParser rejects malformed RECT",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN badrect\n"
        "RECT M1 0 0 10\n"  // missing height
        "END DESIGN\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(!cb.errors.empty());
}

TEST_CASE("DefParser rejects malformed POLY",
          "[parsing][DefParser][fast]")
{
    auto tmp = write_temp_def(
        "DESIGN badpoly\n"
        "POLY DIFF 0 0 5\n"  // odd number of coordinates after layer
        "END DESIGN\n");
    DefParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(!ok);
    REQUIRE(!cb.errors.empty());
}

TEST_CASE("DefParser handles 10K-line file acceptably",
          "[parsing][DefParser][slow]")
{
    std::ostringstream oss;
    oss << "DESIGN benchmark\n";
    for (int i = 0; i < 2500; ++i) {
        oss << "RECT M1 " << i*10 << " 0 10 5\n";
        oss << "POLY DIFF 0 0 5 0 2.5 5\n";
        oss << "DEVICE M" << i << " NMOS gate=n" << i
            << " source=GND drain=nY w=0.5um\n";
        oss << "NET n" << i << " A" << i << " B" << i
            << " width=0.5\n";
    }
    oss << "END DESIGN\n";

    auto tmp = write_temp_def(oss.str());
    DefParser parser;
    NullCancellationToken token;

    auto t0 = std::chrono::steady_clock::now();
    LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    auto t1 = std::chrono::steady_clock::now();

    remove_temp(tmp);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // Sanity: we should have ~10K lines and it should parse in under 5 seconds
    REQUIRE(ir.geometries.size() == 5000);
    REQUIRE(ir.devices.size() == 2500);
    REQUIRE(ir.nets.size() == 2500);
    REQUIRE(ms < 5000);
}
