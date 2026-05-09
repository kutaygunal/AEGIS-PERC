#include "aegis/parsing/lef_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace aegis::parsing {
namespace {

struct LefStatement {
    std::string text;
    std::size_t line = 0;
};

std::string trim(std::string s)
{
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string strip_comment(std::string line)
{
    bool in_quote = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            in_quote = !in_quote;
        }
        if (!in_quote && line[i] == '#') {
            line.erase(i);
            break;
        }
    }
    return line;
}

std::string strip_quotes(std::string value)
{
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            current.push_back(c);
            continue;
        }
        if (!in_quote && std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        if (!in_quote && (c == ';' || c == '(' || c == ')')) {
            flush();
            continue;
        }
        current.push_back(c);
    }
    flush();
    return tokens;
}

double parse_double(const std::string& token)
{
    std::size_t pos = 0;
    const double value = std::stod(token, &pos);
    if (pos != token.size()) {
        throw std::invalid_argument("trailing characters");
    }
    return value;
}

std::string join_tokens(const std::vector<std::string>& tokens, std::size_t first = 0)
{
    std::ostringstream out;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        if (i != first) {
            out << ' ';
        }
        out << tokens[i];
    }
    return out.str();
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open LEF file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void normalize_rect(LefRect& rect)
{
    if (rect.x2 < rect.x1) {
        std::swap(rect.x1, rect.x2);
    }
    if (rect.y2 < rect.y1) {
        std::swap(rect.y1, rect.y2);
    }
}

bool is_block_opener_without_semicolon(const std::vector<std::string>& tokens, const std::string& trimmed)
{
    if (tokens.empty() || trimmed.find(';') != std::string::npos) {
        return false;
    }

    static const std::unordered_set<std::string> keywords{
        "SITE", "LAYER", "VIA", "MACRO", "PIN", "PORT", "OBS",
        "UNITS", "PROPERTYDEFINITIONS", "END", "VIARULE", "NONDEFAULTRULE"
    };
    return keywords.count(tokens.front()) != 0;
}

std::vector<LefStatement> split_statements(const std::string& content)
{
    std::vector<LefStatement> statements;
    std::istringstream input(content);
    std::string raw_line;
    std::string accumulator;
    std::size_t accumulator_line = 0;
    std::size_t line_no = 0;

    auto push_accumulator = [&]() {
        const std::string text = trim(accumulator);
        if (!text.empty()) {
            statements.push_back({text, accumulator_line});
        }
        accumulator.clear();
        accumulator_line = 0;
    };

    while (std::getline(input, raw_line)) {
        ++line_no;
        const std::string stripped = trim(strip_comment(raw_line));
        if (stripped.empty()) {
            continue;
        }

        const auto stripped_tokens = tokenize(stripped);
        if (is_block_opener_without_semicolon(stripped_tokens, stripped)) {
            push_accumulator();
            statements.push_back({stripped, line_no});
            continue;
        }

        std::string working = stripped;
        bool in_quote = false;
        std::size_t segment_start = 0;
        for (std::size_t i = 0; i < working.size(); ++i) {
            if (working[i] == '"') {
                in_quote = !in_quote;
            }
            if (!in_quote && working[i] == ';') {
                std::string segment = trim(working.substr(segment_start, i - segment_start));
                if (!segment.empty()) {
                    if (accumulator.empty()) {
                        accumulator_line = line_no;
                        accumulator = std::move(segment);
                    } else {
                        accumulator += ' ';
                        accumulator += segment;
                    }
                    push_accumulator();
                }
                segment_start = i + 1;
            }
        }

        std::string trailing = trim(working.substr(segment_start));
        if (!trailing.empty()) {
            if (accumulator.empty()) {
                accumulator_line = line_no;
                accumulator = std::move(trailing);
            } else {
                accumulator += ' ';
                accumulator += trailing;
            }
        }
    }

    push_accumulator();
    return statements;
}

bool is_known_top_level_property_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "NAMESCASESENSITIVE", "MANUFACTURINGGRID", "FIXEDMASK", "CLEARANCEMEASURE",
        "NOWIREEXTENSIONATPIN", "USEMINSPACING", "MAXVIASTACK", "BEGINEXT"
    };
    return known.count(keyword) != 0;
}

bool is_known_layer_property_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "WIDTH", "SPACING", "SPACINGTABLE", "PITCH", "OFFSET", "DIRECTION",
        "RESISTANCE", "CAPACITANCE", "EDGECAPACITANCE", "AREA", "THICKNESS",
        "HEIGHT", "WIREEXTENSION", "SHRINKAGE", "CAPMULTIPLIER", "MINWIDTH",
        "MAXWIDTH", "MINSTEP", "PROTRUSIONWIDTH", "PROPERTY"
    };
    return known.count(keyword) != 0;
}

bool is_known_macro_property_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "SOURCE", "EEQ", "LEQ", "SITE", "SYMMETRY", "POWER", "PROPERTY"
    };
    return known.count(keyword) != 0;
}

bool is_known_pin_property_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "SHAPE", "CAPACITANCE", "ANTENNAMODEL", "ANTENNAGATEAREA", "ANTENNADIFFAREA",
        "ANTENNAPARTIALMETALAREA", "ANTENNAPARTIALMETALSIDEAREA", "ANTENNAPARTIALCUTAREA",
        "ANTENNAMAXAREACAR", "ANTENNAMAXSIDEAREACAR", "ANTENNAMAXCUTCAR",
        "MUSTJOIN", "NETEXPR", "SUPPLYSENSITIVITY", "GROUNDSENSITIVITY",
        "TAPERRULE", "PROPERTY"
    };
    return known.count(keyword) != 0;
}

bool is_known_via_property_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "DEFAULT", "RESISTANCE", "FOREIGN", "TOPOFSTACKONLY", "VIARULE",
        "CUTSIZE", "CUTSPACING", "ENCLOSURE", "ROWCOL", "ORIGIN", "OFFSET",
        "PATTERN", "PROPERTY"
    };
    return known.count(keyword) != 0;
}

