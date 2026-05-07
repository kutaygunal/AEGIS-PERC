#include "aegis/parsing/spice_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace aegis::parsing {

namespace {

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

// ---------------------------------------------------------------------------
// Tokeniser
// ---------------------------------------------------------------------------

/** Split a line into whitespace-separated tokens. */
inline std::vector<std::string> tokenise(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : line) {
        if (is_space(c)) {
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

/** Strip inline comments starting at ; or $ or //. */
inline std::string strip_inline_comments(std::string_view line) {
    std::size_t pos = std::string::npos;
    // find ';' not inside quotes
    bool in_quote = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            in_quote = !in_quote;
        } else if (!in_quote) {
            if (line[i] == ';' || line[i] == '$') {
                pos = i;
                break;
            }
            if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
                pos = i;
                break;
            }
        }
    }
    if (pos != std::string::npos) {
        return std::string(trim_view(line.substr(0, pos)));
    }
    return std::string(line);
}

/**
 * Extract value string from tokens until the first key=value pair.
 * Returns the value string and index of first property token.
 */
inline std::pair<std::string, std::size_t> extract_value_string(
    const std::vector<std::string>& tokens,
    std::size_t value_start) {
    std::string value;
    std::size_t i = value_start;
    for (; i < tokens.size(); ++i) {
        if (tokens[i].find('=') != std::string::npos) {
            break;
        }
        if (!value.empty()) value += " ";
        value += tokens[i];
    }
    return {value, i};
}

