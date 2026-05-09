#include "aegis/parsing/def_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aegis::parsing {

namespace {

inline bool is_space(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

inline std::string_view trim_view(std::string_view s)
{
    std::size_t a = 0;
    while (a < s.size() && is_space(s[a])) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && is_space(s[b - 1])) {
        --b;
    }
    return s.substr(a, b - a);
}

inline std::string trim_copy(const std::string& s)
{
    const auto v = trim_view(s);
    return std::string(v.begin(), v.end());
}

inline bool is_comment_or_empty(std::string_view s)
{
    const auto v = trim_view(s);
    return v.empty() || v.front() == '#' || v.front() == ';';
}

inline std::string unquote(const std::string& s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

inline double parse_double_or_throw(const std::string& tok, std::size_t line_no)
{
    try {
        std::size_t pos = 0;
        const double val = std::stod(tok, &pos);
        if (pos != tok.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return val;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Line " + std::to_string(line_no) +
            ": Invalid number token '" + tok + "'");
    }
}

inline int parse_int_or_throw(const std::string& tok, std::size_t line_no)
{
    try {
        std::size_t pos = 0;
        const int val = std::stoi(tok, &pos);
        if (pos != tok.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return val;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Line " + std::to_string(line_no) +
            ": Invalid integer token '" + tok + "'");
    }
}

inline std::vector<std::string> tokenise_simple(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string cur;
    bool in_quote = false;

    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            cur.push_back(c);
        } else if (is_space(c) && !in_quote) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }
    return tokens;
}

inline std::vector<std::string> tokenise_def_statement(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string cur;
    bool in_quote = false;

    auto flush = [&]() {
        if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    };

    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            cur.push_back(c);
            continue;
        }
        if (!in_quote && is_space(c)) {
            flush();
            continue;
        }
        if (!in_quote && (c == ';' || c == '(' || c == ')' || c == '+')) {
            flush();
            tokens.emplace_back(1, c);
            continue;
        }
        cur.push_back(c);
    }
    flush();
    return tokens;
}

inline bool is_pin_name(std::string_view key)
{
    static const std::unordered_set<std::string_view> names = {
        "gate", "source", "drain", "bulk", "base", "collector", "emitter",
        "anode", "cathode", "inv", "in", "out", "input", "output"
    };
    return names.count(key) != 0;
}

inline auto parse_mixed_net_args(
    const std::vector<std::string>& tokens,
    std::size_t first = 0)
    -> std::pair<std::vector<std::string>, std::map<std::string, std::string>>
{
    std::vector<std::string> pin_names;
    std::map<std::string, std::string> props;

    for (std::size_t i = first; i < tokens.size(); ++i) {
        if (tokens[i].find('=') != std::string::npos) {
            const auto pos = tokens[i].find('=');
            props[tokens[i].substr(0, pos)] = tokens[i].substr(pos + 1);
        } else {
            pin_names.push_back(tokens[i]);
        }
    }
    return {pin_names, props};
}

struct LogicalStatement {
    std::size_t line = 0;
    std::string text;
};

inline std::vector<LogicalStatement> read_logical_statements(const std::filesystem::path& path,
                                                             std::size_t& out_line_count)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    std::vector<LogicalStatement> statements;
    std::string raw_line;
    std::string current;
    std::size_t start_line = 0;
    std::size_t line_no = 0;

    while (std::getline(file, raw_line)) {
        ++line_no;
        const auto trimmed = trim_copy(raw_line);
        if (trimmed.empty() || is_comment_or_empty(trimmed)) {
            continue;
        }

        if (current.empty()) {
            start_line = line_no;
        } else {
            current.push_back(' ');
        }
        current += trimmed;

        const bool end_block_without_semicolon =
            trim_view(current).rfind("END ", 0) == 0 && trimmed.find(';') == std::string::npos;

        if (trimmed.find(';') != std::string::npos || end_block_without_semicolon) {
            statements.push_back({start_line, current});
            current.clear();
        }
    }

    if (!current.empty()) {
        statements.push_back({start_line, current});
    }

    out_line_count = line_no;
    return statements;
}