std::vector<LefPoint> parse_points(const std::vector<std::string>& tokens, std::size_t first)
{
    std::vector<LefPoint> points;
    for (std::size_t i = first; i + 1 < tokens.size(); i += 2) {
        points.push_back({parse_double(tokens[i]), parse_double(tokens[i + 1])});
    }
    return points;
}

std::vector<std::string> split_words(const std::string& value)
{
    std::istringstream input(value);
    std::vector<std::string> words;
    std::string token;
    while (input >> token) {
        words.push_back(token);
    }
    return words;
}

std::optional<std::string> first_word(const std::string& value)
{
    const auto words = split_words(value);
    if (words.empty()) {
        return std::nullopt;
    }
    return words.front();
}

std::optional<LefAxisValue> parse_axis_value(const std::string& value)
{
    const auto tokens = split_words(value);
    if (tokens.empty()) {
        return std::nullopt;
    }
    if (tokens.size() == 1) {
        const double scalar = parse_double(tokens[0]);
        return LefAxisValue{scalar, scalar};
    }
    return LefAxisValue{parse_double(tokens[0]), parse_double(tokens[1])};
}

bool contains_token(const std::string& haystack, const std::string& needle)
{
    const auto words = split_words(haystack);
    return std::find(words.begin(), words.end(), needle) != words.end();
}

LefSiteClass parse_site_class(const std::string& value)
{
    if (contains_token(value, "CORE")) return LefSiteClass::Core;
    if (contains_token(value, "PAD")) return LefSiteClass::Pad;
    return LefSiteClass::Unknown;
}

LefLayerType parse_layer_type(const std::string& value)
{
    if (contains_token(value, "ROUTING")) return LefLayerType::Routing;
    if (contains_token(value, "CUT")) return LefLayerType::Cut;
    if (contains_token(value, "MASTERSLICE")) return LefLayerType::Masterslice;
    if (contains_token(value, "OVERLAP")) return LefLayerType::Overlap;
    if (contains_token(value, "IMPLANT")) return LefLayerType::Implant;
    return LefLayerType::Unknown;
}

LefRoutingDirection parse_routing_direction(const std::string& value)
{
    if (contains_token(value, "HORIZONTAL")) return LefRoutingDirection::Horizontal;
    if (contains_token(value, "VERTICAL")) return LefRoutingDirection::Vertical;
    if (contains_token(value, "DIAG45")) return LefRoutingDirection::Diag45;
    if (contains_token(value, "DIAG135")) return LefRoutingDirection::Diag135;
    return LefRoutingDirection::Unknown;
}

LefPinDirection parse_pin_direction(const std::string& value, bool& is_tristate)
{
    is_tristate = contains_token(value, "TRISTATE");
    if (contains_token(value, "INPUT")) return LefPinDirection::Input;
    if (contains_token(value, "OUTPUT")) return LefPinDirection::Output;
    if (contains_token(value, "INOUT")) return LefPinDirection::Inout;
    if (contains_token(value, "FEEDTHRU")) return LefPinDirection::Feedthru;
    return LefPinDirection::Unknown;
}

LefPinUse parse_pin_use(const std::string& value)
{
    if (contains_token(value, "SIGNAL")) return LefPinUse::Signal;
    if (contains_token(value, "POWER")) return LefPinUse::Power;
    if (contains_token(value, "GROUND")) return LefPinUse::Ground;
    if (contains_token(value, "CLOCK")) return LefPinUse::Clock;
    if (contains_token(value, "ANALOG")) return LefPinUse::Analog;
    if (contains_token(value, "RESET")) return LefPinUse::Reset;
    if (contains_token(value, "SCAN")) return LefPinUse::Scan;
    if (contains_token(value, "TIEOFF")) return LefPinUse::Tieoff;
    return LefPinUse::Unknown;
}

LefMacroClass parse_macro_class(const std::string& value)
{
    if (contains_token(value, "CORE")) return LefMacroClass::Core;
    if (contains_token(value, "BLOCK")) return LefMacroClass::Block;
    if (contains_token(value, "PAD")) return LefMacroClass::Pad;
    if (contains_token(value, "ENDCAP")) return LefMacroClass::Endcap;
    if (contains_token(value, "COVER")) return LefMacroClass::Cover;
    if (contains_token(value, "RING")) return LefMacroClass::Ring;
    if (contains_token(value, "SPACER")) return LefMacroClass::Spacer;
    return LefMacroClass::Unknown;
}

void validate_geometry_primitives(LefLibraryData& data,
                                  const LefLayerGeometry& geometry,
                                  const std::string& owner,
                                  std::size_t line)
{
    for (const auto& rect : geometry.rects) {
        if (rect.width() <= 0.0 || rect.height() <= 0.0) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_GEOMETRY_RECT_DEGENERATE",
                                        owner + " contains a degenerate RECT on layer '" + geometry.layer_name + "'",
                                        rect.source_line == 0 ? std::optional<std::size_t>(line)
                                                              : std::optional<std::size_t>(rect.source_line)});
        }
    }
    for (const auto& polygon : geometry.polygons) {
        if (polygon.points.size() < 3) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_GEOMETRY_POLYGON_DEGENERATE",
                                        owner + " contains a degenerate POLYGON on layer '" + geometry.layer_name + "'",
                                        polygon.source_line == 0 ? std::optional<std::size_t>(line)
                                                                 : std::optional<std::size_t>(polygon.source_line)});
        }
    }
    for (const auto& path : geometry.paths) {
        if (path.points.size() < 2) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_GEOMETRY_PATH_DEGENERATE",
                                        owner + " contains a degenerate PATH on layer '" + geometry.layer_name + "'",
                                        path.source_line == 0 ? std::optional<std::size_t>(line)
                                                              : std::optional<std::size_t>(path.source_line)});
        }
        if (path.width.has_value() && *path.width <= 0.0) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_GEOMETRY_PATH_WIDTH_INVALID",
                                        owner + " contains a PATH with non-positive WIDTH on layer '" + geometry.layer_name + "'",
                                        path.source_line == 0 ? std::optional<std::size_t>(line)
                                                              : std::optional<std::size_t>(path.source_line)});
        }
    }
}