inline std::map<std::string, std::string> parse_kv_pairs(
    [[maybe_unused]] std::size_t line_no,
    const std::vector<std::string>& tokens,
    std::size_t first = 0)
{
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string SpiceParser::format_name() const {
    return "SPICE-like";
}

bool SpiceParser::parse(const std::filesystem::path& path,
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

LayoutIR SpiceParser::parse_to_layout_ir(const std::filesystem::path& path,
                                          const ICancellationToken& token) {
    Context ctx;
    bool ok = parse_file(path, ctx, nullptr, token);
    if (!ok) {
        throw LayoutIRError(ctx.errors.empty()
            ? "SPICE-like parse failed"
            : ctx.errors.front());
    }
    return std::move(ctx.ir);
}

// ---------------------------------------------------------------------------
// Internal implementation
// ---------------------------------------------------------------------------

bool SpiceParser::parse_file(const std::filesystem::path& path,
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
    (void)file.tellg(); // size available if needed for progress
    file.seekg(0, std::ios::beg);

    if (callbacks) {
        callbacks->on_progress({0, 100, "Opening file"});
    }

    // -----------------------------------------------------------------------
    // Pre-process lines: handle continuation and comments
    // -----------------------------------------------------------------------
    std::vector<std::pair<std::size_t, std::string>> processed_lines;
    {
        std::string line;
        std::size_t line_no = 0;
        std::string pending;
        std::size_t pending_line_no = 0;

        while (std::getline(file, line)) {
            ++line_no;
            std::string stripped = strip_inline_comments(line);
            auto trimmed = trim_view(stripped);

            if (trimmed.empty()) continue;

            // Full-line comment: * at start
            if (trimmed.front() == '*') continue;

            // Continuation line: + at start
            if (trimmed.front() == '+') {
                if (pending.empty()) {
                    ctx.errors.push_back("Line " + std::to_string(line_no) +
                                         ": Continuation '+' with no preceding line");
                    if (callbacks) {
                        callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    }
                    continue;
                }
                auto cont = trim_view(trimmed.substr(1));
                if (!cont.empty()) {
                    pending += " ";
                    pending += std::string(cont);
                }
                continue;
            }

            // Flush pending line
            if (!pending.empty()) {
                processed_lines.emplace_back(pending_line_no, std::move(pending));
                pending.clear();
            }

            pending = std::string(trimmed);
            pending_line_no = line_no;
        }

        if (!pending.empty()) {
            processed_lines.emplace_back(pending_line_no, std::move(pending));
        }
    }

    if (processed_lines.empty()) {
        ctx.errors.push_back("No statements found");
        if (callbacks) callbacks->on_error(ctx.errors.back(), std::nullopt);
        return false;
    }

    // -----------------------------------------------------------------------
    // Track nets (nodes) so we can emit them in LayoutIR
    // -----------------------------------------------------------------------
    struct NetBuilder {
        std::string name;
        std::vector<std::string> pin_names; // terminal names on devices
    };
    std::map<std::string, NetBuilder> net_map; // node name -> builder

    auto ensure_net = [&](const std::string& node_name) -> NetBuilder* {
        auto it = net_map.find(node_name);
        if (it == net_map.end()) {
            auto [new_it, _] = net_map.emplace(node_name, NetBuilder{node_name, {}});
            return &new_it->second;
        }
        return &it->second;
    };

    // Current subckt context
    std::string current_subckt;
    std::vector<std::string> subckt_nodes;
    bool in_subckt = false;

    std::size_t last_pct_reported = 0;
    const std::size_t total_statements = processed_lines.size();

    auto report_error = [&](const std::string& msg, std::size_t line_no) {
        ctx.errors.push_back("Line " + std::to_string(line_no) + ": " + msg);
    };

    auto report_progress = [&](std::size_t idx) {
        if (!callbacks || total_statements == 0) return;
        std::size_t pct = (idx * 100) / total_statements;
        if (pct > last_pct_reported) {
            last_pct_reported = pct;
            callbacks->on_progress({pct, 100,
                "Parsed " + std::to_string(idx) + " statements"});
        }
    };

    // -----------------------------------------------------------------------
    // Parse each logical line
    // -----------------------------------------------------------------------
    for (std::size_t idx = 0; idx < processed_lines.size(); ++idx) {
        auto [line_no, raw_line] = processed_lines[idx];

        // Cancellation check every few lines
        if (idx % 10 == 0 && token.is_cancelled()) {
            report_error("Parse cancelled by user", line_no);
            if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
            return false;
        }
        report_progress(idx);

        auto tokens = tokenise(raw_line);
        if (tokens.empty()) continue;

        const std::string& first = tokens[0];
        if (first.empty()) continue;

        // -------------------------------------------------------------------
        // .SUBCKT
        // -------------------------------------------------------------------
        if (first.size() >= 7 &&
            (first.substr(0, 7) == ".SUBCKT" || first.substr(0, 7) == ".subckt")) {
            if (tokens.size() < 2) {
                report_error(".SUBCKT requires a name", line_no);
                if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                return false;
            }
            in_subckt = true;
            current_subckt = tokens[1];
            subckt_nodes.clear();
            if (ctx.ir.design_name.empty()) {
                ctx.ir.design_name = current_subckt;
            }
            for (std::size_t i = 2; i < tokens.size(); ++i) {
                subckt_nodes.push_back(tokens[i]);
                auto* nb = ensure_net(tokens[i]);
                (void)nb; // nodes will get ports below
            }
            continue;
        }

        // -------------------------------------------------------------------
        // .ENDS
        // -------------------------------------------------------------------
        if (first.size() >= 5 &&
            (first.substr(0, 5) == ".ENDS" || first.substr(0, 5) == ".ends")) {
            if (!in_subckt) {
                report_error(".ENDS without matching .SUBCKT", line_no);
                if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                return false;
            }
            // Emit ports for the subckt nodes
            for (const auto& node : subckt_nodes) {
                Port port;
                port.name = node;
                port.direction = "INOUT";
                port.net_name = node;
                ctx.ir.ports.push_back(port);

                if (callbacks) {
                    ParsedPin pp;
                    pp.name = node;
                    pp.direction = "INOUT";
                    pp.net_name = node;
                    callbacks->on_pin(pp);
                }

                // Ensure the net exists and records this port as a pin
                auto* nb = ensure_net(node);
                nb->pin_names.push_back(node);
            }
            in_subckt = false;
            current_subckt.clear();
            subckt_nodes.clear();
            continue;
        }

        // -------------------------------------------------------------------
        // Element instances: R, C, M, V, I
        // -------------------------------------------------------------------
        char element_type = static_cast<char>(std::toupper(static_cast<unsigned char>(first.front())));
        bool is_element = (element_type == 'R' || element_type == 'C' ||
                           element_type == 'M' || element_type == 'V' ||
                           element_type == 'I');
        if (!is_element) {
            report_error("Unknown statement '" + first + "'", line_no);
            if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
            return false;
        }

        // Validate element name
        if (first.size() < 2) {
            report_error(std::string(1, element_type) + " element requires a name", line_no);
            if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
            return false;
        }
        std::string dev_name = first.substr(1);

        Device dev;
        dev.name = dev_name;

        switch (element_type) {
            case 'R': {
                if (tokens.size() < 4) {
                    report_error("R element requires node1 node2 value", line_no);
                    if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    return false;
                }
                dev.type = "RESISTOR";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                {
                    auto [val, prop_start] = extract_value_string(tokens, 3);
                    dev.properties["value"] = val;
                    if (prop_start < tokens.size()) {
                        auto props = parse_kv_pairs(line_no, tokens, prop_start);
                        dev.properties.insert(props.begin(), props.end());
                    }
                }

                ensure_net(tokens[1])->pin_names.push_back(dev_name + ":pos");
                ensure_net(tokens[2])->pin_names.push_back(dev_name + ":neg");
                break;
            }
            case 'C': {
                if (tokens.size() < 4) {
                    report_error("C element requires node1 node2 value", line_no);
                    if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    return false;
                }
                dev.type = "CAPACITOR";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                {
                    auto [val, prop_start] = extract_value_string(tokens, 3);
                    dev.properties["value"] = val;
                    if (prop_start < tokens.size()) {
                        auto props = parse_kv_pairs(line_no, tokens, prop_start);
                        dev.properties.insert(props.begin(), props.end());
                    }
                }

                ensure_net(tokens[1])->pin_names.push_back(dev_name + ":pos");
                ensure_net(tokens[2])->pin_names.push_back(dev_name + ":neg");
                break;
            }
            case 'M': {
                if (tokens.size() < 6) {
                    report_error("M element requires drain gate source bulk model", line_no);
                    if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    return false;
                }
                dev.type = "MOSFET";
                dev.pins["drain"]  = tokens[1];
                dev.pins["gate"]   = tokens[2];
                dev.pins["source"] = tokens[3];
                dev.pins["bulk"]   = tokens[4];
                dev.properties["model"] = tokens[5];
                if (tokens.size() > 6) {
                    auto props = parse_kv_pairs(line_no, tokens, 6);
                    dev.properties.insert(props.begin(), props.end());
                }

                ensure_net(tokens[1])->pin_names.push_back(dev_name + ":drain");
                ensure_net(tokens[2])->pin_names.push_back(dev_name + ":gate");
                ensure_net(tokens[3])->pin_names.push_back(dev_name + ":source");
                ensure_net(tokens[4])->pin_names.push_back(dev_name + ":bulk");
                break;
            }
            case 'V': {
                if (tokens.size() < 4) {
                    report_error("V element requires node+ node- value", line_no);
                    if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    return false;
                }
                dev.type = "VOLTAGE_SOURCE";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                {
                    auto [val, prop_start] = extract_value_string(tokens, 3);
                    dev.properties["value"] = val;
                    if (prop_start < tokens.size()) {
                        auto props = parse_kv_pairs(line_no, tokens, prop_start);
                        dev.properties.insert(props.begin(), props.end());
                    }
                }

                ensure_net(tokens[1])->pin_names.push_back(dev_name + ":pos");
                ensure_net(tokens[2])->pin_names.push_back(dev_name + ":neg");
                break;
            }
            case 'I': {
                if (tokens.size() < 4) {
                    report_error("I element requires node+ node- value", line_no);
                    if (callbacks) callbacks->on_error(ctx.errors.back(), static_cast<int>(line_no));
                    return false;
                }
                dev.type = "CURRENT_SOURCE";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                {
                    auto [val, prop_start] = extract_value_string(tokens, 3);
                    dev.properties["value"] = val;
                    if (prop_start < tokens.size()) {
                        auto props = parse_kv_pairs(line_no, tokens, prop_start);
                        dev.properties.insert(props.begin(), props.end());
                    }
                }

                ensure_net(tokens[1])->pin_names.push_back(dev_name + ":pos");
                ensure_net(tokens[2])->pin_names.push_back(dev_name + ":neg");
                break;
            }
        }

        ctx.ir.devices.push_back(dev);

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
    }

    // -----------------------------------------------------------------------
    // Emit nets
    // -----------------------------------------------------------------------
    for (auto& [name, nb] : net_map) {
        Net net;
        net.name = name;
        net.pin_names = std::move(nb.pin_names);
        ctx.ir.nets.push_back(std::move(net));

        if (callbacks) {
            ParsedNet pn;
            pn.name = name;
            pn.pin_names = ctx.ir.nets.back().pin_names;
            callbacks->on_net(pn);
        }
    }

    // Default design name if none set
    if (ctx.ir.design_name.empty() && !ctx.ir.devices.empty()) {
        ctx.ir.design_name = ctx.ir.devices.front().name + "_circuit";
    }

    if (in_subckt) {
        report_error("Missing .ENDS for subcircuit '" + current_subckt + "'", 0);
        if (callbacks) callbacks->on_error(ctx.errors.back(), std::nullopt);
        return false;
    }

    return ctx.errors.empty();
}

} // namespace aegis::parsing
