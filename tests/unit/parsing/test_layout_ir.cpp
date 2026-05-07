#include <catch2/catch_test_macros.hpp>
#include "aegis/parsing/layout_ir.hpp"

using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Helper: construct a fully-populated IR for round-trip testing
// ---------------------------------------------------------------------------

static LayoutIR make_full_ir()
{
    LayoutIR ir;
    ir.version       = LayoutIR::CURRENT_VERSION;
    ir.design_name   = "nand2";
    ir.description   = "2-input NAND gate";

    Layer l1{"M1", "metallisation", 0, "#FF0000"};
    Layer l2{"DIFF", "diffusion", 1, "#00FF00"};
    ir.layers.push_back(l1);
    ir.layers.push_back(l2);

    Geometry g1{"M1", Rectangle{0.0, 0.0, 10.0, 5.0}};
    Geometry g2{"DIFF", Polygon{{{0.0, 0.0}, {5.0, 0.0}, {2.5, 5.0}}}};
    ir.geometries.push_back(g1);
    ir.geometries.push_back(g2);

    Net n1{"nA", std::vector<std::string>{"A"}, std::map<std::string, std::string>{{"width", "0.5"}}};
    Net n2{"nB", std::vector<std::string>{"B"}, {}};
    Net n3{"nY", std::vector<std::string>{"Y"}, std::map<std::string, std::string>{{"width", "0.7"}}};
    ir.nets.push_back(n1);
    ir.nets.push_back(n2);
    ir.nets.push_back(n3);

    Device d1{"M1", "NMOS",
              std::map<std::string, std::string>{{"gate", "nA"}, {"source", "GND"}, {"drain", "n_int"}},
              std::map<std::string, std::string>{{"w", "0.5um"}}};
    Device d2{"M2", "NMOS",
              std::map<std::string, std::string>{{"gate", "nB"}, {"source", "n_int"}, {"drain", "nY"}},
              std::map<std::string, std::string>{{"w", "0.5um"}}};
    Device d3{"M3", "PMOS",
              std::map<std::string, std::string>{{"gate", "nA"}, {"source", "nY"}, {"drain", "VDD"}},
              std::map<std::string, std::string>{{"w", "1.0um"}}};
    Device d4{"M4", "PMOS",
              std::map<std::string, std::string>{{"gate", "nB"}, {"source", "nY"}, {"drain", "VDD"}},
              std::map<std::string, std::string>{{"w", "1.0um"}}};
    ir.devices.push_back(d1);
    ir.devices.push_back(d2);
    ir.devices.push_back(d3);
    ir.devices.push_back(d4);

    Port p1{"A",  "INPUT",  "nA",  std::string{"M1"}, Point{2.0, 2.0}};
    Port p2{"B",  "INPUT",  "nB",  std::string{"M1"}, Point{5.0, 2.0}};
    Port p3{"Y",  "OUTPUT", "nY",  std::string{"M1"}, Point{8.0, 2.0}};
    ir.ports.push_back(p1);
    ir.ports.push_back(p2);
    ir.ports.push_back(p3);

    Annotation a1{"note", "initial sample", std::string{"M1"}, Point{1.0, 1.0}};
    ir.annotations.push_back(a1);

    ir.metadata = std::map<std::string, std::string>{{"tech", "65nm"}, {"foundry", "tsmc"}};
    return ir;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Empty IR round-trips through JSON",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "empty_test";
    ir_in.description = "A design with no content";

    const std::string json = layout_ir_to_json_string(ir_in);
    REQUIRE(!json.empty());

    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);
}

TEST_CASE("Full IR round-trips through JSON",
          "[parsing][LayoutIR][fast]")
{
    const auto ir_in = make_full_ir();
    const std::string json = layout_ir_to_json_string(ir_in);
    REQUIRE(!json.empty());

    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);

    // Spot-check a few nested values
    REQUIRE(ir_out.layers.size() == 2);
    REQUIRE(ir_out.layers[0].color == "#FF0000");
    REQUIRE(ir_out.layers[1].order  == 1);

    REQUIRE(ir_out.geometries.size() == 2);
    REQUIRE(ir_out.geometries[0].layer == "M1");
    REQUIRE(std::holds_alternative<Rectangle>(ir_out.geometries[0].shape));
    const auto& r = std::get<Rectangle>(ir_out.geometries[0].shape);
    REQUIRE(r.width  == 10.0);
    REQUIRE(r.height == 5.0);

    REQUIRE(std::holds_alternative<Polygon>(ir_out.geometries[1].shape));
    const auto& p = std::get<Polygon>(ir_out.geometries[1].shape);
    REQUIRE(p.points.size() == 3);
    REQUIRE(p.points[1] == Point{5.0, 0.0});

    REQUIRE(ir_out.devices.size() == 4);
    REQUIRE(ir_out.devices[0].pins.at("gate") == "nA");
}

TEST_CASE("Rectangle geometry round-trip",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "rect_only";
    ir_in.description = "";
    ir_in.geometries.push_back({"M2", Rectangle{1.0, 2.0, 3.0, 4.0}});

    const auto json = layout_ir_to_json_string(ir_in);
    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);
    REQUIRE(std::holds_alternative<Rectangle>(ir_out.geometries[0].shape));
}