void normalize_semantics(LefLibraryData& data)
{
    for (auto& site : data.sites) {
        site.class_kind = parse_site_class(site.site_class);
        if (const auto it = site.properties.find("SYMMETRY"); it != site.properties.end()) {
            site.symmetries = split_words(it->second);
        }

        if (site.class_kind == LefSiteClass::Unknown) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Warning,
                                        "LEF_SITE_CLASS_UNKNOWN",
                                        "SITE '" + site.name + "' has an unrecognized CLASS semantic",
                                        site.source_line});
        }
        if (site.width <= 0.0 || site.height <= 0.0) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_SITE_SIZE_INCOMPLETE",
                                        "SITE '" + site.name + "' is missing a valid positive SIZE semantic",
                                        site.source_line});
        }
    }

    for (auto& layer : data.layers) {
        layer.type_kind = parse_layer_type(layer.type);
        if (layer.type_kind == LefLayerType::Unknown) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_LAYER_TYPE_INCOMPLETE",
                                        "LAYER '" + layer.name + "' is missing a recognized TYPE semantic",
                                        layer.source_line});
        }

        try {
            if (const auto it = layer.properties.find("DIRECTION"); it != layer.properties.end()) {
                layer.routing_direction = parse_routing_direction(it->second);
            }
            if (const auto it = layer.properties.find("WIDTH"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.width_value = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("SPACING"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.spacing_value = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("PITCH"); it != layer.properties.end()) {
                layer.pitch = parse_axis_value(it->second);
            }
            if (const auto it = layer.properties.find("OFFSET"); it != layer.properties.end()) {
                layer.offset = parse_axis_value(it->second);
            }
            if (const auto it = layer.properties.find("RESISTANCE"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.resistance = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("CAPACITANCE"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.capacitance = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("EDGECAPACITANCE"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.edge_capacitance = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("AREA"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.area = parse_double(*value);
                }
            }
            if (const auto it = layer.properties.find("THICKNESS"); it != layer.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    layer.thickness = parse_double(*value);
                }
            }
        } catch (const std::exception&) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_LAYER_SEMANTIC_INVALID",
                                        "LAYER '" + layer.name + "' contains an invalid typed semantic value",
                                        layer.source_line});
        }

        if (layer.type_kind == LefLayerType::Routing) {
            if (!layer.width_value.has_value() || *layer.width_value <= 0.0) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_LAYER_ROUTING_WIDTH_INCOMPLETE",
                                            "ROUTING LAYER '" + layer.name + "' is missing a valid positive WIDTH semantic",
                                            layer.source_line});
            }
            if (!layer.pitch.has_value() || layer.pitch->x <= 0.0 || layer.pitch->y <= 0.0) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_LAYER_ROUTING_PITCH_INCOMPLETE",
                                            "ROUTING LAYER '" + layer.name + "' is missing a valid positive PITCH semantic",
                                            layer.source_line});
            }
            if (layer.routing_direction == LefRoutingDirection::Unknown) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_LAYER_ROUTING_DIRECTION_INCOMPLETE",
                                            "ROUTING LAYER '" + layer.name + "' is missing a recognized DIRECTION semantic",
                                            layer.source_line});
            }
        }
    }

    for (auto& via : data.vias) {
        via.is_default = via.properties.contains("HEADER") && contains_token(via.properties.at("HEADER"), "DEFAULT");
        try {
            if (const auto it = via.properties.find("RESISTANCE"); it != via.properties.end()) {
                if (const auto value = first_word(it->second); value.has_value()) {
                    via.resistance = parse_double(*value);
                }
            }
        } catch (const std::exception&) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_VIA_SEMANTIC_INVALID",
                                        "VIA '" + via.name + "' contains an invalid RESISTANCE semantic",
                                        via.source_line});
        }

        if (via.layers.empty()) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_VIA_LAYERS_INCOMPLETE",
                                        "VIA '" + via.name + "' does not define any layer geometry",
                                        via.source_line});
        }
        for (const auto& layer_geometry : via.layers) {
            validate_geometry_primitives(data, layer_geometry, "VIA '" + via.name + "'", via.source_line);
        }
    }

    for (auto& via_rule : data.via_rules) {
        via_rule.generate = via_rule.properties.contains("GENERATE");
        if (!via_rule.generate) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Warning,
                                        "LEF_VIARULE_INCOMPLETE",
                                        "VIARULE '" + via_rule.name + "' does not declare GENERATE semantics",
                                        via_rule.source_line});
        }
    }

    for (auto& rule : data.non_default_rules) {
        rule.hard_spacing = rule.properties.contains("HARDSPACING");
    }

    for (auto& macro : data.macros) {
        macro.class_kind = parse_macro_class(macro.macro_class);
        if (const auto it = macro.properties.find("SYMMETRY"); it != macro.properties.end()) {
            macro.symmetries = split_words(it->second);
        }
        if (const auto it = macro.properties.find("SITE"); it != macro.properties.end()) {
            macro.site_name = it->second;
        }
        if (const auto it = macro.properties.find("SOURCE"); it != macro.properties.end()) {
            macro.source = it->second;
        }

        if (macro.class_kind == LefMacroClass::Unknown) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Warning,
                                        "LEF_MACRO_CLASS_UNKNOWN",
                                        "MACRO '" + macro.name + "' has an unrecognized CLASS semantic",
                                        macro.source_line});
        }
        if (macro.width <= 0.0 || macro.height <= 0.0) {
            data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                        "LEF_MACRO_SIZE_INCOMPLETE",
                                        "MACRO '" + macro.name + "' is missing a valid positive SIZE semantic",
                                        macro.source_line});
        }

        for (auto& pin : macro.pins) {
            pin.direction_kind = parse_pin_direction(pin.direction, pin.is_tristate);
            pin.use_kind = parse_pin_use(pin.use);

            try {
                if (const auto it = pin.properties.find("SHAPE"); it != pin.properties.end()) {
                    pin.shape = it->second;
                }
                if (const auto it = pin.properties.find("CAPACITANCE"); it != pin.properties.end()) {
                    if (const auto value = first_word(it->second); value.has_value()) {
                        pin.capacitance = parse_double(*value);
                    }
                }
                if (const auto it = pin.properties.find("ANTENNAGATEAREA"); it != pin.properties.end()) {
                    if (const auto value = first_word(it->second); value.has_value()) {
                        pin.antenna_gate_area = parse_double(*value);
                    }
                }
                if (const auto it = pin.properties.find("ANTENNADIFFAREA"); it != pin.properties.end()) {
                    if (const auto value = first_word(it->second); value.has_value()) {
                        pin.antenna_diff_area = parse_double(*value);
                    }
                }
            } catch (const std::exception&) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_PIN_SEMANTIC_INVALID",
                                            "PIN '" + pin.name + "' in MACRO '" + macro.name + "' contains an invalid typed semantic value",
                                            pin.source_line});
            }

            if (pin.direction_kind == LefPinDirection::Unknown) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_PIN_DIRECTION_INCOMPLETE",
                                            "PIN '" + pin.name + "' in MACRO '" + macro.name + "' is missing a recognized DIRECTION semantic",
                                            pin.source_line});
            }
            if (pin.use_kind == LefPinUse::Unknown) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Warning,
                                            "LEF_PIN_USE_UNKNOWN",
                                            "PIN '" + pin.name + "' in MACRO '" + macro.name + "' has an unrecognized USE semantic",
                                            pin.source_line});
            }
            if (pin.ports.empty()) {
                data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                            "LEF_PIN_PORTS_INCOMPLETE",
                                            "PIN '" + pin.name + "' in MACRO '" + macro.name + "' does not define any PORT geometry",
                                            pin.source_line});
            }
            for (const auto& port : pin.ports) {
                if (port.layers.empty()) {
                    data.diagnostics.push_back({LefDiagnostic::Severity::Error,
                                                "LEF_PORT_LAYERS_INCOMPLETE",
                                                "PIN '" + pin.name + "' in MACRO '" + macro.name + "' contains a PORT without any LAYER geometry",
                                                port.source_line});
                }
                for (const auto& layer_geometry : port.layers) {
                    validate_geometry_primitives(data,
                                                 layer_geometry,
                                                 "PIN '" + pin.name + "' in MACRO '" + macro.name + "'",
                                                 port.source_line);
                }
            }
        }

        if (macro.obstruction.has_value()) {
            for (const auto& layer_geometry : macro.obstruction->layers) {
                validate_geometry_primitives(data,
                                             layer_geometry,
                                             "OBS in MACRO '" + macro.name + "'",
                                             macro.obstruction->source_line);
            }
        }
    }
}

} // namespace

