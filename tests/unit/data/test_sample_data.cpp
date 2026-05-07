#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static fs::path sample_dir()
{
    // Prefer compile-time source directory if available (CI)
#ifdef AEGIS_SOURCE_DIR
    fs::path dir = fs::path(AEGIS_SOURCE_DIR) / "data" / "sample_designs";
    if (fs::exists(dir / "schema.json")) {
        return dir;
    }
#endif
    // Fall back to relative path from build tree
    fs::path candidate = fs::path("") / ".." / ".." / ".." / ".." / "data" / "sample_designs";
    if (fs::exists(candidate / "schema.json")) {
        return candidate;
    }
    return fs::path("") / ".." / ".." / ".." / "data" / "sample_designs";
}

static json load_json(const fs::path& path)
{
    std::ifstream f(path);
    REQUIRE(f.is_open());
    json j;
    f >> j;
    return j;
}

static void validate_common(const json& doc, const char* name)
{
    REQUIRE(doc.contains("format_version"));
    REQUIRE(doc["format_version"].get<std::string>() == "1.0.0");

    REQUIRE(doc.contains("design"));
    REQUIRE(doc["design"]["name"].get<std::string>() == name);
    REQUIRE(!doc["design"]["technology"].get<std::string>().empty());

    REQUIRE(doc.contains("layout"));
    REQUIRE(doc["layout"].contains("layers"));
    REQUIRE(doc["layout"]["layers"].is_array());
    REQUIRE(doc["layout"]["layers"].size() >= 1);
    REQUIRE(doc["layout"].contains("shapes"));
    REQUIRE(doc["layout"]["shapes"].is_array());
    REQUIRE(doc["layout"]["shapes"].size() >= 1);

    for (const auto& shape : doc["layout"]["shapes"]) {
        REQUIRE(shape.contains("layer"));
        REQUIRE(shape.contains("primitive"));
        std::string prim = shape["primitive"].get<std::string>();
        REQUIRE((prim == "rectangle" || prim == "polygon"));
        if (prim == "rectangle") {
            REQUIRE(shape.contains("bbox"));
            REQUIRE(shape["bbox"].size() == 4);
        } else {
            REQUIRE(shape.contains("points"));
            REQUIRE(shape["points"].size() >= 3);
        }
    }

    REQUIRE(doc.contains("netlist"));
    REQUIRE(doc["netlist"].contains("devices"));
    REQUIRE(doc["netlist"]["devices"].is_array());
    REQUIRE(doc["netlist"].contains("nets"));
    REQUIRE(doc["netlist"]["nets"].is_array());
    REQUIRE(doc["netlist"].contains("ports"));
    REQUIRE(doc["netlist"]["ports"].is_array());
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_CASE("Sample data directory exists and contains schema", "[data][p1-008][SampleData]")
{
    fs::path dir = sample_dir();
    REQUIRE(fs::exists(dir / "schema.json"));
    REQUIRE(fs::exists(dir / "inverter.json"));
    REQUIRE(fs::exists(dir / "nand2.json"));
    REQUIRE(fs::exists(dir / "ring_oscillator.json"));
    REQUIRE(fs::exists(dir / "README.md"));
}

TEST_CASE("Inverter sample validates against schema structure", "[data][p1-008][SampleData]")
{
    auto doc = load_json(sample_dir() / "inverter.json");
    validate_common(doc, "inverter");

    REQUIRE(doc["netlist"]["devices"].size() == 2);
    REQUIRE(doc["netlist"]["nets"].size() == 4);
    REQUIRE(doc["netlist"]["ports"].size() == 4);

    // Verify PMOS/NMOS present
    bool has_pmos = false, has_nmos = false;
    for (const auto& dev : doc["netlist"]["devices"]) {
        std::string t = dev["type"].get<std::string>();
        if (t == "pmos") has_pmos = true;
        if (t == "nmos") has_nmos = true;
    }
    REQUIRE(has_pmos);
    REQUIRE(has_nmos);

    // Verify input/output ports
    bool has_in = false, has_out = false;
    for (const auto& port : doc["netlist"]["ports"]) {
        std::string n = port["name"].get<std::string>();
        std::string dir = port["direction"].get<std::string>();
        if (n == "in" && dir == "input") has_in = true;
        if (n == "out" && dir == "output") has_out = true;
    }
    REQUIRE(has_in);
    REQUIRE(has_out);
}

TEST_CASE("NAND2 sample validates against schema structure", "[data][p1-008][SampleData]")
{
    auto doc = load_json(sample_dir() / "nand2.json");
    validate_common(doc, "nand2");

    REQUIRE(doc["netlist"]["devices"].size() == 4);
    REQUIRE(doc["netlist"]["nets"].size() == 6);
    REQUIRE(doc["netlist"]["ports"].size() == 5);

    // Count transistor types
    int pmos_count = 0, nmos_count = 0;
    for (const auto& dev : doc["netlist"]["devices"]) {
        std::string t = dev["type"].get<std::string>();
        if (t == "pmos") ++pmos_count;
        if (t == "nmos") ++nmos_count;
    }
    REQUIRE(pmos_count == 2);
    REQUIRE(nmos_count == 2);

    // Verify two inputs
    int input_ports = 0;
    for (const auto& port : doc["netlist"]["ports"]) {
        if (port["direction"].get<std::string>() == "input") ++input_ports;
    }
    REQUIRE(input_ports == 2);
}

TEST_CASE("Ring oscillator sample validates against schema structure", "[data][p1-008][SampleData]")
{
    auto doc = load_json(sample_dir() / "ring_oscillator.json");
    validate_common(doc, "ring_oscillator");

    REQUIRE(doc["netlist"]["devices"].size() == 10);
    REQUIRE(doc["netlist"]["nets"].size() == 8);
    REQUIRE(doc["netlist"]["ports"].size() == 3);

    // Verify polygon shapes present
    bool has_polygon = false;
    for (const auto& shape : doc["layout"]["shapes"]) {
        if (shape["primitive"].get<std::string>() == "polygon") {
            has_polygon = true;
            break;
        }
    }
    REQUIRE(has_polygon);
}
