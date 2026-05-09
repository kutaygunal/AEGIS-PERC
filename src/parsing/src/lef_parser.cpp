#include "aegis/parsing/lef_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace aegis::parsing {
namespace {

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

bool is_known_property_like_statement(const std::string& keyword)
{
    static const std::unordered_set<std::string> known{
        "WIDTH", "SPACING", "PITCH", "OFFSET", "DIRECTION", "RESISTANCE",
        "CAPACITANCE", "AREA", "THICKNESS", "WIREEXTENSION", "USEMINSPACING",
        "EDGECAPACITANCE", "PROPERTY", "PROPERTYDEFINITIONS", "NAMESCASESENSITIVE",
        "MANUFACTURINGGRID", "UNITS", "DATABASE", "ENDCAP", "SYMMETRY", "SITE",
        "ANTENNAGATEAREA", "ANTENNADIFFAREA", "ANTENNAPARTIALMETALAREA",
        "ANTENNAPARTIALMETALSIDEAREA", "ANTENNAPARTIALCUTAREA", "SHAPE"
    };
    return known.count(keyword) != 0;
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
        Units
    };

    struct State {
        Context context = Context::Top;
        LefSite site;
        LefLayer layer;
        LefVia via;
        LefMacro macro;
        LefPin pin;
        LefPort port;
        LefObs obs;
        LefLayerGeometry* active_geometry = nullptr;
        std::size_t property_definitions_depth = 0;
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

    const std::size_t total_bytes = content.size();
    std::istringstream input(content);
    std::string raw_line;
    std::size_t line_no = 0;
    std::size_t bytes_read = 0;
    std::size_t last_pct_reported = 0;
    bool saw_end_library = false;

    report_progress(0, 100, "Opening LEF content");

    while (std::getline(input, raw_line)) {
        ++line_no;
        bytes_read += raw_line.size() + 1;

        if (line_no % 32 == 0 && token.is_cancelled()) {
            push_diag(LefDiagnostic::Severity::Error,
                      "LEF_PARSE_CANCELLED",
                      "LEF parse cancelled by user",
                      line_no);
            break;
        }

        if (progress && total_bytes > 0) {
            const std::size_t pct = (bytes_read * 100) / total_bytes;
            if (pct > last_pct_reported) {
                last_pct_reported = pct;
                report_progress(pct, 100, "Parsed " + std::to_string(line_no) + " LEF lines");
            }
        }

        const std::string stripped = trim(strip_comment(raw_line));
        if (stripped.empty()) {
            continue;
        }

        const auto tokens = tokenize(stripped);
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
        if (keyword == "NOWIREEXTENSIONATPIN") {
            data.properties[keyword] = tokens.size() > 1 ? join_tokens(tokens, 1) : "ON";
            continue;
        }
        if (keyword == "NAMESCASESENSITIVE" || keyword == "MANUFACTURINGGRID") {
            data.properties[keyword] = join_tokens(tokens, 1);
            continue;
        }
        if (keyword == "PROPERTYDEFINITIONS") {
            state.context = Context::PropertyDefinitions;
            state.property_definitions_depth = 1;
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
                state.context = Context::Via;
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

            if (!is_known_property_like_statement(keyword)) {
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
                } else {
                    set_property(state.layer.properties, keyword, tokens);
                }
                break;

            case Context::Via:
                if (keyword == "LAYER") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_VIA_LAYER_INVALID", "VIA LAYER requires a layer name", line_no);
                        break;
                    }
                    state.via.layers.push_back({tokens[1], {}, line_no});
                    state.active_geometry = &state.via.layers.back();
                } else if (keyword == "RECT") {
                    if (tokens.size() < 5 || !require_geometry_target(line_no)) {
                        if (tokens.size() < 5) {
                            push_diag(LefDiagnostic::Severity::Error, "LEF_RECT_INVALID", "RECT requires four coordinates", line_no);
                        }
                        break;
                    }
                    LefRect rect{parse_double(tokens[1]), parse_double(tokens[2]), parse_double(tokens[3]), parse_double(tokens[4]), line_no};
                    normalize_rect(rect);
                    state.active_geometry->rects.push_back(rect);
                } else {
                    set_property(state.via.properties, keyword, tokens);
                }
                break;

            case Context::Macro:
                if (keyword == "CLASS") {
                    state.macro.macro_class = join_tokens(tokens, 1);
                } else if (keyword == "FOREIGN") {
                    state.macro.foreign_name = tokens.size() >= 2 ? tokens[1] : "";
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
                } else {
                    set_property(state.macro.properties, keyword, tokens);
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
                } else {
                    set_property(state.pin.properties, keyword, tokens);
                }
                break;

            case Context::Port:
                if (keyword == "LAYER") {
                    if (tokens.size() < 2) {
                        push_diag(LefDiagnostic::Severity::Error, "LEF_PORT_LAYER_INVALID", "PORT LAYER requires a layer name", line_no);
                        break;
                    }
                    state.port.layers.push_back({tokens[1], {}, line_no});
                    state.active_geometry = &state.port.layers.back();
                } else if (keyword == "RECT") {
                    if (tokens.size() < 5 || !require_geometry_target(line_no)) {
                        if (tokens.size() < 5) {
                            push_diag(LefDiagnostic::Severity::Error, "LEF_RECT_INVALID", "RECT requires four coordinates", line_no);
                        }
                        break;
                    }
                    LefRect rect{parse_double(tokens[1]), parse_double(tokens[2]), parse_double(tokens[3]), parse_double(tokens[4]), line_no};
                    normalize_rect(rect);
                    state.active_geometry->rects.push_back(rect);
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
                    state.obs.layers.push_back({tokens[1], {}, line_no});
                    state.active_geometry = &state.obs.layers.back();
                } else if (keyword == "RECT") {
                    if (tokens.size() < 5 || !require_geometry_target(line_no)) {
                        if (tokens.size() < 5) {
                            push_diag(LefDiagnostic::Severity::Error, "LEF_RECT_INVALID", "RECT requires four coordinates", line_no);
                        }
                        break;
                    }
                    LefRect rect{parse_double(tokens[1]), parse_double(tokens[2]), parse_double(tokens[3]), parse_double(tokens[4]), line_no};
                    normalize_rect(rect);
                    state.active_geometry->rects.push_back(rect);
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
                      "Invalid numeric value in statement '" + stripped + "'",
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
    for (const auto& macro : data.macros) {
        if (!seen_names.insert("macro:" + macro.name).second) {
            push_diag(LefDiagnostic::Severity::Warning,
                      "LEF_DUPLICATE_MACRO",
                      "Duplicate MACRO definition for '" + macro.name + "'",
                      macro.source_line);
        }
    }

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