std::string to_string(LefDiagnostic::Severity severity)
{
    switch (severity) {
    case LefDiagnostic::Severity::Info: return "info";
    case LefDiagnostic::Severity::Warning: return "warning";
    case LefDiagnostic::Severity::Error: return "error";
    }
    return "info";
}

std::string to_string(LefSiteClass value)
{
    switch (value) {
    case LefSiteClass::Unknown: return "unknown";
    case LefSiteClass::Core: return "core";
    case LefSiteClass::Pad: return "pad";
    }
    return "unknown";
}

std::string to_string(LefLayerType value)
{
    switch (value) {
    case LefLayerType::Unknown: return "unknown";
    case LefLayerType::Routing: return "routing";
    case LefLayerType::Cut: return "cut";
    case LefLayerType::Masterslice: return "masterslice";
    case LefLayerType::Overlap: return "overlap";
    case LefLayerType::Implant: return "implant";
    }
    return "unknown";
}

std::string to_string(LefRoutingDirection value)
{
    switch (value) {
    case LefRoutingDirection::Unknown: return "unknown";
    case LefRoutingDirection::Horizontal: return "horizontal";
    case LefRoutingDirection::Vertical: return "vertical";
    case LefRoutingDirection::Diag45: return "diag45";
    case LefRoutingDirection::Diag135: return "diag135";
    }
    return "unknown";
}

std::string to_string(LefPinDirection value)
{
    switch (value) {
    case LefPinDirection::Unknown: return "unknown";
    case LefPinDirection::Input: return "input";
    case LefPinDirection::Output: return "output";
    case LefPinDirection::Inout: return "inout";
    case LefPinDirection::Feedthru: return "feedthru";
    }
    return "unknown";
}

std::string to_string(LefPinUse value)
{
    switch (value) {
    case LefPinUse::Unknown: return "unknown";
    case LefPinUse::Signal: return "signal";
    case LefPinUse::Power: return "power";
    case LefPinUse::Ground: return "ground";
    case LefPinUse::Clock: return "clock";
    case LefPinUse::Analog: return "analog";
    case LefPinUse::Reset: return "reset";
    case LefPinUse::Scan: return "scan";
    case LefPinUse::Tieoff: return "tieoff";
    }
    return "unknown";
}

std::string to_string(LefMacroClass value)
{
    switch (value) {
    case LefMacroClass::Unknown: return "unknown";
    case LefMacroClass::Core: return "core";
    case LefMacroClass::Block: return "block";
    case LefMacroClass::Pad: return "pad";
    case LefMacroClass::Endcap: return "endcap";
    case LefMacroClass::Cover: return "cover";
    case LefMacroClass::Ring: return "ring";
    case LefMacroClass::Spacer: return "spacer";
    }
    return "unknown";
}

bool LefLibraryData::has_errors() const noexcept
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == LefDiagnostic::Severity::Error) {
            return true;
        }
    }
    return false;
}

