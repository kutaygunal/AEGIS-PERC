#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/lef_parser.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
using namespace aegis::parsing;
using Catch::Matchers::ContainsSubstring;

namespace {

bool has_diagnostic_code(const LefLibraryData& data, const std::string& code)
{
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

std::string make_large_lef(std::size_t layer_count, std::size_t macro_count, std::size_t pins_per_macro)
{
    std::string lef;
    lef.reserve(layer_count * 128 + macro_count * pins_per_macro * 192);
    lef += "VERSION 5.8 ;\n";
    lef += "UNITS\n  DATABASE MICRONS 2000 ;\nEND UNITS\n";

    for (std::size_t i = 0; i < layer_count; ++i) {
        lef += "LAYER M" + std::to_string(i) + "\n";
        lef += "  TYPE ROUTING ;\n";
        lef += "  WIDTH 0.10 ;\n";
        lef += "  PITCH 0.20 0.20 ;\n";
        lef += "  DIRECTION HORIZONTAL ;\n";
        lef += "END M" + std::to_string(i) + "\n";
    }

    for (std::size_t macro_index = 0; macro_index < macro_count; ++macro_index) {
        lef += "MACRO MACRO_" + std::to_string(macro_index) + "\n";
        lef += "  CLASS BLOCK ;\n";
        lef += "  ORIGIN 0 0 ;\n";
        lef += "  SIZE 100 BY 200 ;\n";
        lef += "  SITE CORE ;\n";
        for (std::size_t pin_index = 0; pin_index < pins_per_macro; ++pin_index) {
            lef += "  PIN P" + std::to_string(pin_index) + "\n";
            lef += "    DIRECTION INPUT ;\n";
            lef += "    USE SIGNAL ;\n";
            lef += "    PORT\n";
            lef += "      LAYER M" + std::to_string(pin_index % std::max<std::size_t>(std::size_t{1}, layer_count)) + " ;\n";
            lef += "        RECT 0 0 1 1 ;\n";
            lef += "        PATH 0 0 10 0 10 5 ;\n";
            lef += "    END\n";
            lef += "  END P" + std::to_string(pin_index) + "\n";
        }
        lef += "  OBS\n";
        lef += "    LAYER M0 ;\n";
        lef += "      RECT 0 0 50 50 ;\n";
        lef += "      POLYGON 0 0 10 0 10 10 0 10 ;\n";
        lef += "  END\n";
        lef += "END MACRO_" + std::to_string(macro_index) + "\n";
    }

    lef += "END LIBRARY\n";
    return lef;
}

std::string mutate_text_deterministically(std::string text, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> action_dist(0, 5);
    std::uniform_int_distribution<int> ascii_dist(32, 126);

    const std::size_t mutation_count = std::max<std::size_t>(8, text.size() / 32);
    for (std::size_t i = 0; i < mutation_count; ++i) {
        const int action = action_dist(rng);
        const char injected = static_cast<char>(ascii_dist(rng));
        const std::size_t pos = text.empty() ? 0 : static_cast<std::size_t>(rng() % text.size());

        switch (action) {
        case 0:
            text.insert(text.begin() + static_cast<std::ptrdiff_t>(pos), injected);
            break;
        case 1:
            if (!text.empty()) {
                text.erase(text.begin() + static_cast<std::ptrdiff_t>(pos));
            }
            break;
        case 2:
            if (!text.empty()) {
                text[pos] = injected;
            }
            break;
        case 3:
            text.insert(pos, ";\n");
            break;
        case 4:
            text.insert(pos, " END ");
            break;
        case 5:
            text.insert(pos, " POLYGON 0 0 1 ; ");
            break;
        }
    }

    return text;
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
  PITCH 0.20 ;
  DIRECTION HORIZONTAL ;
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
    REQUIRE(data.properties.at("PROPERTYDEF.MACRO.LEAKAGE") == "CURRENT REAL");
    REQUIRE(data.sites.front().name == "CORE");
    REQUIRE(data.sites.front().site_class == "CORE");
    REQUIRE(data.sites.front().width == Catch::Approx(0.46));
    REQUIRE(data.layers.front().name == "met1");
    REQUIRE(data.layers.front().type == "ROUTING");
    REQUIRE(data.layers.front().type_kind == LefLayerType::Routing);
    REQUIRE(data.layers.front().width_value == Catch::Approx(0.14));
    REQUIRE(data.layers.front().pitch->x == Catch::Approx(0.20));
    REQUIRE(data.layers.front().routing_direction == LefRoutingDirection::Horizontal);
    REQUIRE(data.vias.front().name == "VIA12");
    REQUIRE(data.vias.front().layers.size() == 2);
    REQUIRE(data.vias.front().layers.front().rects.size() == 1);
    REQUIRE(data.vias.front().layers.front().rects.front().x1 == Catch::Approx(0.0));
    REQUIRE(data.vias.front().layers.front().rects.front().y1 == Catch::Approx(0.0));
    REQUIRE(data.vias.front().layers.front().rects.front().x2 == Catch::Approx(1.0));
    REQUIRE(data.vias.front().layers.front().rects.front().y2 == Catch::Approx(1.0));
}

TEST_CASE("LefParser normalizes mixed advanced geometry forms across VIA PORT and OBS contexts", "[parsing][Lef][fast]")
{
    const char* lef = R"(
VERSION 5.8 ;
MACRO NAND2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1.4 BY 2.8 ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER M2 ;
        WIDTH 0.15 ;
        RECT 0 0 0.4 0.2 ;
        POLYGON 0 0 1 0 1 1 0 1 ;
        PATH 0 0 2 0 2 1 ;
        VIA 5 6 VIA23 R90 ;
    END
  END A
  OBS
    LAYER M3 ;
      WIDTH 0.20 ;
      PATH 10 10 11 10 11 12 ;
      POLYGON 2 2 4 2 4 4 2 4 ;
      VIA 3 4 VOBS ;
      RECT 0.5 0.5 1.5 1.5 ;
  END
END NAND2
VIA VIA23 DEFAULT
  LAYER M2 ;
    WIDTH 0.11 ;
    PATH 0 0 1 0 ;
    POLYGON 0 0 1 0 1 1 ;
    RECT 0 0 1 1 ;
  LAYER CUT ;
    VIA 0 0 CUT12 ;
END VIA23
END LIBRARY
)";

    LefParser parser;
    const LefLibraryData data = parser.parse_string(lef, "geometry.lef");

    REQUIRE(!data.has_errors());
    REQUIRE(data.macros.size() == 1);
    REQUIRE(data.vias.size() == 1);

    const auto& port_layer = data.macros.front().pins.front().ports.front().layers.front();
    REQUIRE(port_layer.layer_name == "M2");
    REQUIRE(port_layer.path_width_hint == Catch::Approx(0.15));
    REQUIRE(port_layer.rects.size() == 1);
    REQUIRE(port_layer.polygons.size() == 1);
    REQUIRE(port_layer.paths.size() == 1);
    REQUIRE(port_layer.via_placements.size() == 1);
    REQUIRE(port_layer.polygons.front().points.size() == 4);
    REQUIRE(port_layer.paths.front().points.size() == 3);
    REQUIRE(port_layer.paths.front().width == Catch::Approx(0.15));
    REQUIRE(port_layer.via_placements.front().via_name == "VIA23");
    REQUIRE(port_layer.via_placements.front().orientation == "R90");
    REQUIRE(port_layer.rects.front().source_line > 0);
    REQUIRE(port_layer.polygons.front().source_line > 0);
    REQUIRE(port_layer.paths.front().source_line > 0);
    REQUIRE(port_layer.via_placements.front().source_line > 0);

    const auto& obs_layer = data.macros.front().obstruction->layers.front();
    REQUIRE(obs_layer.layer_name == "M3");
    REQUIRE(obs_layer.path_width_hint == Catch::Approx(0.20));
    REQUIRE(obs_layer.rects.size() == 1);
    REQUIRE(obs_layer.polygons.size() == 1);
    REQUIRE(obs_layer.paths.size() == 1);
    REQUIRE(obs_layer.via_placements.size() == 1);
    REQUIRE(obs_layer.paths.front().width == Catch::Approx(0.20));

    const auto& via_layer = data.vias.front().layers.front();
    REQUIRE(via_layer.layer_name == "M2");
    REQUIRE(via_layer.path_width_hint == Catch::Approx(0.11));
    REQUIRE(via_layer.rects.size() == 1);
    REQUIRE(via_layer.polygons.size() == 1);
    REQUIRE(via_layer.paths.size() == 1);
    REQUIRE(via_layer.paths.front().width == Catch::Approx(0.11));
    REQUIRE(data.vias.front().layers[1].via_placements.size() == 1);
}

TEST_CASE("LefParser supports broader LEF 5.x statements with multiline logical statements", "[parsing][Lef][fast]")
{
    const char* lef = R"(
VERSION 5.8 ;
NAMESCASESENSITIVE ON ;
FIXEDMASK ;
CLEARANCEMEASURE EUCLIDEAN ;
USEMINSPACING OBS ON ;
PROPERTYDEFINITIONS
  MACRO LEAKAGE CURRENT REAL ;
  PIN OWNER STRING ;
END PROPERTYDEFINITIONS
UNITS
  TIME NANOSECONDS 1 ;
  DATABASE MICRONS 2000 ;
END UNITS
SITE CORE
  CLASS CORE ;
  SYMMETRY X Y R90 ;
  SIZE
    0.46 BY
    2.72 ;
END CORE
LAYER M2
  TYPE ROUTING ;
  WIDTH 0.10 ;
  DIRECTION VERTICAL ;
  PITCH 0.4 0.5 ;
  OFFSET 0.1 0.2 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0 0.1
    WIDTH 0.1 0.2 0.3 ;
END M2
VIA VIA23 DEFAULT
  RESISTANCE 1.2 ;
  TOPOFSTACKONLY ;
  LAYER M2 ;
    RECT 0 0 1 1 ;
END VIA23
VIARULE VR1
  GENERATE ;
  CUTSIZE 0.2 0.2 ;
END VR1
NONDEFAULTRULE WIDE_M2
  HARDSPACING ;
  LAYER M2 ;
    WIDTH 0.8 ;
END WIDE_M2
MACRO NAND2
  CLASS CORE SPACER ;
  SOURCE USER ;
  FOREIGN NAND2 0 0 ;
  ORIGIN 0 0 ;
  SIZE 1.4 BY 2.8 ;
  SYMMETRY X Y ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    SHAPE ABUTMENT ;
    ANTENNAMODEL OXIDE1 ;
    ANTENNAGATEAREA 0.12 ;
    PORT
      CLASS CORE ;
      LAYER M2 ;
        RECT 0 0 0.4 0.2 ;
        POLYGON 0 0 1 0 1 1 ;
    END
  END A
  OBS
    LAYER M2 ;
      PATH 0 0 1 1 ;
      RECT 0.5 0.5 1.5 1.5 ;
  END
END NAND2
END LIBRARY
)";

