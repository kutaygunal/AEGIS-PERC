#include "aegis/parsing/def_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace aegis::parsing {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline bool is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c));
}

inline std::string_view trim_view(std::string_view s) {
    std::size_t a = 0;
    while (a < s.size() && is_space(s[a])) ++a;
    std::size_t b = s.size();
    while (b > a && is_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

/** Tokeniser that preserves double-quoted strings with spaces. */
inline std::vector<std::string> tokenise(const std::string& line) {
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

inline bool is_comment_or_empty(std::string_view s) {
    auto v = trim_view(s);
    return v.empty() || v.front() == '#' || v.front() == ';';
}

inline double parse_double(const std::string& tok, std::size_t line_no) {
    try {
        std::size_t pos = 0;
        double val = std::stod(tok, &pos);
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

inline int parse_int(const std::string& tok, std::size_t line_no) {
    try {
        std::size_t pos = 0;
        int val = std::stoi(tok, &pos);
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

inline std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

/** Known pin names used to separate pins from generic properties. */
inline bool is_pin_name(std::string_view key) {
    static const std::unordered_set<std::string_view> names = {
        "gate", "source", "drain", "bulk", "base",
        "collector", "emitter", "anode", "cathode",
        "inv", "in", "out", "input", "output"
    };
    return names.count(key) != 0;
}

inline auto parse_kv_pairs(
    [[maybe_unused]] std::size_t line_no,
    const std::vector<std::string>& tokens,
    std::size_t first = 0)
    -> std::map<std::string, std::string> {
    std::map<std::string, std::string> result;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        auto pos = tokens[i].find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error(
                "Line " + std::to_string(line_no) +
                ": Expected key=value pair, got '" + tokens[i] + "'");
        }
        result[tokens[i].substr(0, pos)] = tokens[i].substr(pos + 1);
    }
    return result;
}

inline auto parse_mixed_net_args(
    [[maybe_unused]] std::size_t line_no,
    const std::vector<std::string>& tokens,
    std::size_t first = 0)
    -> std::pair<std::vector<std::string>, std::map<std::string, std::string>> {
    std::vector<std::string> pin_names;
    std::map<std::string, std::string> props;

    for (std::size_t i = first; i < tokens.size(); ++i) {
        if (tokens[i].find('=') != std::string::npos) {
            auto pos = tokens[i].find('=');
            props[tokens[i].substr(0, pos)] = tokens[i].substr(pos + 1);
        } else {
            pin_names.push_back(tokens[i]);
        }
    }
    return {pin_names, props};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string DefParser::format_name() const {
    return "DEF-like";
}

bool DefParser::parse(const std::filesystem::path& path,
                      IParserCallbacks& callbacks,
                      const ICancellationToken& token) {
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
                                       const ICancellationToken& token) {
    Context ctx;
    bool ok = parse_file(path, ctx, nullptr, token);
    if (!ok) {
        throw LayoutIRError(ctx.errors.empty()
            ? "DEF-like parse failed"
            : ctx.errors.front());
    }
    return std::move(ctx.ir);
}

// ---------------------------------------------------------------------------
// Internal implementation
// ---------------------------------------------------------------------------

bool DefParser::parse_file(const std::filesystem::path& path,
                             Context& ctx,
                             IParserCallbacks* callbacks,
                             const ICancellationToken& token) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::string msg = "Cannot open file: " + path.string();
        ctx.errors.push_back(msg);
        if (callbacks) {
            callbacks->on_error(msg, std::nullopt);
        }
        return false;
    }

    file.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (callbacks) {
        callbacks->on_progress({0, 100, "Opening file"});
    }

    std::string line;
    std::size_t line_no = 0;
    std::size_t bytes_read = 0;
    std::size_t last_pct_reported = 0;
    bool has_design = false;
    bool end_design = false;

    auto report_error = [&](const std::string& msg) {
        ctx.errors.push_back("Line " + std::to_string(line_no) + ": " + msg);
        if (callbacks) {
            callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
        }
    };

    auto report_progress = [&]() {
        if (!callbacks || file_size == 0) return;
        std::size_t pct = (bytes_read * 100) / file_size;
        if (pct > last_pct_reported) {
            last_pct_reported = pct;
            callbacks->on_progress({pct, 100,
                "Parsed " + std::to_string(line_no) + " lines"});
        }
    };

    try {
        while (std::getline(file, line)) {
            ++line_no;
            bytes_read += line.size() + 1; // +1 for newline char

            // Periodic cancellation check
            if (line_no % 10 == 0) {
                if (token.is_cancelled()) {
                    report_error("Parse cancelled by user");
                    return false;
                }
            }
            report_progress();

            if (is_comment_or_empty(line)) continue;

            auto tokens = tokenise(line);
            if (tokens.empty()) continue;

            const std::string& cmd = tokens[0];

            // ---------------------------------------------------------------
            // DESIGN
            // ---------------------------------------------------------------
            if (cmd == "DESIGN") {
                if (tokens.size() < 2) {
                    report_error("DESIGN requires a name");
                    return false;
                }
                ctx.ir.design_name = tokens[1];
                has_design = true;
            }
            // ---------------------------------------------------------------
            // UNITS
            // ---------------------------------------------------------------
            else if (cmd == "UNITS") {
                if (tokens.size() < 2) {
                    report_error("UNITS requires a value");
                    return false;
                }
                ctx.ir.metadata["units"] = tokens[1];
            }
            // ---------------------------------------------------------------
            // LAYER
            // ---------------------------------------------------------------
            else if (cmd == "LAYER") {
                if (tokens.size() < 5) {
                    report_error("LAYER requires name, purpose, order, color");
                    return false;
                }
                Layer lyr;
                lyr.name    = tokens[1];
                lyr.purpose = tokens[2];
                lyr.order   = parse_int(tokens[3], line_no);
                lyr.color   = tokens[4];
                ctx.ir.layers.push_back(lyr);
                // No callback: IParserCallbacks has no on_layer()
            }
            // ---------------------------------------------------------------
            // RECT
            // ---------------------------------------------------------------
            else if (cmd == "RECT") {
                if (tokens.size() < 6) {
                    report_error("RECT requires layer x y width height");
                    return false;
                }
                const std::string& layer = tokens[1];
                double x = parse_double(tokens[2], line_no);
                double y = parse_double(tokens[3], line_no);
                double w = parse_double(tokens[4], line_no);
                double h = parse_double(tokens[5], line_no);

                ctx.ir.geometries.push_back(Geometry{layer, Rectangle{x, y, w, h}});

                if (callbacks) {
                    ParsedGeometry pg;
                    pg.layer      = layer;
                    pg.shape_type = "RECTANGLE";
                    pg.points     = {{x, y}, {x + w, y + h}};
                    callbacks->on_geometry(pg);
                }
            }
            // ---------------------------------------------------------------
            // POLY
            // ---------------------------------------------------------------
            else if (cmd == "POLY") {
                if (tokens.size() < 5 || ((tokens.size() - 2) % 2 != 0)) {
                    report_error("POLY requires layer and even number of coordinates");
                    return false;
                }
                const std::string& layer = tokens[1];
                Polygon poly;
                for (std::size_t i = 2; i + 1 < tokens.size(); i += 2) {
                    double x = parse_double(tokens[i],   line_no);
                    double y = parse_double(tokens[i+1], line_no);
                    poly.points.push_back({x, y});
                }
                ctx.ir.geometries.push_back(Geometry{layer, poly});

                if (callbacks) {
                    ParsedGeometry pg;
                    pg.layer      = layer;
                    pg.shape_type = "POLYGON";
                    for (const auto& p : poly.points) {
                        pg.points.push_back({p.x, p.y});
                    }
                    callbacks->on_geometry(pg);
                }
            }
            // ---------------------------------------------------------------
            // DEVICE
            // ---------------------------------------------------------------
            else if (cmd == "DEVICE") {
                if (tokens.size() < 3) {
                    report_error("DEVICE requires name and type");
                    return false;
                }
                Device dev;
                dev.name = tokens[1];
                dev.type = tokens[2];

                for (std::size_t i = 3; i < tokens.size(); ++i) {
                    auto pos = tokens[i].find('=');
                    if (pos == std::string::npos) {
                        report_error("DEVICE properties must be key=value pairs");
                        return false;
                    }
                    std::string key   = tokens[i].substr(0, pos);
                    std::string value = tokens[i].substr(pos + 1);
                    if (is_pin_name(key)) {
                        dev.pins[key] = value;
                    } else {
                        dev.properties[key] = value;
                    }
                }
                ctx.ir.devices.push_back(dev);

                if (callbacks) {
                    ParsedCell cell;
                    cell.name       = dev.name;
                    cell.properties = dev.properties;
                    cell.properties["type"] = dev.type;
                    for (const auto& [k, v] : dev.pins) {
                        cell.properties[k] = v;
                    }
                    callbacks->on_cell(cell);
                }
            }
            // ---------------------------------------------------------------
            // PORT
            // ---------------------------------------------------------------
            else if (cmd == "PORT") {
                // PORT name direction net_name [layer] x y
                if (tokens.size() < 6) {
                    report_error(
                        "PORT requires name direction net_name [layer] x y");
                    return false;
                }
                Port port;
                port.name      = tokens[1];
                port.direction = tokens[2];
                port.net_name  = tokens[3];

                std::size_t idx = 4;
                // If 7 tokens: layer is present, coords are tokens 5,6
                // If 6 tokens: no layer, coords are tokens 4,5
                if (tokens.size() == 7) {
                    port.layer = tokens[idx++];
                } else if (tokens.size() != 6) {
                    report_error(
                        "PORT requires exactly 6 or 7 tokens"
                        " (name direction net_name [layer] x y)");
                    return false;
                }
                double x = parse_double(tokens[idx],     line_no);
                double y = parse_double(tokens[idx + 1], line_no);
                port.location = Point{x, y};
                ctx.ir.ports.push_back(port);

                if (callbacks) {
                    ParsedPin pin;
                    pin.name      = port.name;
                    pin.direction = port.direction;
                    pin.net_name  = port.net_name;
                    callbacks->on_pin(pin);
                }
            }
            // ---------------------------------------------------------------
            // NET
            // ---------------------------------------------------------------
            else if (cmd == "NET") {
                if (tokens.size() < 3) {
                    report_error("NET requires name and at least one pin");
                    return false;
                }
                auto [pin_names, props] = parse_mixed_net_args(line_no, tokens, 2);
                if (pin_names.empty()) {
                    report_error("NET requires at least one pin name");
                    return false;
                }
                Net net;
                net.name      = tokens[1];
                net.pin_names = pin_names;
                net.properties = props;
                ctx.ir.nets.push_back(net);

                if (callbacks) {
                    ParsedNet pn;
                    pn.name       = net.name;
                    pn.pin_names  = net.pin_names;
                    pn.properties = net.properties;
                    callbacks->on_net(pn);
                }
            }
            // ---------------------------------------------------------------
            // ANNOTATION
            // ---------------------------------------------------------------
            else if (cmd == "ANNOTATION") {
                // ANNOTATION key value [layer] x y
                if (tokens.size() < 5 || tokens.size() > 6) {
                    report_error(
                        "ANNOTATION requires key value [layer] x y"
                        " (5 or 6 tokens)");
                    return false;
                }
                Annotation ann;
                ann.key   = tokens[1];
                ann.value = unquote(tokens[2]);

                std::size_t idx = 3;
                if (tokens.size() == 6) {
                    ann.layer = tokens[idx++];
                }
                double x = parse_double(tokens[idx],     line_no);
                double y = parse_double(tokens[idx + 1], line_no);
                ann.position = Point{x, y};
                ctx.ir.annotations.push_back(ann);
            }
            // ---------------------------------------------------------------
            // END DESIGN
            // ---------------------------------------------------------------
            else if (cmd == "END") {
                if (tokens.size() >= 2 && tokens[1] == "DESIGN") {
                    end_design = true;
                    break;
                }
                report_error("Unexpected END without DESIGN");
                return false;
            }
            // ---------------------------------------------------------------
            // Unknown command
            // ---------------------------------------------------------------
            else {
                report_error("Unknown command '" + cmd + "'");
                return false;
            }
        }
    } catch (const std::exception& e) {
        report_error(std::string("Unexpected error: ") + e.what());
        return false;
    }

    if (!has_design) {
        report_error("Missing DESIGN declaration");
        return false;
    }
    if (!end_design) {
        report_error("Missing END DESIGN");
        return false;
    }

    return ctx.errors.empty();
}

} // namespace aegis::parsing