TEST_CASE("Polygon geometry round-trip",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "poly_only";
    ir_in.description = "";
    ir_in.geometries.push_back(
        {"POLY", Polygon{{{0.0, 0.0}, {1.0, 0.0}, {0.5, 1.0}, {0.0, 0.0}}}});

    const auto json = layout_ir_to_json_string(ir_in);
    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);
    REQUIRE(std::holds_alternative<Polygon>(ir_out.geometries[0].shape));
}

TEST_CASE("Unsupported version is rejected",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir;
    ir.version     = 99; // unsupported
    ir.design_name = "future";
    ir.description = "";

    const auto json = layout_ir_to_json_string(ir);
    REQUIRE_THROWS_AS(layout_ir_from_json_string(json), LayoutIRError);
}

TEST_CASE("Invalid JSON is rejected",
          "[parsing][LayoutIR][fast]")
{
    REQUIRE_THROWS_AS(layout_ir_from_json_string("{garbage"), LayoutIRError);
}

TEST_CASE("Missing required fields are rejected",
          "[parsing][LayoutIR][fast]")
{
    // Missing version
    REQUIRE_THROWS_AS(
        layout_ir_from_json_string(R"({"design_name":"x","description":"","layers":[],"geometries":[],"nets":[],"devices":[],"ports":[],"annotations":[],"metadata":{}})"),
        LayoutIRError);

    // Missing design_name
    REQUIRE_THROWS_AS(
        layout_ir_from_json_string(R"({"version":1,"description":"","layers":[],"geometries":[],"nets":[],"devices":[],"ports":[],"annotations":[],"metadata":{}})"),
        LayoutIRError);

    // Missing layers array
    REQUIRE_THROWS_AS(
        layout_ir_from_json_string(R"({"version":1,"design_name":"x","description":"","geometries":[],"nets":[],"devices":[],"ports":[],"annotations":[],"metadata":{}})"),
        LayoutIRError);
}

TEST_CASE("Schema validation rejects bad geometry shape type",
          "[parsing][LayoutIR][fast]")
{
    const std::string bad_json = R"({
        "version": 1,
        "design_name": "bad_shape",
        "description": "",
        "layers": [],
        "geometries": [
            {
                "layer": "M1",
                "shape": {"type": "ellipse", "rx": 5, "ry": 3}
            }
        ],
        "nets": [],
        "devices": [],
        "ports": [],
        "annotations": [],
        "metadata": {}
    })";

    REQUIRE_THROWS_AS(layout_ir_from_json_string(bad_json), LayoutIRError);
}

TEST_CASE("Schema validation rejects missing layer fields",
          "[parsing][LayoutIR][fast]")
{
    const std::string bad_json = R"({
        "version": 1,
        "design_name": "bad_layer",
        "description": "",
        "layers": [{"name":"M1"}],
        "geometries": [],
        "nets": [],
        "devices": [],
        "ports": [],
        "annotations": [],
        "metadata": {}
    })";

    REQUIRE_THROWS_AS(layout_ir_from_json_string(bad_json), LayoutIRError);
}

TEST_CASE("Schema validation rejects missing device fields",
          "[parsing][LayoutIR][fast]")
{
    const std::string bad_json = R"({
        "version": 1,
        "design_name": "bad_dev",
        "description": "",
        "layers": [],
        "geometries": [],
        "nets": [],
        "devices": [{"type":"NMOS"}],
        "ports": [],
        "annotations": [],
        "metadata": {}
    })";

    REQUIRE_THROWS_AS(layout_ir_from_json_string(bad_json), LayoutIRError);
}

TEST_CASE("Port optional fields round-trip",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "opt_port";
    ir_in.description = "";

    Port p1{"A", "INPUT", "nA", std::nullopt, std::nullopt};
    Port p2{"B", "INPUT", "nB", std::string{"M1"}, Point{3.0, 4.0}};
    ir_in.ports.push_back(p1);
    ir_in.ports.push_back(p2);

    const auto json  = layout_ir_to_json_string(ir_in);
    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);
    REQUIRE(!ir_out.ports[0].layer.has_value());
    REQUIRE(ir_out.ports[1].layer.has_value());
}

TEST_CASE("Annotation optional fields round-trip",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "opt_anno";
    ir_in.description = "";

    Annotation a1{"key1", "val1", std::nullopt, std::nullopt};
    Annotation a2{"key2", "val2", std::string{"M2"}, Point{7.0, 8.0}};
    ir_in.annotations.push_back(a1);
    ir_in.annotations.push_back(a2);

    const auto json  = layout_ir_to_json_string(ir_in);
    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out == ir_in);
}

TEST_CASE("Metadata round-trip",
          "[parsing][LayoutIR][fast]")
{
    LayoutIR ir_in;
    ir_in.version     = LayoutIR::CURRENT_VERSION;
    ir_in.design_name = "meta";
    ir_in.description = "";
    ir_in.metadata    = {{"key1", "val1"}, {"key2", "val2"}};

    const auto json  = layout_ir_to_json_string(ir_in);
    const auto ir_out = layout_ir_from_json_string(json);
    REQUIRE(ir_out.metadata.size() == 2);
    REQUIRE(ir_out.metadata.at("key1") == "val1");
    REQUIRE(ir_out.metadata.at("key2") == "val2");
}

TEST_CASE("IR version constant is current",
          "[parsing][LayoutIR][fast]")
{
    REQUIRE(LayoutIR::CURRENT_VERSION == 1);
}