    LefParser parser;
    const LefLibraryData data = parser.parse_string(lef, "broader.lef");

    REQUIRE(!data.has_errors());
    REQUIRE(data.properties.at("NAMESCASESENSITIVE") == "ON");
    REQUIRE(data.properties.at("FIXEDMASK") == "ON");
    REQUIRE(data.properties.at("CLEARANCEMEASURE") == "EUCLIDEAN");
    REQUIRE(data.properties.at("USEMINSPACING") == "OBS ON");
    REQUIRE(data.properties.at("PROPERTYDEF.PIN.OWNER") == "STRING");
    REQUIRE(data.properties.at("UNITS.TIME") == "NANOSECONDS 1");
    REQUIRE(data.sites.front().width == Catch::Approx(0.46));
    REQUIRE(data.sites.front().height == Catch::Approx(2.72));
    REQUIRE(data.sites.front().properties.at("SYMMETRY") == "X Y R90");
    REQUIRE(data.layers.front().properties.at("PITCH") == "0.4 0.5");
    REQUIRE(data.layers.front().properties.at("SPACINGTABLE") == "PARALLELRUNLENGTH 0 0.1 WIDTH 0.1 0.2 0.3");
    REQUIRE(data.layers.front().type_kind == LefLayerType::Routing);
    REQUIRE(data.layers.front().routing_direction == LefRoutingDirection::Vertical);
    REQUIRE(data.layers.front().width_value == Catch::Approx(0.10));
    REQUIRE(data.layers.front().pitch->x == Catch::Approx(0.4));
    REQUIRE(data.layers.front().pitch->y == Catch::Approx(0.5));
    REQUIRE(data.vias.front().properties.at("HEADER") == "DEFAULT");
    REQUIRE(data.vias.front().properties.at("TOPOFSTACKONLY") == "");
    REQUIRE(data.vias.front().is_default);
    REQUIRE(data.vias.front().resistance == Catch::Approx(1.2));
    REQUIRE(data.macros.size() == 1);
    REQUIRE(data.macros.front().properties.at("SOURCE") == "USER");
    REQUIRE(data.macros.front().properties.at("SITE") == "CORE");
    REQUIRE(data.macros.front().pins.size() == 1);
    REQUIRE(data.macros.front().class_kind == LefMacroClass::Core);
    REQUIRE(data.macros.front().source == "USER");
    REQUIRE(data.macros.front().site_name == "CORE");
    REQUIRE(data.macros.front().pins.front().properties.at("SHAPE") == "ABUTMENT");
    REQUIRE(data.macros.front().pins.front().properties.at("ANTENNAGATEAREA") == "0.12");
    REQUIRE(data.macros.front().pins.front().direction_kind == LefPinDirection::Input);
    REQUIRE(data.macros.front().pins.front().use_kind == LefPinUse::Signal);
    REQUIRE(data.macros.front().pins.front().shape == "ABUTMENT");
    REQUIRE(data.macros.front().pins.front().antenna_gate_area == Catch::Approx(0.12));
    REQUIRE(data.macros.front().pins.front().properties.at("PORT_CLASS") == "CORE");
    REQUIRE(data.macros.front().pins.front().ports.front().layers.front().polygons.size() == 1);
    REQUIRE(data.macros.front().obstruction.has_value());
    REQUIRE(data.macros.front().obstruction->layers.front().paths.size() == 1);
    REQUIRE(data.via_rules.size() == 1);
    REQUIRE(data.via_rules.front().name == "VR1");
    REQUIRE(data.via_rules.front().generate);
    REQUIRE(data.non_default_rules.size() == 1);
    REQUIRE(data.non_default_rules.front().name == "WIDE_M2");
    REQUIRE(data.non_default_rules.front().hard_spacing);
}