inline std::vector<LogicalStatement> read_nonempty_lines(const std::filesystem::path& path,
                                                         std::size_t& out_line_count)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    std::vector<LogicalStatement> lines;
    std::string raw_line;
    std::size_t line_no = 0;
    while (std::getline(file, raw_line)) {
        ++line_no;
        const auto trimmed = trim_copy(raw_line);
        if (trimmed.empty() || is_comment_or_empty(trimmed)) {
            continue;
        }
        lines.push_back({line_no, trimmed});
    }
    out_line_count = line_no;
    return lines;
}

inline bool looks_like_real_def(const std::vector<LogicalStatement>& statements)
{
    for (std::size_t i = 0; i < std::min<std::size_t>(statements.size(), 12); ++i) {
        const auto& s = statements[i].text;
        if (s.rfind("VERSION", 0) == 0 || s.rfind("DIVIDERCHAR", 0) == 0 ||
            s.rfind("BUSBITCHARS", 0) == 0 || s.rfind("DIEAREA", 0) == 0 ||
            s.rfind("COMPONENTS", 0) == 0 || s.rfind("PINS", 0) == 0) {
            return true;
        }
    }
    return false;
}

inline std::string default_layer_color(std::size_t index)
{
    static const char* palette[] = {
        "#4FC3F7", "#81C784", "#FFB74D", "#BA68C8", "#E57373",
        "#AED581", "#64B5F6", "#FFD54F", "#9575CD", "#4DB6AC"
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

struct RealDefState {
    std::unordered_map<std::string, std::size_t> layer_order;
    std::unordered_map<std::string, std::size_t> net_index;
    std::size_t row_count = 0;
    std::size_t track_count = 0;
    std::size_t gcellgrid_count = 0;
};

inline void ensure_layer(LayoutIR& ir, RealDefState& state, const std::string& layer_name)
{
    if (layer_name.empty()) {
        return;
    }
    if (state.layer_order.find(layer_name) != state.layer_order.end()) {
        return;
    }
    const std::size_t order = state.layer_order.size();
    state.layer_order[layer_name] = order;
    ir.layers.push_back(Layer{layer_name, "routing", static_cast<int>(order), default_layer_color(order)});
}

inline void add_rect_geometry(LayoutIR& ir,
                              RealDefState& state,
                              const std::string& layer_name,
                              double x1,
                              double y1,
                              double x2,
                              double y2)
{
    ensure_layer(ir, state, layer_name);
    const double min_x = std::min(x1, x2);
    const double min_y = std::min(y1, y2);
    const double max_x = std::max(x1, x2);
    const double max_y = std::max(y1, y2);
    ir.geometries.push_back(Geometry{layer_name, Rectangle{min_x, min_y, max_x - min_x, max_y - min_y}});
}

inline void emit_geometry_callback(IParserCallbacks* callbacks,
                                   const std::string& layer,
                                   double x1,
                                   double y1,
                                   double x2,
                                   double y2)
{
    if (!callbacks) {
        return;
    }
    ParsedGeometry pg;
    pg.layer = layer;
    pg.shape_type = "RECTANGLE";
    pg.points = {{x1, y1}, {x2, y2}};
    callbacks->on_geometry(pg);
}

inline std::string flatten_endpoint(const std::vector<std::string>& endpoint)
{
    if (endpoint.empty()) {
        return {};
    }
    if (endpoint.size() == 1) {
        return endpoint[0];
    }
    if (endpoint[0] == "PIN" && endpoint.size() >= 2) {
        return endpoint[1];
    }
    if (endpoint[0] == "*" && endpoint.size() >= 2) {
        return endpoint[1];
    }
    return endpoint[0] + "." + endpoint[1];
}

inline Net& ensure_net(LayoutIR& ir, RealDefState& state, const std::string& net_name)
{
    const auto it = state.net_index.find(net_name);
    if (it != state.net_index.end()) {
        return ir.nets[it->second];
    }
    state.net_index[net_name] = ir.nets.size();
    ir.nets.push_back(Net{net_name, {}, {}});
    return ir.nets.back();
}

inline std::optional<double> try_parse_double(const std::string& token)
{
    try {
        std::size_t pos = 0;
        const double value = std::stod(token, &pos);
        if (pos != token.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

bool parse_simplified_def(const std::vector<LogicalStatement>& statements,
                          std::size_t total_lines,
                          LayoutIR& ir,
                          std::vector<std::string>& errors,
                          IParserCallbacks* callbacks,
                          const ICancellationToken& token)
{
    bool has_design = false;
    bool end_design = false;
    std::size_t last_pct_reported = 0;

    auto report_error = [&](std::size_t line_no, const std::string& msg) {
        errors.push_back("Line " + std::to_string(line_no) + ": " + msg);
        if (callbacks) {
            callbacks->on_error(errors.back(), static_cast<int>(line_no));
        }
    };

    auto report_progress = [&](std::size_t line_no) {
        if (!callbacks || total_lines == 0) {
            return;
        }
        const std::size_t pct = (line_no * 100) / total_lines;
        if (pct > last_pct_reported) {
            last_pct_reported = pct;
            callbacks->on_progress({pct, 100, "Parsed " + std::to_string(line_no) + " lines"});
        }
    };

    std::size_t current_line = total_lines > 0 ? total_lines : 1;
    try {
        for (std::size_t stmt_idx = 0; stmt_idx < statements.size(); ++stmt_idx) {
            const auto& stmt = statements[stmt_idx];
            current_line = stmt.line;
            if (stmt_idx % 10 == 0 && token.is_cancelled()) {
                report_error(stmt.line, "Parse cancelled by user");
                return false;
            }
            report_progress(stmt.line);

            auto tokens = tokenise_simple(stmt.text);
            if (tokens.empty()) {
                continue;
            }

            const std::string& cmd = tokens[0];
            if (cmd == "DESIGN") {
                if (tokens.size() < 2) {
                    report_error(stmt.line, "DESIGN requires a name");
                    return false;
                }
                ir.design_name = tokens[1];
                has_design = true;
            } else if (cmd == "UNITS") {
                if (tokens.size() < 2) {
                    report_error(stmt.line, "UNITS requires a value");
                    return false;
                }
                ir.metadata["units"] = tokens[1];
            } else if (cmd == "LAYER") {
                if (tokens.size() < 5) {
                    report_error(stmt.line, "LAYER requires name, purpose, order, color");
                    return false;
                }
                ir.layers.push_back(Layer{tokens[1], tokens[2], parse_int_or_throw(tokens[3], stmt.line), tokens[4]});
            } else if (cmd == "RECT") {
                if (tokens.size() < 6) {
                    report_error(stmt.line, "RECT requires layer x y width height");
                    return false;
                }
                const std::string& layer = tokens[1];
                const double x = parse_double_or_throw(tokens[2], stmt.line);
                const double y = parse_double_or_throw(tokens[3], stmt.line);
                const double w = parse_double_or_throw(tokens[4], stmt.line);
                const double h = parse_double_or_throw(tokens[5], stmt.line);
                ir.geometries.push_back(Geometry{layer, Rectangle{x, y, w, h}});
                emit_geometry_callback(callbacks, layer, x, y, x + w, y + h);
            } else if (cmd == "POLY") {
                if (tokens.size() < 5 || ((tokens.size() - 2) % 2 != 0)) {
                    report_error(stmt.line, "POLY requires layer and even number of coordinates");
                    return false;
                }
                const std::string& layer = tokens[1];
                Polygon poly;
                for (std::size_t i = 2; i + 1 < tokens.size(); i += 2) {
                    poly.points.push_back({parse_double_or_throw(tokens[i], stmt.line), parse_double_or_throw(tokens[i + 1], stmt.line)});
                }
                ir.geometries.push_back(Geometry{layer, poly});
                if (callbacks) {
                    ParsedGeometry pg;
                    pg.layer = layer;
                    pg.shape_type = "POLYGON";
                    for (const auto& p : poly.points) {
                        pg.points.push_back({p.x, p.y});
                    }
                    callbacks->on_geometry(pg);
                }
            } else if (cmd == "DEVICE") {
                if (tokens.size() < 3) {
                    report_error(stmt.line, "DEVICE requires name and type");
                    return false;
                }
                Device dev;
                dev.name = tokens[1];
                dev.type = tokens[2];
                for (std::size_t i = 3; i < tokens.size(); ++i) {
                    const auto pos = tokens[i].find('=');
                    if (pos == std::string::npos) {
                        report_error(stmt.line, "DEVICE properties must be key=value pairs");
                        return false;
                    }
                    std::string key = tokens[i].substr(0, pos);
                    std::string value = tokens[i].substr(pos + 1);
                    if (is_pin_name(key)) {
                        dev.pins[key] = value;
                    } else {
                        dev.properties[key] = value;
                    }
                }
                ir.devices.push_back(dev);
                if (callbacks) {
                    ParsedCell cell;
                    cell.name = dev.name;
                    cell.properties = dev.properties;
                    cell.properties["type"] = dev.type;
                    for (const auto& [k, v] : dev.pins) {
                        cell.properties[k] = v;
                    }
                    callbacks->on_cell(cell);
                }
            } else if (cmd == "PORT") {
                if (tokens.size() < 6) {
                    report_error(stmt.line, "PORT requires name direction net_name [layer] x y");
                    return false;
                }
                Port port;
                port.name = tokens[1];
                port.direction = tokens[2];
                port.net_name = tokens[3];
                std::size_t idx = 4;
                if (tokens.size() == 7) {
                    port.layer = tokens[idx++];
                } else if (tokens.size() != 6) {
                    report_error(stmt.line, "PORT requires exactly 6 or 7 tokens");
                    return false;
                }
                port.location = Point{parse_double_or_throw(tokens[idx], stmt.line), parse_double_or_throw(tokens[idx + 1], stmt.line)};
                ir.ports.push_back(port);
                if (callbacks) {
                    callbacks->on_pin(ParsedPin{port.name, port.net_name, port.direction});
                }
            } else if (cmd == "NET") {
                if (tokens.size() < 3) {
                    report_error(stmt.line, "NET requires name and at least one pin");
                    return false;
                }
                auto [pin_names, props] = parse_mixed_net_args(tokens, 2);
                if (pin_names.empty()) {
                    report_error(stmt.line, "NET requires at least one pin name");
                    return false;
                }
                ir.nets.push_back(Net{tokens[1], pin_names, props});
                if (callbacks) {
                    callbacks->on_net(ParsedNet{tokens[1], pin_names, props});
                }
            } else if (cmd == "ANNOTATION") {
                if (tokens.size() < 5 || tokens.size() > 6) {
                    report_error(stmt.line, "ANNOTATION requires key value [layer] x y");
                    return false;
                }
                Annotation ann;
                ann.key = tokens[1];
                ann.value = unquote(tokens[2]);
                std::size_t idx = 3;
                if (tokens.size() == 6) {
                    ann.layer = tokens[idx++];
                }
                ann.position = Point{parse_double_or_throw(tokens[idx], stmt.line), parse_double_or_throw(tokens[idx + 1], stmt.line)};
                ir.annotations.push_back(ann);
            } else if (cmd == "END") {
                if (tokens.size() >= 2 && tokens[1] == "DESIGN") {
                    end_design = true;
                    break;
                }
                report_error(stmt.line, "Unexpected END without DESIGN");
                return false;
            } else {
                report_error(stmt.line, "Unknown command '" + cmd + "'");
                return false;
            }
        }
    } catch (const std::exception& e) {
        report_error(current_line, std::string("Unexpected error: ") + e.what());
        return false;
    }

    if (!has_design) {
        report_error(total_lines > 0 ? total_lines : 1, "Missing DESIGN declaration");
        return false;
    }
    if (!end_design) {
        report_error(total_lines > 0 ? total_lines : 1, "Missing END DESIGN");
        return false;
    }
    return errors.empty();
}

bool parse_real_def(const std::vector<LogicalStatement>& statements,
                    std::size_t total_lines,
                    LayoutIR& ir,
                    std::vector<std::string>& errors,
                    IParserCallbacks* callbacks,
                    const ICancellationToken& token)
{
    enum class Section {
        None,
        Vias,
        Components,
        Pins,
        SpecialNets,
        Nets
    };

    RealDefState real_state;
    Section section = Section::None;
    std::size_t last_pct_reported = 0;

    auto report_error = [&](std::size_t line_no, const std::string& msg) {
        errors.push_back("Line " + std::to_string(line_no) + ": " + msg);
        if (callbacks) {
            callbacks->on_error(errors.back(), static_cast<int>(line_no));
        }
    };

    auto report_progress = [&](std::size_t line_no) {
        if (!callbacks || total_lines == 0) {
            return;
        }
        const std::size_t pct = (line_no * 100) / total_lines;
        if (pct > last_pct_reported) {
            last_pct_reported = pct;
            callbacks->on_progress({pct, 100, "Parsed " + std::to_string(line_no) + " DEF lines"});
        }
    };

    auto set_declared_count = [&](const std::string& key, const std::vector<std::string>& tokens, std::size_t line) {
        if (tokens.size() >= 2) {
            ir.metadata[key] = std::to_string(parse_int_or_throw(tokens[1], line));
        }
    };

    auto parse_component = [&](const std::vector<std::string>& tokens, std::size_t line_no) {
        if (tokens.size() < 3 || tokens[0] != "-") {
            report_error(line_no, "Malformed COMPONENT entry");
            return;
        }

        Device dev;
        dev.name = tokens[1];
        dev.type = tokens[2];
        dev.properties["macro"] = tokens[2];

        for (std::size_t i = 3; i < tokens.size(); ++i) {
            if (tokens[i] != "+") {
                continue;
            }
            ++i;
            if (i >= tokens.size()) {
                break;
            }
            if (tokens[i] == "SOURCE" && i + 1 < tokens.size()) {
                dev.properties["source"] = tokens[++i];
            } else if ((tokens[i] == "PLACED" || tokens[i] == "FIXED") && i + 5 < tokens.size()) {
                const std::string status = tokens[i];
                if (tokens[i + 1] == "(" && tokens[i + 4] == ")") {
                    dev.properties["placement_status"] = status;
                    dev.properties["x"] = tokens[i + 2];
                    dev.properties["y"] = tokens[i + 3];
                    dev.properties["orient"] = tokens[i + 5];
                    i += 5;
                }
            }
        }

        ir.devices.push_back(dev);
        if (callbacks) {
            ParsedCell cell;
            cell.name = dev.name;
            cell.properties = dev.properties;
            cell.properties["type"] = dev.type;
            callbacks->on_cell(cell);
        }
    };

    auto parse_pin = [&](const std::vector<std::string>& tokens, std::size_t line_no) {
        if (tokens.size() < 2 || tokens[0] != "-") {
            report_error(line_no, "Malformed PINS entry");
            return;
        }

        Port port;
        port.name = tokens[1];
        std::optional<double> place_x;
        std::optional<double> place_y;
        std::vector<std::tuple<std::string, double, double, double, double>> local_rects;

        for (std::size_t i = 2; i < tokens.size(); ++i) {
            if (tokens[i] != "+") {
                continue;
            }
            ++i;
            if (i >= tokens.size()) {
                break;
            }
            if (tokens[i] == "NET" && i + 1 < tokens.size()) {
                port.net_name = tokens[++i];
            } else if (tokens[i] == "DIRECTION" && i + 1 < tokens.size()) {
                port.direction = tokens[++i];
            } else if (tokens[i] == "LAYER" && i + 9 < tokens.size()) {
                const std::string layer_name = tokens[i + 1];
                ensure_layer(ir, real_state, layer_name);
                port.layer = layer_name;
                if (tokens[i + 2] == "(" && tokens[i + 5] == ")" && tokens[i + 6] == "(" && tokens[i + 9] == ")") {
                    local_rects.emplace_back(
                        layer_name,
                        parse_double_or_throw(tokens[i + 3], line_no),
                        parse_double_or_throw(tokens[i + 4], line_no),
                        parse_double_or_throw(tokens[i + 7], line_no),
                        parse_double_or_throw(tokens[i + 8], line_no));
                }
            } else if ((tokens[i] == "PLACED" || tokens[i] == "FIXED") && i + 5 < tokens.size()) {
                if (tokens[i + 1] == "(" && tokens[i + 4] == ")") {
                    place_x = parse_double_or_throw(tokens[i + 2], line_no);
                    place_y = parse_double_or_throw(tokens[i + 3], line_no);
                    port.location = Point{*place_x, *place_y};
                }
            }
        }

        if (port.direction.empty()) {
            port.direction = "INOUT";
        }
        if (port.net_name.empty()) {
            port.net_name = port.name;
        }

        for (const auto& [layer, x1, y1, x2, y2] : local_rects) {
            if (place_x.has_value() && place_y.has_value()) {
                add_rect_geometry(ir, real_state, layer, *place_x + x1, *place_y + y1, *place_x + x2, *place_y + y2);
                emit_geometry_callback(callbacks, layer, *place_x + x1, *place_y + y1, *place_x + x2, *place_y + y2);
            }
        }

        ir.ports.push_back(port);
        Net& net = ensure_net(ir, real_state, port.net_name);
        net.pin_names.push_back(port.name);

        if (callbacks) {
            callbacks->on_pin(ParsedPin{port.name, port.net_name, port.direction});
        }
    };

    auto parse_route_segments = [&](const std::vector<std::string>& tokens,
                                    std::size_t& i,
                                    std::size_t line_no,
                                    bool emit_geometry) -> std::string {
        if (i >= tokens.size()) {
            return {};
        }
        const std::string layer_name = tokens[i++];
        ensure_layer(ir, real_state, layer_name);

        double width = 0.0;
        if (i < tokens.size()) {
            const auto maybe_width = try_parse_double(tokens[i]);
            if (maybe_width.has_value()) {
                width = *maybe_width;
                ++i;
            }
        }

        std::optional<Point> previous;
        while (i < tokens.size()) {
            if (tokens[i] == "+" || tokens[i] == "NEW") {
                break;
            }
            if (tokens[i] != "(") {
                ++i;
                continue;
            }
            if (i + 3 >= tokens.size()) {
                break;
            }

            const std::string x_tok = tokens[i + 1];
            const std::string y_tok = tokens[i + 2];
            std::size_t j = i + 3;
            while (j < tokens.size() && tokens[j] != ")") {
                ++j;
            }
            if (j >= tokens.size()) {
                break;
            }
            i = j + 1;

            Point current{};
            if (x_tok == "*" && previous.has_value()) {
                current.x = previous->x;
            } else {
                current.x = parse_double_or_throw(x_tok, line_no);
            }
            if (y_tok == "*" && previous.has_value()) {
                current.y = previous->y;
            } else {
                current.y = parse_double_or_throw(y_tok, line_no);
            }

            if (previous.has_value() && emit_geometry) {
                const double half = std::max(width / 2.0, 1.0);
                if (std::abs(previous->x - current.x) < 1e-9) {
                    add_rect_geometry(ir, real_state, layer_name,
                                      current.x - half, previous->y,
                                      current.x + half, current.y);
                    emit_geometry_callback(callbacks, layer_name,
                                           current.x - half, previous->y,
                                           current.x + half, current.y);
                } else if (std::abs(previous->y - current.y) < 1e-9) {
                    add_rect_geometry(ir, real_state, layer_name,
                                      previous->x, current.y - half,
                                      current.x, current.y + half);
                    emit_geometry_callback(callbacks, layer_name,
                                           previous->x, current.y - half,
                                           current.x, current.y + half);
                }
            }
            previous = current;
        }

        return layer_name;
    };

    auto parse_net_like = [&](const std::vector<std::string>& tokens, std::size_t line_no, bool special) {
        if (tokens.size() < 2 || tokens[0] != "-") {
            report_error(line_no, special ? "Malformed SPECIALNETS entry" : "Malformed NETS entry");
            return;
        }

        Net& net = ensure_net(ir, real_state, tokens[1]);
        if (special) {
            net.properties["special"] = "true";
        }

        std::size_t i = 2;
        while (i < tokens.size()) {
            if (tokens[i] == "(") {
                std::vector<std::string> endpoint;
                ++i;
                while (i < tokens.size() && tokens[i] != ")") {
                    endpoint.push_back(tokens[i++]);
                }
                if (i < tokens.size() && tokens[i] == ")") {
                    ++i;
                }
                const auto flat = flatten_endpoint(endpoint);
                if (!flat.empty()) {
                    net.pin_names.push_back(flat);
                }
                continue;
            }
            if (tokens[i] == "+") {
                ++i;
                if (i >= tokens.size()) {
                    break;
                }
                if (tokens[i] == "USE" && i + 1 < tokens.size()) {
                    net.properties["use"] = tokens[++i];
                    ++i;
                    continue;
                }
                if (tokens[i] == "ROUTED") {
                    ++i;
                    const std::string layer = parse_route_segments(tokens, i, line_no, !special);
                    if (!layer.empty()) {
                        net.properties["last_route_layer"] = layer;
                    }
                    continue;
                }
            }
            if (tokens[i] == "NEW") {
                ++i;
                const std::string layer = parse_route_segments(tokens, i, line_no, !special);
                if (!layer.empty()) {
                    net.properties["last_route_layer"] = layer;
                }
                continue;
            }
            ++i;
        }

        if (callbacks) {
            callbacks->on_net(ParsedNet{net.name, net.pin_names, net.properties});
        }
    };

    try {
        for (std::size_t stmt_idx = 0; stmt_idx < statements.size(); ++stmt_idx) {
            const auto& stmt = statements[stmt_idx];
            if (stmt_idx % 8 == 0 && token.is_cancelled()) {
                report_error(stmt.line, "Parse cancelled by user");
                return false;
            }
            report_progress(stmt.line);

            const auto tokens = tokenise_def_statement(stmt.text);
            if (tokens.empty()) {
                continue;
            }

            if (tokens[0] == "VERSION" && tokens.size() >= 2) {
                ir.metadata["def_version"] = tokens[1];
                continue;
            }
            if (tokens[0] == "DIVIDERCHAR" && tokens.size() >= 2) {
                ir.metadata["divider_char"] = unquote(tokens[1]);
                continue;
            }
            if (tokens[0] == "BUSBITCHARS" && tokens.size() >= 2) {
                ir.metadata["bus_bit_chars"] = unquote(tokens[1]);
                continue;
            }
            if (tokens[0] == "DESIGN" && tokens.size() >= 2) {
                ir.design_name = tokens[1];
                continue;
            }
            if (tokens[0] == "UNITS" && tokens.size() >= 4) {
                ir.metadata["units_domain"] = tokens[1];
                ir.metadata["units_kind"] = tokens[2];
                ir.metadata["units"] = tokens[3];
                continue;
            }
            if (tokens[0] == "DIEAREA") {
                if (tokens.size() >= 10) {
                    const double x1 = parse_double_or_throw(tokens[2], stmt.line);
                    const double y1 = parse_double_or_throw(tokens[3], stmt.line);
                    const double x2 = parse_double_or_throw(tokens[6], stmt.line);
                    const double y2 = parse_double_or_throw(tokens[7], stmt.line);
                    ir.metadata["diearea"] = std::to_string(x1) + "," + std::to_string(y1) + "," + std::to_string(x2) + "," + std::to_string(y2);
                    add_rect_geometry(ir, real_state, "__diearea__", x1, y1, x2, y2);
                    emit_geometry_callback(callbacks, "__diearea__", x1, y1, x2, y2);
                }
                continue;
            }
            if (tokens[0] == "ROW") {
                ++real_state.row_count;
                ir.metadata["row_count"] = std::to_string(real_state.row_count);
                continue;
            }
            if (tokens[0] == "TRACKS") {
                ++real_state.track_count;
                ir.metadata["track_count"] = std::to_string(real_state.track_count);
                for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
                    if (tokens[i] == "LAYER") {
                        ensure_layer(ir, real_state, tokens[i + 1]);
                    }
                }
                continue;
            }
            if (tokens[0] == "GCELLGRID") {
                ++real_state.gcellgrid_count;
                ir.metadata["gcellgrid_count"] = std::to_string(real_state.gcellgrid_count);
                continue;
            }
            if (tokens[0] == "VIAS") {
                section = Section::Vias;
                set_declared_count("via_count_declared", tokens, stmt.line);
                continue;
            }
            if (tokens[0] == "COMPONENTS") {
                section = Section::Components;
                set_declared_count("component_count_declared", tokens, stmt.line);
                continue;
            }
            if (tokens[0] == "PINS") {
                section = Section::Pins;
                set_declared_count("pin_count_declared", tokens, stmt.line);
                continue;
            }
            if (tokens[0] == "SPECIALNETS") {
                section = Section::SpecialNets;
                set_declared_count("specialnet_count_declared", tokens, stmt.line);
                continue;
            }
            if (tokens[0] == "NETS") {
                section = Section::Nets;
                set_declared_count("net_count_declared", tokens, stmt.line);
                continue;
            }
            if (tokens[0] == "END") {
                if (tokens.size() >= 2 && tokens[1] == "DESIGN") {
                    break;
                }
                section = Section::None;
                continue;
            }

            switch (section) {
            case Section::Vias:
                if (tokens[0] == "-" && tokens.size() >= 2) {
                    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
                        if (tokens[i] == "LAYERS") {
                            ensure_layer(ir, real_state, tokens[i + 1]);
                            if (i + 3 < tokens.size()) {
                                ensure_layer(ir, real_state, tokens[i + 3]);
                            }
                        }
                    }
                }
                break;
            case Section::Components:
                parse_component(tokens, stmt.line);
                break;
            case Section::Pins:
                parse_pin(tokens, stmt.line);
                break;
            case Section::SpecialNets:
                parse_net_like(tokens, stmt.line, true);
                break;
            case Section::Nets:
                parse_net_like(tokens, stmt.line, false);
                break;
            case Section::None:
                break;
            }
        }
    } catch (const std::exception& e) {
        report_error(total_lines > 0 ? total_lines : 1, std::string("Unexpected error: ") + e.what());
        return false;
    }

    if (ir.metadata.find("component_count_declared") == ir.metadata.end()) {
        ir.metadata["component_count_declared"] = std::to_string(ir.devices.size());
    }
    if (ir.metadata.find("pin_count_declared") == ir.metadata.end()) {
        ir.metadata["pin_count_declared"] = std::to_string(ir.ports.size());
    }
    if (ir.metadata.find("specialnet_count_declared") == ir.metadata.end()) {
        std::size_t count = 0;
        for (const auto& net : ir.nets) {
            if (const auto it = net.properties.find("special"); it != net.properties.end() && it->second == "true") {
                ++count;
            }
        }
        ir.metadata["specialnet_count_declared"] = std::to_string(count);
    }
    if (ir.metadata.find("net_count_declared") == ir.metadata.end()) {
        std::size_t count = 0;
        for (const auto& net : ir.nets) {
            if (const auto it = net.properties.find("special"); it == net.properties.end() || it->second != "true") {
                ++count;
            }
        }
        ir.metadata["net_count_declared"] = std::to_string(count);
    }

    if (ir.design_name.empty()) {
        report_error(total_lines > 0 ? total_lines : 1, "Missing DESIGN declaration");
        return false;
    }

    return errors.empty();
}

} // anonymous namespace

std::string DefParser::format_name() const
{
    return "DEF-like";
}

bool DefParser::parse(const std::filesystem::path& path,
                      IParserCallbacks& callbacks,
                      const ICancellationToken& token)
{
    callbacks.on_begin(path);
    Context ctx;
    bool ok = false;
    try {
        ok = parse_file(path, ctx, &callbacks, token);
    } catch (const std::exception& e) {
        if (ctx.errors.empty()) {
            ctx.errors.push_back(e.what());
        }
        for (const auto& err : ctx.errors) {
            callbacks.on_error(err, std::nullopt);
        }
        callbacks.on_end(false);
        return false;
    }
    if (!ok) {
        for (const auto& err : ctx.errors) {
            callbacks.on_error(err, std::nullopt);
        }
    }
    callbacks.on_end(ok);
    return ok;
}

LayoutIR DefParser::parse_to_layout_ir(const std::filesystem::path& path,
                                       const ICancellationToken& token)
{
    Context ctx;
    const bool ok = parse_file(path, ctx, nullptr, token);
    if (!ok) {
        throw LayoutIRError(ctx.errors.empty() ? "DEF parse failed" : ctx.errors.front());
    }
    return std::move(ctx.ir);
}

bool DefParser::parse_file(const std::filesystem::path& path,
                           Context& ctx,
                           IParserCallbacks* callbacks,
                           const ICancellationToken& token)
{
    std::size_t total_lines = 0;
    std::vector<LogicalStatement> statements;
    try {
        statements = read_logical_statements(path, total_lines);
    } catch (const std::exception& e) {
        ctx.errors.push_back(e.what());
        if (callbacks) {
            callbacks->on_error(ctx.errors.back(), std::nullopt);
        }
        return false;
    }

    if (callbacks) {
        callbacks->on_progress({0, 100, "Opening file"});
    }

    if (looks_like_real_def(statements)) {
        return parse_real_def(statements, total_lines, ctx.ir, ctx.errors, callbacks, token);
    }

    try {
        statements = read_nonempty_lines(path, total_lines);
    } catch (const std::exception& e) {
        ctx.errors.push_back(e.what());
        if (callbacks) {
            callbacks->on_error(ctx.errors.back(), std::nullopt);
        }
        return false;
    }
    return parse_simplified_def(statements, total_lines, ctx.ir, ctx.errors, callbacks, token);
}

} // namespace aegis::parsing
