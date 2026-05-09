#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/lef_parser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace aegis::parsing;
using Catch::Matchers::ContainsSubstring;

namespace {

fs::path write_temp_lef(const std::string& content)
{
    const auto path = fs::temp_directory_path() /
        ("aegis_lef_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lef");
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

void remove_temp(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
}

bool has_error_code(const LefLibraryData& data, const std::string& code)
{
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("LefParser parses the shipped sample technology LEF", "[parsing][Lef][fast]")
{
    const fs::path sample = fs::path(AEGIS_SOURCE_DIR) /
        "data/import_packages/openframe_simple_design/layout/technology.lef";

    LefParser parser;
    const LefLibraryData data = parser.parse_file(sample);

    REQUIRE(!data.has_errors());
    REQUIRE(data.version == "5.7");
    REQUIRE(data.divider_char == "/");
    REQUIRE(data.bus_bit_chars == "[]");
    REQUIRE(data.macros.size() == 1);
    REQUIRE(data.properties.at("NOWIREEXTENSIONATPIN") == "ON");

    const auto& macro = data.macros.front();
    REQUIRE(macro.name == "simple_design");
    REQUIRE_THAT(macro.macro_class, ContainsSubstring("BLOCK"));
    REQUIRE(macro.foreign_name == "simple_design");
    REQUIRE(macro.width == Catch::Approx(520.0));
    REQUIRE(macro.height == Catch::Approx(520.0));
    REQUIRE(macro.pins.size() == 7);
    REQUIRE(macro.obstruction.has_value());

    const auto pin_it = std::find_if(macro.pins.begin(), macro.pins.end(), [](const LefPin& pin) {
        return pin.name == "out";
    });
    REQUIRE(pin_it != macro.pins.end());
    REQUIRE_THAT(pin_it->direction, ContainsSubstring("OUTPUT"));
    REQUIRE(pin_it->use == "SIGNAL");
    REQUIRE(pin_it->ports.size() == 1);
    REQUIRE(pin_it->ports.front().layers.size() == 1);
    REQUIRE(pin_it->ports.front().layers.front().layer_name == "met2");
    REQUIRE(pin_it->ports.front().layers.front().rects.size() == 1);

    const auto& obs = *macro.obstruction;
    REQUIRE(obs.layers.size() >= 3);
    const auto obs_met2 = std::find_if(obs.layers.begin(), obs.layers.end(), [](const LefLayerGeometry& layer) {
        return layer.layer_name == "met2";
    });
    REQUIRE(obs_met2 != obs.layers.end());
    REQUIRE(obs_met2->rects.size() >= 5);
}

TEST_CASE("LefParser parses top-level SITE LAYER VIA definitions and top-level metadata blocks", "[parsing][Lef][fast]")
{
    const char* lef = R"(
VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
PROPERTYDEFINITIONS
  MACRO LEAKAGE CURRENT REAL ;
END PROPERTYDEFINITIONS
UNITS
  DATABASE MICRONS 2000 ;
END UNITS
SITE CORE
  CLASS CORE ;
  SIZE 0.46 BY 2.72 ;
END CORE
LAYER met1
  TYPE ROUTING ;
  WIDTH 0.14 ;
END met1
VIA VIA12
  RESISTANCE 0.5 ;
  LAYER met1 ;
    RECT 1.0 1.0 0.0 0.0 ;
  LAYER via ;
    RECT 0.4 0.4 0.6 0.6 ;
END VIA12
END LIBRARY
)";

    LefParser parser;
    const LefLibraryData data = parser.parse_string(lef, "inline.lef");

    REQUIRE(!data.has_errors());
    REQUIRE(data.sites.size() == 1);
    REQUIRE(data.layers.size() == 1);
    REQUIRE(data.vias.size() == 1);
    REQUIRE(data.properties.at("UNITS.DATABASE") == "MICRONS 2000");
    REQUIRE(data.sites.front().name == "CORE");
    REQUIRE(data.sites.front().site_class == "CORE");
    REQUIRE(data.sites.front().width == Catch::Approx(0.46));
    REQUIRE(data.layers.front().name == "met1");
    REQUIRE(data.layers.front().type == "ROUTING");
    REQUIRE(data.vias.front().name == "VIA12");
    REQUIRE(data.vias.front().layers.size() == 2);
    REQUIRE(data.vias.front().layers.front().rects.size() == 1);
    REQUIRE(data.vias.front().layers.front().rects.front().x1 == Catch::Approx(0.0));
    REQUIRE(data.vias.front().layers.front().rects.front().y1 == Catch::Approx(0.0));
    REQUIRE(data.vias.front().layers.front().rects.front().x2 == Catch::Approx(1.0));
    REQUIRE(data.vias.front().layers.front().rects.front().y2 == Catch::Approx(1.0));
}

TEST_CASE("LefParser supports progress reporting and cancellation", "[parsing][Lef][fast]")
{
    std::string lef = "VERSION 5.8 ;\n";
    for (int i = 0; i < 200; ++i) {
        lef += "LAYER M" + std::to_string(i) + "\n  TYPE ROUTING ;\nEND M" + std::to_string(i) + "\n";
    }
    lef += "END LIBRARY\n";

    std::vector<Progress> progress_events;
    CancellationToken token;
    bool cancelled = false;

    LefParser parser;
    const LefLibraryData data = parser.parse_string(
        lef,
        "big.lef",
        token,
        [&](const Progress& p) {
            progress_events.push_back(p);
            if (!cancelled && p.current >= 20) {
                token.cancel();
                cancelled = true;
            }
        });

    REQUIRE(!progress_events.empty());
    REQUIRE(data.has_errors());
    REQUIRE(has_error_code(data, "LEF_PARSE_CANCELLED"));
}

TEST_CASE("LefParser reports malformed RECT with line context", "[parsing][Lef][fast]")
{
    const char* lef = R"(
MACRO bad
  PIN A
    PORT
      LAYER met1 ;
        RECT 0 0 1 ;
    END
  END A
END bad
END LIBRARY
)";

    LefParser parser;
    const LefLibraryData data = parser.parse_string(lef, "bad.lef");

    REQUIRE(data.has_errors());
    REQUIRE(has_error_code(data, "LEF_RECT_INVALID"));
    bool saw_line = false;
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.code == "LEF_RECT_INVALID" && diagnostic.line.has_value()) {
            saw_line = true;
        }
    }
    REQUIRE(saw_line);
}