TEST_CASE("LefParser reports malformed geometry and incomplete semantics with line context", "[parsing][Lef][fast]")
{
    const char* lef = R"(
SITE BADSITE
  CLASS UNKNOWNCLASS ;
END BADSITE
LAYER M1
  TYPE ROUTING ;
END M1
VIA V1
END V1
VIARULE VR_BAD
END VR_BAD
MACRO bad
  PIN A
    USE MYSTERY ;
    PORT
      LAYER met1 ;
        RECT 0 0 1 ;
        POLYGON 0 0 1 0 1 ;
        PATH 0 0 1 ;
        VIA 0 only_two_tokens ;
    END
  END A
END bad
END LIBRARY
)";

    LefParser parser;
    const LefLibraryData data = parser.parse_string(lef, "bad.lef");

    REQUIRE(data.has_errors());
    REQUIRE(has_diagnostic_code(data, "LEF_RECT_INVALID"));
    REQUIRE(has_diagnostic_code(data, "LEF_POLYGON_INVALID"));
    REQUIRE(has_diagnostic_code(data, "LEF_PATH_INVALID"));
    REQUIRE(has_diagnostic_code(data, "LEF_VIA_GEOMETRY_INVALID"));
    REQUIRE(has_diagnostic_code(data, "LEF_SITE_CLASS_UNKNOWN"));
    REQUIRE(has_diagnostic_code(data, "LEF_SITE_SIZE_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_LAYER_ROUTING_WIDTH_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_LAYER_ROUTING_PITCH_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_LAYER_ROUTING_DIRECTION_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_VIA_LAYERS_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_VIARULE_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_PIN_DIRECTION_INCOMPLETE"));
    REQUIRE(has_diagnostic_code(data, "LEF_PIN_USE_UNKNOWN"));
    bool saw_line = false;
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.line.has_value()) {
            saw_line = true;
            break;
        }
    }
    REQUIRE(saw_line);
}