LefLibraryData LefParser::parse_string(const std::string& content,
                                       const std::string& /*source_name*/,
                                       const ICancellationToken& token,
                                       LefProgressCallback progress) const
{
    LefLibraryData data;

    enum class Context {
        Top,
        Site,
        Layer,
        Via,
        Macro,
        Pin,
        Port,
        Obs,
        PropertyDefinitions,
        Units,
        ViaRule,
        NonDefaultRule
    };

    struct State {
        Context context = Context::Top;
        LefSite site;
        LefLayer layer;
        LefVia via;
        LefViaRule via_rule;
        LefNonDefaultRule non_default_rule;
        LefMacro macro;
        LefPin pin;
        LefPort port;
        LefObs obs;
        LefLayerGeometry* active_geometry = nullptr;
    } state;

    auto push_diag = [&](LefDiagnostic::Severity severity,
                         std::string code,
                         std::string message,
                         std::optional<std::size_t> line) {
        data.diagnostics.push_back({severity, std::move(code), std::move(message), line});
    };

    auto set_property = [](std::map<std::string, std::string>& props,
                           const std::string& key,
                           const std::vector<std::string>& tokens,
                           std::size_t first = 1) {
        props[key] = join_tokens(tokens, first);
    };

    auto report_progress = [&](std::size_t current, std::size_t total, const std::string& message) {
        if (progress) {
            progress({current, total, message});
        }
    };

    auto require_geometry_target = [&](std::size_t line_no) -> bool {
        if (state.active_geometry == nullptr) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_GEOMETRY_WITHOUT_LAYER",
                      "Geometry statement requires an active LAYER context",
                      line_no);
            return false;
        }
        return true;
    };

    auto set_geometry_width_hint = [&](std::size_t line_no, const std::vector<std::string>& tokens, const std::string& scope) {
        if (tokens.size() < 2) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_WIDTH_INVALID",
                      scope + " WIDTH requires a numeric value",
                      line_no);
            return;
        }
        if (!require_geometry_target(line_no)) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_" + scope + "_WIDTH_WITHOUT_LAYER",
                      scope + " WIDTH was recorded without an active LAYER context",
                      line_no);
            return;
        }
        state.active_geometry->path_width_hint = parse_double(tokens[1]);
    };

    auto add_rect = [&](std::size_t line_no, const std::vector<std::string>& tokens) {
        if (tokens.size() != 5 || !require_geometry_target(line_no)) {
            if (tokens.size() != 5) {
                push_diag(LefDiagnostic::Severity::Error, "LEF_RECT_INVALID", "RECT requires four coordinates", line_no);
            }
            return;
        }
        LefRect rect{parse_double(tokens[1]), parse_double(tokens[2]), parse_double(tokens[3]), parse_double(tokens[4]), line_no};
        normalize_rect(rect);
        state.active_geometry->rects.push_back(rect);
    };

    auto add_polygon = [&](std::size_t line_no, const std::vector<std::string>& tokens) {
        if (!require_geometry_target(line_no)) {
            return;
        }
        const std::size_t coord_count = tokens.size() >= 1 ? tokens.size() - 1 : 0;
        if (coord_count < 6 || coord_count % 2 != 0) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_POLYGON_INVALID",
                      "POLYGON requires at least three coordinate pairs",
                      line_no);
            return;
        }
        LefPolygon polygon;
        polygon.points = parse_points(tokens, 1);
        polygon.source_line = line_no;
        state.active_geometry->polygons.push_back(std::move(polygon));
    };

    auto add_path = [&](std::size_t line_no, const std::vector<std::string>& tokens) {
        if (!require_geometry_target(line_no)) {
            return;
        }
        const std::size_t coord_count = tokens.size() >= 1 ? tokens.size() - 1 : 0;
        if (coord_count < 4 || coord_count % 2 != 0) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_PATH_INVALID",
                      "PATH requires at least two coordinate pairs",
                      line_no);
            return;
        }
        LefPath path;
        path.points = parse_points(tokens, 1);
        path.width = state.active_geometry->path_width_hint;
        path.source_line = line_no;
        state.active_geometry->paths.push_back(std::move(path));
    };

    auto add_via_placement = [&](std::size_t line_no, const std::vector<std::string>& tokens) {
        if (!require_geometry_target(line_no)) {
            return;
        }
        if (tokens.size() < 4) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_VIA_GEOMETRY_INVALID",
                      "VIA geometry requires x y and via name",
                      line_no);
            return;
        }
        LefViaPlacement placement;
        placement.x = parse_double(tokens[1]);
        placement.y = parse_double(tokens[2]);
        placement.via_name = tokens[3];
        placement.orientation = tokens.size() > 4 ? join_tokens(tokens, 4) : "";
        placement.source_line = line_no;
        state.active_geometry->via_placements.push_back(std::move(placement));
    };

    auto close_port = [&]() {
        if (state.context == Context::Port) {
            state.pin.ports.push_back(std::move(state.port));
            state.port = {};
            state.active_geometry = nullptr;
            state.context = Context::Pin;
        }
    };

    auto close_pin = [&]() {
        if (state.context == Context::Port) {
            close_port();
        }
        if (state.context == Context::Pin) {
            state.macro.pins.push_back(std::move(state.pin));
            state.pin = {};
            state.active_geometry = nullptr;
            state.context = Context::Macro;
        }
    };

    auto close_obs = [&]() {
        if (state.context == Context::Obs) {
            state.macro.obstruction = std::move(state.obs);
            state.obs = {};
            state.active_geometry = nullptr;
            state.context = Context::Macro;
        }
    };

    auto close_macro = [&]() {
        if (state.context == Context::Port) {
            close_port();
        }
        if (state.context == Context::Pin) {
            close_pin();
        }
        if (state.context == Context::Obs) {
            close_obs();
        }
        if (state.context == Context::Macro) {
            data.macros.push_back(std::move(state.macro));
            state.macro = {};
            state.active_geometry = nullptr;
            state.context = Context::Top;
        }
    };

    auto close_site = [&]() {
        if (state.context == Context::Site) {
            data.sites.push_back(std::move(state.site));
            state.site = {};
            state.context = Context::Top;
        }
    };

    auto close_layer = [&]() {
        if (state.context == Context::Layer) {
            data.layers.push_back(std::move(state.layer));
            state.layer = {};
            state.context = Context::Top;
        }
    };

    auto close_via = [&]() {
        if (state.context == Context::Via) {
            data.vias.push_back(std::move(state.via));
            state.via = {};
            state.active_geometry = nullptr;
            state.context = Context::Top;
        }
    };

    auto close_via_rule = [&]() {
        if (state.context == Context::ViaRule) {
            data.via_rules.push_back(std::move(state.via_rule));
            state.via_rule = {};
            state.context = Context::Top;
        }
    };

    auto close_non_default_rule = [&]() {
        if (state.context == Context::NonDefaultRule) {
            data.non_default_rules.push_back(std::move(state.non_default_rule));
            state.non_default_rule = {};
            state.context = Context::Top;
        }
    };

    const auto statements = split_statements(content);
    const std::size_t total_statements = statements.size();
    std::size_t last_pct_reported = 0;
    bool saw_end_library = false;

    report_progress(0, 100, "Opening LEF content");

    for (std::size_t index = 0; index < statements.size(); ++index) {
        const auto& statement = statements[index];
        const std::size_t line_no = statement.line;

        if (index % 32 == 0 && token.is_cancelled()) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_PARSE_CANCELLED",
                      "LEF parse cancelled by user",
                      line_no);
            break;
        }

        if (progress && total_statements > 0) {
            const std::size_t pct = ((index + 1) * 100) / total_statements;
            if (pct > last_pct_reported) {
                last_pct_reported = pct;
                report_progress(pct, 100, "Parsed " + std::to_string(index + 1) + " LEF statements");
            }
        }

        const auto tokens = tokenize(statement.text);
        if (tokens.empty()) {
            continue;
        }

        const std::string& keyword = tokens[0];

        if (keyword == "VERSION") {
            if (tokens.size() < 2) {
                push_diag(LefDiagnostic::Severity::Error, "LEF_VERSION_INVALID", "VERSION requires a value", line_no);
                continue;
            }
            data.version = tokens[1];
            continue;
        }
        if (keyword == "DIVIDERCHAR") {
            if (tokens.size() < 2) {
                push_diag(LefDiagnostic::Severity::Error, "LEF_DIVIDERCHAR_INVALID", "DIVIDERCHAR requires a value", line_no);
                continue;
            }
            data.divider_char = strip_quotes(tokens[1]);
            continue;
        }
        if (keyword == "BUSBITCHARS") {
            if (tokens.size() < 2) {
                push_diag(LefDiagnostic::Severity::Error, "LEF_BUSBITCHARS_INVALID", "BUSBITCHARS requires a value", line_no);
                continue;
            }
            data.bus_bit_chars = strip_quotes(tokens[1]);
            continue;
        }
        if (keyword == "NOWIREEXTENSIONATPIN" || keyword == "FIXEDMASK") {
            data.properties[keyword] = tokens.size() > 1 ? join_tokens(tokens, 1) : "ON";
            continue;
        }
        if (keyword == "NAMESCASESENSITIVE" || keyword == "MANUFACTURINGGRID" ||
            keyword == "CLEARANCEMEASURE" || keyword == "USEMINSPACING" || keyword == "MAXVIASTACK") {
            data.properties[keyword] = join_tokens(tokens, 1);
            continue;
        }
        if (keyword == "PROPERTYDEFINITIONS") {
            state.context = Context::PropertyDefinitions;
            continue;
        }
        if (keyword == "UNITS") {
            state.context = Context::Units;
            continue;
        }
        if (keyword == "END" && tokens.size() >= 2 && tokens[1] == "LIBRARY") {
            saw_end_library = true;
            break;
        }

        if (state.context == Context::PropertyDefinitions) {
            if (keyword == "END" && tokens.size() >= 2 && tokens[1] == "PROPERTYDEFINITIONS") {
                state.context = Context::Top;
            } else if (tokens.size() >= 3) {
                data.properties["PROPERTYDEF." + tokens[0] + "." + tokens[1]] = join_tokens(tokens, 2);
            } else {
                push_diag(LefDiagnostic::Severity::Warning,
                          "LEF_PROPERTYDEFINITION_PARTIAL",
                          "PROPERTYDEFINITIONS entry is incomplete and was recorded as raw text only",
                          line_no);
                data.properties["PROPERTYDEF.RAW." + std::to_string(line_no)] = statement.text;
            }
            continue;
        }
        if (state.context == Context::Units) {
            if (keyword == "END" && tokens.size() >= 2 && tokens[1] == "UNITS") {
                state.context = Context::Top;
            } else {
                data.properties["UNITS." + keyword] = join_tokens(tokens, 1);
            }
            continue;
        }

        if (state.context == Context::Top) {
            if (keyword == "SITE") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_SITE_INVALID", "SITE requires a name", line_no);
                    continue;
                }
                state.site = {};
                state.site.name = tokens[1];
                state.site.source_line = line_no;
                state.context = Context::Site;
                continue;
            }
            if (keyword == "LAYER") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_LAYER_INVALID", "LAYER requires a name", line_no);
                    continue;
                }
                state.layer = {};
                state.layer.name = tokens[1];
                state.layer.source_line = line_no;
                state.context = Context::Layer;
                continue;
            }
            if (keyword == "VIA") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_VIA_INVALID", "VIA requires a name", line_no);
                    continue;
                }
                state.via = {};
                state.via.name = tokens[1];
                state.via.source_line = line_no;
                state.active_geometry = nullptr;
                if (tokens.size() > 2) {
                    state.via.properties["HEADER"] = join_tokens(tokens, 2);
                }
                state.context = Context::Via;
                continue;
            }
            if (keyword == "VIARULE") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_VIARULE_INVALID", "VIARULE requires a name", line_no);
                    continue;
                }
                state.via_rule = {};
                state.via_rule.name = tokens[1];
                state.via_rule.source_line = line_no;
                state.context = Context::ViaRule;
                continue;
            }
            if (keyword == "NONDEFAULTRULE") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_NONDEFAULTRULE_INVALID", "NONDEFAULTRULE requires a name", line_no);
                    continue;
                }
                state.non_default_rule = {};
                state.non_default_rule.name = tokens[1];
                state.non_default_rule.source_line = line_no;
                state.context = Context::NonDefaultRule;
                continue;
            }
            if (keyword == "MACRO") {
                if (tokens.size() < 2) {
                    push_diag(LefDiagnostic::Severity::Error, "LEF_MACRO_INVALID", "MACRO requires a name", line_no);
                    continue;
                }
                state.macro = {};
                state.macro.name = tokens[1];
                state.macro.source_line = line_no;
                state.context = Context::Macro;
                continue;
            }

            if (!is_known_top_level_property_statement(keyword)) {
                push_diag(LefDiagnostic::Severity::Warning,
                          "LEF_UNSUPPORTED_TOP_LEVEL",
                          "Unsupported top-level LEF statement '" + keyword + "'",
                          line_no);
            } else {
                data.properties[keyword] = join_tokens(tokens, 1);
            }
            continue;
        }

        if (keyword == "END") {
            const std::string target = tokens.size() >= 2 ? tokens[1] : "";
            if (state.context == Context::Site && target == state.site.name) {
                close_site();
                continue;
            }
            if (state.context == Context::Layer && target == state.layer.name) {
                close_layer();
                continue;
            }
            if (state.context == Context::Via && target == state.via.name) {
                close_via();
                continue;
            }
            if (state.context == Context::ViaRule && target == state.via_rule.name) {
                close_via_rule();
                continue;
            }
            if (state.context == Context::NonDefaultRule && target == state.non_default_rule.name) {
                close_non_default_rule();
                continue;
            }
            if (state.context == Context::Macro && target == state.macro.name) {
                close_macro();
                continue;
            }
            if (state.context == Context::Pin && target == state.pin.name) {
                close_pin();
                continue;
            }
            if (state.context == Context::Port && (target.empty() || target == "PORT")) {
                close_port();
                continue;
            }
            if (state.context == Context::Obs && (target.empty() || target == "OBS")) {
                close_obs();
                continue;
            }
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_END_MISMATCH",
                      "Unexpected END statement for '" + target + "'",
                      line_no);
            continue;
        }

        try {
            switch (state.context) {
            case Context::Site:
                if (keyword == "CLASS") {
                    state.site.site_class = join_tokens(tokens, 1);
                } else if (keyword == "SIZE" && tokens.size() >= 4 && tokens[2] == "BY") {
                    state.site.width = parse_double(tokens[1]);
                    state.site.height = parse_double(tokens[3]);
                } else {
                    set_property(state.site.properties, keyword, tokens);
                }
                break;

            case Context::Layer:
                if (keyword == "TYPE") {
                    state.layer.type = join_tokens(tokens, 1);
                } else if (is_known_layer_property_statement(keyword)) {
                    set_property(state.layer.properties, keyword, tokens);
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_LAYER_STATEMENT",
                              "Unsupported LAYER statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::Via:
                if (keyword == "LAYER") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_VIA_LAYER_INVALID", "VIA LAYER requires a layer name", line_no);
                        break;
                    }
                    state.via.layers.push_back({tokens[1], {}, {}, {}, {}, std::nullopt, line_no});
                    state.active_geometry = &state.via.layers.back();
                } else if (keyword == "RECT") {
                    add_rect(line_no, tokens);
                } else if (keyword == "POLYGON") {
                    add_polygon(line_no, tokens);
                } else if (keyword == "PATH") {
                    add_path(line_no, tokens);
                } else if (keyword == "VIA") {
                    add_via_placement(line_no, tokens);
                } else if (keyword == "WIDTH" && state.active_geometry != nullptr) {
                    set_geometry_width_hint(line_no, tokens, "VIA");
                } else if (is_known_via_property_statement(keyword)) {
                    set_property(state.via.properties, keyword, tokens);
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_VIA_STATEMENT",
                              "Unsupported VIA statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::ViaRule:
                if (keyword == "GENERATE") {
                    state.via_rule.properties["GENERATE"] = "ON";
                } else {
                    set_property(state.via_rule.properties, keyword, tokens);
                }
                break;

            case Context::NonDefaultRule:
                if (keyword == "HARDSPACING") {
                    state.non_default_rule.properties["HARDSPACING"] = "ON";
                } else {
                    set_property(state.non_default_rule.properties, keyword, tokens);
                }
                break;

            case Context::Macro:
                if (keyword == "CLASS") {
                    state.macro.macro_class = join_tokens(tokens, 1);
                } else if (keyword == "FOREIGN") {
                    state.macro.foreign_name = tokens.size() >= 2 ? tokens[1] : "";
                    if (tokens.size() > 2) {
                        state.macro.properties["FOREIGN_ARGS"] = join_tokens(tokens, 2);
                    }
                } else if (keyword == "ORIGIN" && tokens.size() >= 3) {
                    state.macro.origin_x = parse_double(tokens[1]);
                    state.macro.origin_y = parse_double(tokens[2]);
                } else if (keyword == "SIZE" && tokens.size() >= 4 && tokens[2] == "BY") {
                    state.macro.width = parse_double(tokens[1]);
                    state.macro.height = parse_double(tokens[3]);
                } else if (keyword == "PIN") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_PIN_INVALID", "PIN requires a name", line_no);
                        break;
                    }
                    state.pin = {};
                    state.pin.name = tokens[1];
                    state.pin.source_line = line_no;
                    state.context = Context::Pin;
                } else if (keyword == "OBS") {
                    state.obs = {};
                    state.obs.source_line = line_no;
                    state.active_geometry = nullptr;
                    state.context = Context::Obs;
                } else if (is_known_macro_property_statement(keyword)) {
                    set_property(state.macro.properties, keyword, tokens);
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_MACRO_STATEMENT",
                              "Unsupported MACRO statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::Pin:
                if (keyword == "DIRECTION") {
                    state.pin.direction = join_tokens(tokens, 1);
                } else if (keyword == "USE") {
                    state.pin.use = join_tokens(tokens, 1);
                } else if (keyword == "PORT") {
                    state.port = {};
                    state.port.source_line = line_no;
                    state.active_geometry = nullptr;
                    state.context = Context::Port;
                } else if (is_known_pin_property_statement(keyword)) {
                    set_property(state.pin.properties, keyword, tokens);
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_PIN_STATEMENT",
                              "Unsupported PIN statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::Port:
                if (keyword == "CLASS") {
                    if (!state.pin.properties.contains("PORT_CLASS")) {
                        state.pin.properties["PORT_CLASS"] = join_tokens(tokens, 1);
                    } else {
                        state.pin.properties["PORT_CLASS." + std::to_string(line_no)] = join_tokens(tokens, 1);
                    }
                } else if (keyword == "LAYER") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_PORT_LAYER_INVALID", "PORT LAYER requires a layer name", line_no);
                        break;
                    }
                    state.port.layers.push_back({tokens[1], {}, {}, {}, {}, std::nullopt, line_no});
                    state.active_geometry = &state.port.layers.back();
                } else if (keyword == "RECT") {
                    add_rect(line_no, tokens);
                } else if (keyword == "POLYGON") {
                    add_polygon(line_no, tokens);
                } else if (keyword == "PATH") {
                    add_path(line_no, tokens);
                } else if (keyword == "VIA") {
                    add_via_placement(line_no, tokens);
                } else if (keyword == "WIDTH") {
                    set_geometry_width_hint(line_no, tokens, "PORT");
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_PORT_STATEMENT",
                              "Unsupported PORT statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::Obs:
                if (keyword == "LAYER") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_OBS_LAYER_INVALID", "OBS LAYER requires a layer name", line_no);
                        break;
                    }
                    state.obs.layers.push_back({tokens[1], {}, {}, {}, {}, std::nullopt, line_no});
                    state.active_geometry = &state.obs.layers.back();
                } else if (keyword == "RECT") {
                    add_rect(line_no, tokens);
                } else if (keyword == "POLYGON") {
                    add_polygon(line_no, tokens);
                } else if (keyword == "PATH") {
                    add_path(line_no, tokens);
                } else if (keyword == "VIA") {
                    add_via_placement(line_no, tokens);
                } else if (keyword == "WIDTH") {
                    set_geometry_width_hint(line_no, tokens, "OBS");
                } else {
                    push_diag(LefDiagnostic::Severity::Warning,
                              "LEF_UNSUPPORTED_OBS_STATEMENT",
                              "Unsupported OBS statement '" + keyword + "'",
                              line_no);
                }
                break;

            case Context::Top:
            case Context::PropertyDefinitions:
            case Context::Units:
                break;
            }
        } catch (const std::exception&) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_NUMBER_INVALID",
                      "Invalid numeric value in statement '" + statement.text + "'",
                      line_no);
        }
    }

    if (state.context == Context::Port || state.context == Context::Pin || state.context == Context::Obs || state.context == Context::Macro) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_MACRO_BLOCK",
                  "LEF file ended before macro-related block was closed",
                  std::nullopt);
    } else if (state.context == Context::Site) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_SITE",
                  "LEF file ended before SITE block was closed",
                  std::nullopt);
    } else if (state.context == Context::Layer) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_LAYER",
                  "LEF file ended before LAYER block was closed",
                  std::nullopt);
    } else if (state.context == Context::Via) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_VIA",
                  "LEF file ended before VIA block was closed",
                  std::nullopt);
    } else if (state.context == Context::ViaRule) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_VIARULE",
                  "LEF file ended before VIARULE block was closed",
                  std::nullopt);
    } else if (state.context == Context::NonDefaultRule) {
        push_diag(LefDiagnostic::Severity::Error,
                  "LEF_UNTERMINATED_NONDEFAULTRULE",
                  "LEF file ended before NONDEFAULTRULE block was closed",
                  std::nullopt);
    }

    if (!saw_end_library && !token.is_cancelled()) {
        push_diag(LefDiagnostic::Severity::Warning,
                  "LEF_END_LIBRARY_MISSING",
                  "LEF file does not contain explicit END LIBRARY",
                  std::nullopt);
    }

    std::unordered_set<std::string> seen_names;
    for (const auto& layer : data.layers) {
        if (!seen_names.insert("layer:" + layer.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_LAYER",
                      "Duplicate LAYER definition for '" + layer.name + "'",
                      layer.source_line);
        }
    }
    for (const auto& via : data.vias) {
        if (!seen_names.insert("via:" + via.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_VIA",
                      "Duplicate VIA definition for '" + via.name + "'",
                      via.source_line);
        }
    }
    for (const auto& via_rule : data.via_rules) {
        if (!seen_names.insert("viarule:" + via_rule.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_VIARULE",
                      "Duplicate VIARULE definition for '" + via_rule.name + "'",
                      via_rule.source_line);
        }
    }
    for (const auto& rule : data.non_default_rules) {
        if (!seen_names.insert("ndr:" + rule.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_NONDEFAULTRULE",
                      "Duplicate NONDEFAULTRULE definition for '" + rule.name + "'",
                      rule.source_line);
        }
    }
    for (const auto& macro : data.macros) {
        if (!seen_names.insert("macro:" + macro.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_MACRO",
                      "Duplicate MACRO definition for '" + macro.name + "'",
                      macro.source_line);
        }
    }

    normalize_semantics(data);
    report_progress(100, 100, "Completed LEF parse");
    return data;
}

LefLibraryData LefParser::parse_file(const std::filesystem::path& path,
                                     const ICancellationToken& token,
                                     LefProgressCallback progress) const
{
    return parse_string(read_text_file(path), path.string(), token, std::move(progress));
}

} // namespace aegis::parsing