TEST_CASE("LefParser supports progress reporting and cancellation", "[parsing][Lef][fast]")
{
    std::string lef = "VERSION 5.8 ;\n";
    for (int i = 0; i < 200; ++i) {
        lef += "LAYER M" + std::to_string(i) + "\n  TYPE ROUTING ;\n  WIDTH 0.1 ;\n  PITCH 0.2 ;\n  DIRECTION HORIZONTAL ;\nEND M" + std::to_string(i) + "\n";
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
    REQUIRE(has_diagnostic_code(data, "LEF_PARSE_CANCELLED"));
}

TEST_CASE("LefParser Slow deterministic malformed corpus does not crash and preserves diagnostics", "[parsing][Lef][Slow]")
{
    const std::array<std::string, 8> corpus{
        "",
        "VERSION 5.8 ;\nEND LIBRARY\n",
        "MACRO broken\n  PIN A\n    PORT\n      LAYER M1 ;\n        RECT x y z q ;\n",
        "SITE CORE\n  CLASS CORE ;\n  SIZE -1 BY 0 ;\nEND CORE\nEND LIBRARY\n",
        "LAYER M1\n  TYPE ROUTING ;\n  WIDTH 0 ;\n  PITCH 0 ;\n  DIRECTION UNKNOWN ;\nEND M1\nEND LIBRARY\n",
        "VIARULE VR1\n  CUTSIZE 0.1 0.2 ;\nEND VR1\nEND LIBRARY\n",
        std::string(16384, 'X'),
        "MACRO A\n  OBS\n    LAYER M1 ;\n      POLYGON 0 0 1 0 1 ;\n  END\nEND A\nEND LIBRARY\n"
    };

    LefParser parser;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const LefLibraryData data = parser.parse_string(corpus[i], "corpus_" + std::to_string(i) + ".lef");
        REQUIRE(data.diagnostics.size() < 10000);
    }
}

TEST_CASE("LefParser Slow deterministic mutation fuzz corpus does not throw on malformed industrial-like inputs", "[parsing][Lef][Slow]")
{
    const std::string seed_text = make_large_lef(8, 6, 4);
    LefParser parser;

    for (std::uint32_t seed = 1; seed <= 24; ++seed) {
        const std::string mutated = mutate_text_deterministically(seed_text, seed);
        REQUIRE_NOTHROW([&]() {
            const LefLibraryData data = parser.parse_string(mutated, "mutated_" + std::to_string(seed) + ".lef");
            REQUIRE(data.diagnostics.size() < 20000);
        }());
    }
}

TEST_CASE("LefParser Slow large synthetic library stress parse remains bounded and structurally correct", "[parsing][Lef][Slow]")
{
    const std::string lef = make_large_lef(96, 64, 12);
    LefParser parser;

    const auto start = std::chrono::steady_clock::now();
    const LefLibraryData data = parser.parse_string(lef, "stress.lef");
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(!data.has_errors());
    REQUIRE(data.layers.size() == 96);
    REQUIRE(data.macros.size() == 64);
    REQUIRE(data.macros.front().pins.size() == 12);
    REQUIRE(elapsed_ms < 5000);
}
