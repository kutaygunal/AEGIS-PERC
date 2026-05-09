#include "aegis/parsing/spice_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aegis::parsing {

namespace {

struct LogicalStatement {
    std::filesystem::path source_path;
    std::size_t line = 0;
    std::string text;
};

struct SubcktFrame {
    std::string name;
    std::vector<std::string> interface_nodes;
    std::size_t source_line = 0;
};

struct NetBuilder {
    std::string name;
    std::vector<std::string> pin_names;
    std::map<std::string, std::string> properties;
};

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

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool starts_with_ci(std::string_view value, std::string_view prefix)
{
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string strip_inline_comments(std::string_view line)
{
    std::size_t pos = std::string::npos;
    bool in_quote = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) {
            continue;
        }
        if (line[i] == ';' || line[i] == '$') {
            pos = i;
            break;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            pos = i;
            break;
        }
    }
    if (pos == std::string::npos) {
        return std::string(line);
    }
    return std::string(trim_view(line.substr(0, pos)));
}

std::string strip_token_punctuation(std::string token)
{
    while (!token.empty() && (token.front() == '(' || token.front() == ',')) {
        token.erase(token.begin());
    }
    while (!token.empty() && (token.back() == ')' || token.back() == ',')) {
        token.pop_back();
    }
    return token;
}

std::vector<std::string> tokenise(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_quote = false;

    auto flush = [&]() {
        if (current.empty()) {
            return;
        }
        auto token = strip_token_punctuation(current);
        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }
        current.clear();
    };

    for (char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            current.push_back(c);
            continue;
        }
        if (!in_quote && is_space(c)) {
            flush();
            continue;
        }
        current.push_back(c);
    }
    flush();
    return tokens;
}

std::string unquote(std::string value)
{
    value = trim_copy(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::map<std::string, std::string> parse_kv_pairs(std::size_t line_no,
                                                  const std::vector<std::string>& tokens,
                                                  std::size_t first = 0)
{
    std::map<std::string, std::string> result;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        auto token = strip_token_punctuation(tokens[i]);
        if (token.empty()) {
            continue;
        }
        const auto pos = token.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Line " + std::to_string(line_no) +
                                     ": Expected key=value pair, got '" + token + "'");
        }
        result[token.substr(0, pos)] = token.substr(pos + 1);
    }
    return result;
}

std::pair<std::string, std::size_t> extract_value_string(const std::vector<std::string>& tokens,
                                                         std::size_t value_start)
{
    std::string value;
    std::size_t i = value_start;
    for (; i < tokens.size(); ++i) {
        const auto token = strip_token_punctuation(tokens[i]);
        if (token.find('=') != std::string::npos) {
            break;
        }
        if (!value.empty()) {
            value.push_back(' ');
        }
        value += token;
    }
    return {value, i};
}

bool is_parameter_marker(std::string_view token)
{
    return starts_with_ci(token, "params:") || starts_with_ci(token, "param:");
}

std::vector<std::string> parse_subckt_interface_nodes(const std::vector<std::string>& tokens,
                                                      std::size_t first)
{
    std::vector<std::string> nodes;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        const auto token = strip_token_punctuation(tokens[i]);
        if (token.empty()) {
            continue;
        }
        if (is_parameter_marker(token) || token.find('=') != std::string::npos) {
            break;
        }
        nodes.push_back(token);
    }
    return nodes;
}

std::string make_message(const LogicalStatement& stmt, const std::string& message)
{
    return stmt.source_path.generic_string() + ": line " + std::to_string(stmt.line) + ": " + message;
}

std::filesystem::path resolve_include_path(const std::filesystem::path& base,
                                           const std::string& raw_path)
{
    std::filesystem::path resolved = unquote(raw_path);
    if (resolved.is_absolute()) {
        return resolved;
    }
    return base.parent_path() / resolved;
}

bool is_non_electrical_control_card(std::string_view directive)
{
    return starts_with_ci(directive, ".option") || starts_with_ci(directive, ".temp") ||
           starts_with_ci(directive, ".protect") || starts_with_ci(directive, ".unprotect") ||
           starts_with_ci(directive, ".end");
}

bool load_logical_statements(const std::filesystem::path& path,
                             std::vector<LogicalStatement>& out,
                             std::vector<std::string>& errors,
                             std::unordered_set<std::string>& include_stack,
                             int depth)
{
    if (depth > 16) {
        errors.push_back("Include nesting exceeds supported depth (16) at '" + path.generic_string() + "'");
        return false;
    }

    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    const auto key = ec ? path.lexically_normal().generic_string() : canonical.generic_string();
    if (include_stack.count(key) != 0) {
        errors.push_back("Recursive include detected for '" + key + "'");
        return false;
    }
    include_stack.insert(key);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        errors.push_back("Cannot open file: " + path.generic_string());
        include_stack.erase(key);
        return false;
    }

    std::string line;
    std::size_t line_no = 0;
    std::string pending;
    std::size_t pending_line_no = 0;

    auto flush_pending = [&]() -> bool {
        if (pending.empty()) {
            return true;
        }
        const auto tokens = tokenise(pending);
        if (!tokens.empty() && starts_with_ci(tokens.front(), ".include")) {
            if (tokens.size() < 2) {
                errors.push_back(path.generic_string() + ": line " + std::to_string(pending_line_no) + ": .INCLUDE requires a file path");
                return false;
            }
            if (!load_logical_statements(resolve_include_path(path, tokens[1]), out, errors, include_stack, depth + 1)) {
                return false;
            }
        } else if (!tokens.empty() && starts_with_ci(tokens.front(), ".inc")) {
            if (tokens.size() < 2) {
                errors.push_back(path.generic_string() + ": line " + std::to_string(pending_line_no) + ": .INC requires a file path");
                return false;
            }
            if (!load_logical_statements(resolve_include_path(path, tokens[1]), out, errors, include_stack, depth + 1)) {
                return false;
            }
        } else if (!tokens.empty() && starts_with_ci(tokens.front(), ".lib")) {
            if (tokens.size() < 2) {
                errors.push_back(path.generic_string() + ": line " + std::to_string(pending_line_no) + ": .LIB requires a file path");
                return false;
            }
            if (!load_logical_statements(resolve_include_path(path, tokens[1]), out, errors, include_stack, depth + 1)) {
                return false;
            }
        } else {
            out.push_back({path, pending_line_no, trim_copy(pending)});
        }
        pending.clear();
        pending_line_no = 0;
        return true;
    };

    while (std::getline(file, line)) {
        ++line_no;
        const std::string stripped = strip_inline_comments(line);
        const auto trimmed = trim_view(stripped);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == '*') {
            continue;
        }
        if (trimmed.front() == '+') {
            if (pending.empty()) {
                errors.push_back(path.generic_string() + ": line " + std::to_string(line_no) + ": Continuation '+' with no preceding line");
                include_stack.erase(key);
                return false;
            }
            const auto cont = trim_view(trimmed.substr(1));
            if (!cont.empty()) {
                pending.push_back(' ');
                pending.append(cont.begin(), cont.end());
            }
            continue;
        }
        if (!flush_pending()) {
            include_stack.erase(key);
            return false;
        }
        pending.assign(trimmed.begin(), trimmed.end());
        pending_line_no = line_no;
    }

    const bool ok = flush_pending();
    include_stack.erase(key);
    return ok;
}

} // namespace

std::string SpiceParser::format_name() const
{
    return "SPICE-like";
}

bool SpiceParser::parse(const std::filesystem::path& path,
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

LayoutIR SpiceParser::parse_to_layout_ir(const std::filesystem::path& path,
                                         const ICancellationToken& token)
{
    Context ctx;
    const bool ok = parse_file(path, ctx, nullptr, token);
    if (!ok) {
        throw LayoutIRError(ctx.errors.empty() ? "SPICE-like parse failed" : ctx.errors.front());
    }
    return std::move(ctx.ir);
}

bool SpiceParser::parse_file(const std::filesystem::path& path,
                             Context& ctx,
                             IParserCallbacks* callbacks,
                             const ICancellationToken& token)
{
    if (callbacks) {
        callbacks->on_progress({0, 100, "Opening file"});
    }

    std::vector<LogicalStatement> statements;
    std::unordered_set<std::string> include_stack;
    if (!load_logical_statements(path, statements, ctx.errors, include_stack, 0)) {
        return false;
    }
    if (statements.empty()) {
        ctx.errors.push_back("No statements found");
        return false;
    }

    std::map<std::string, NetBuilder> net_map;
    auto ensure_net = [&](const std::string& node_name) -> NetBuilder& {
        auto [it, inserted] = net_map.emplace(node_name, NetBuilder{node_name, {}, {}});
        if (inserted && (node_name == "0" || node_name == "gnd" || node_name == "GND")) {
            it->second.properties["kind"] = "ground";
        }
        return it->second;
    };

    std::unordered_map<std::string, std::vector<std::string>> subckt_interfaces;
    std::vector<SubcktFrame> subckt_stack;
    std::size_t last_pct_reported = 0;
    const std::size_t total_statements = statements.size();
    std::size_t annotation_count = 0;

    auto emit_error = [&](const LogicalStatement& stmt, const std::string& message) {
        ctx.errors.push_back(make_message(stmt, message));
        if (callbacks) {
            callbacks->on_error(ctx.errors.back(), static_cast<int>(stmt.line));
        }
    };

    auto emit_annotation = [&](std::string key, std::string value) {
        ctx.ir.annotations.push_back({std::move(key), std::move(value), std::nullopt, std::nullopt});
        ++annotation_count;
    };

    for (std::size_t idx = 0; idx < statements.size(); ++idx) {
        if (idx % 16 == 0 && token.is_cancelled()) {
            ctx.errors.push_back("Parse cancelled by user");
            return false;
        }

        const auto& stmt = statements[idx];
        if (callbacks && total_statements > 0) {
            const std::size_t pct = ((idx + 1) * 100) / total_statements;
            if (pct > last_pct_reported) {
                last_pct_reported = pct;
                callbacks->on_progress({pct, 100, "Parsed " + std::to_string(idx + 1) + " statements"});
            }
        }

        const auto tokens = tokenise(stmt.text);
        if (tokens.empty()) {
            continue;
        }

        const std::string first_upper = to_upper(tokens.front());
        if (starts_with_ci(first_upper, ".SUBCKT")) {
            if (tokens.size() < 2) {
                emit_error(stmt, ".SUBCKT requires a name");
                return false;
            }
            SubcktFrame frame;
            frame.name = tokens[1];
            frame.source_line = stmt.line;
            frame.interface_nodes = parse_subckt_interface_nodes(tokens, 2);
            subckt_interfaces[frame.name] = frame.interface_nodes;
            subckt_stack.push_back(frame);
            if (ctx.ir.design_name.empty()) {
                ctx.ir.design_name = frame.name;
            }
            continue;
        }

        if (starts_with_ci(first_upper, ".ENDS")) {
            if (subckt_stack.empty()) {
                emit_error(stmt, ".ENDS without matching .SUBCKT");
                return false;
            }
            const SubcktFrame frame = subckt_stack.back();
            subckt_stack.pop_back();
            if (tokens.size() >= 2 && tokens[1] != frame.name) {
                emit_error(stmt, ".ENDS name '" + tokens[1] + "' does not match open subcircuit '" + frame.name + "'");
                return false;
            }
            for (const auto& node : frame.interface_nodes) {
                Port port;
                port.name = node;
                port.direction = "INOUT";
                port.net_name = node;
                ctx.ir.ports.push_back(port);
                ensure_net(node).pin_names.push_back(node);
                if (callbacks) {
                    callbacks->on_pin({node, node, "INOUT"});
                }
            }
            continue;
        }

        if (starts_with_ci(first_upper, ".MODEL")) {
            if (tokens.size() < 3) {
                emit_error(stmt, ".MODEL requires a model name and type");
                return false;
            }
            auto props = parse_kv_pairs(stmt.line, tokens, 3);
            ctx.ir.metadata["model." + tokens[1] + ".type"] = tokens[2];
            for (const auto& [key, value] : props) {
                ctx.ir.metadata["model." + tokens[1] + "." + key] = value;
            }
            emit_annotation("model", tokens[1] + " " + tokens[2]);
            continue;
        }

        if (starts_with_ci(first_upper, ".PARAM")) {
            if (tokens.size() < 2) {
                emit_error(stmt, ".PARAM requires at least one key=value pair");
                return false;
            }
            auto params = parse_kv_pairs(stmt.line, tokens, 1);
            for (const auto& [key, value] : params) {
                ctx.ir.metadata["param." + key] = value;
            }
            emit_annotation("param", std::to_string(params.size()));
            continue;
        }

        if (starts_with_ci(first_upper, ".GLOBAL")) {
            if (tokens.size() < 2) {
                emit_error(stmt, ".GLOBAL requires at least one net name");
                return false;
            }
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                auto& net = ensure_net(tokens[i]);
                net.properties["global"] = "true";
            }
            continue;
        }

        if (is_non_electrical_control_card(first_upper)) {
            emit_annotation("control_card", stmt.text);
            continue;
        }

        if (!tokens.front().empty() && tokens.front().front() == '.') {
            emit_error(stmt, "Unsupported SPICE/CDL directive '" + tokens.front() + "'");
            return false;
        }

        const char element_type = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens.front().front())));
        if (tokens.front().size() < 2) {
            emit_error(stmt, std::string(1, element_type) + " element requires a name");
            return false;
        }
        const std::string dev_name = tokens.front().substr(1);

        Device dev;
        dev.name = dev_name;
        dev.properties["source_file"] = stmt.source_path.filename().generic_string();
        dev.properties["source_line"] = std::to_string(stmt.line);
        if (!subckt_stack.empty()) {
            dev.properties["subckt_scope"] = subckt_stack.back().name;
        }

        try {
            switch (element_type) {
            case 'R': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "R element requires node1 node2 value");
                    return false;
                }
                dev.type = "RESISTOR";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                const auto [value, first_prop] = extract_value_string(tokens, 3);
                dev.properties["value"] = value;
                auto props = parse_kv_pairs(stmt.line, tokens, first_prop);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'C': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "C element requires node1 node2 value");
                    return false;
                }
                dev.type = "CAPACITOR";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                const auto [value, first_prop] = extract_value_string(tokens, 3);
                dev.properties["value"] = value;
                auto props = parse_kv_pairs(stmt.line, tokens, first_prop);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'L': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "L element requires node1 node2 value");
                    return false;
                }
                dev.type = "INDUCTOR";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                const auto [value, first_prop] = extract_value_string(tokens, 3);
                dev.properties["value"] = value;
                auto props = parse_kv_pairs(stmt.line, tokens, first_prop);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'M': {
                if (tokens.size() < 6) {
                    emit_error(stmt, "M element requires drain gate source bulk model");
                    return false;
                }
                dev.type = "MOSFET";
                dev.pins["drain"] = tokens[1];
                dev.pins["gate"] = tokens[2];
                dev.pins["source"] = tokens[3];
                dev.pins["bulk"] = tokens[4];
                dev.properties["model"] = tokens[5];
                auto props = parse_kv_pairs(stmt.line, tokens, 6);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'D': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "D element requires anode cathode model");
                    return false;
                }
                dev.type = "DIODE";
                dev.pins["anode"] = tokens[1];
                dev.pins["cathode"] = tokens[2];
                dev.properties["model"] = tokens[3];
                auto props = parse_kv_pairs(stmt.line, tokens, 4);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'Q': {
                if (tokens.size() < 5) {
                    emit_error(stmt, "Q element requires collector base emitter model or collector base emitter substrate model");
                    return false;
                }
                dev.type = "BJT";
                dev.pins["collector"] = tokens[1];
                dev.pins["base"] = tokens[2];
                dev.pins["emitter"] = tokens[3];
                std::size_t model_index = 4;
                if (tokens.size() >= 6 && tokens[5].find('=') == std::string::npos) {
                    dev.pins["substrate"] = tokens[4];
                    model_index = 5;
                }
                dev.properties["model"] = tokens[model_index];
                auto props = parse_kv_pairs(stmt.line, tokens, model_index + 1);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'V': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "V element requires node+ node- value");
                    return false;
                }
                dev.type = "VOLTAGE_SOURCE";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                const auto [value, first_prop] = extract_value_string(tokens, 3);
                dev.properties["value"] = value;
                auto props = parse_kv_pairs(stmt.line, tokens, first_prop);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'I': {
                if (tokens.size() < 4) {
                    emit_error(stmt, "I element requires node+ node- value");
                    return false;
                }
                dev.type = "CURRENT_SOURCE";
                dev.pins["pos"] = tokens[1];
                dev.pins["neg"] = tokens[2];
                const auto [value, first_prop] = extract_value_string(tokens, 3);
                dev.properties["value"] = value;
                auto props = parse_kv_pairs(stmt.line, tokens, first_prop);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            case 'X': {
                if (tokens.size() < 3) {
                    emit_error(stmt, "X element requires at least one connection and a subcircuit name");
                    return false;
                }
                std::size_t subckt_name_index = tokens.size();
                for (std::size_t i = tokens.size(); i-- > 1;) {
                    const auto lower = to_lower(tokens[i]);
                    if (tokens[i].find('=') == std::string::npos && !is_parameter_marker(lower)) {
                        subckt_name_index = i;
                        break;
                    }
                }
                if (subckt_name_index == tokens.size() || subckt_name_index <= 1) {
                    emit_error(stmt, "X element requires explicit subcircuit target after connection nodes");
                    return false;
                }
                dev.type = "SUBCKT_INSTANCE";
                dev.properties["subckt"] = tokens[subckt_name_index];
                const auto formals_it = subckt_interfaces.find(tokens[subckt_name_index]);
                const std::size_t terminal_count = subckt_name_index - 1;
                for (std::size_t i = 0; i < terminal_count; ++i) {
                    std::string pin_name = "p" + std::to_string(i);
                    if (formals_it != subckt_interfaces.end() && i < formals_it->second.size()) {
                        pin_name = formals_it->second[i];
                    }
                    dev.pins[pin_name] = tokens[i + 1];
                }
                auto props = parse_kv_pairs(stmt.line, tokens, subckt_name_index + 1);
                dev.properties.insert(props.begin(), props.end());
                break;
            }
            default:
                emit_error(stmt, std::string("Unsupported SPICE/CDL element '") + tokens.front() + "'");
                return false;
            }
        } catch (const std::exception& ex) {
            ctx.errors.push_back(ex.what());
            return false;
        }

        for (const auto& [pin_name, net_name] : dev.pins) {
            ensure_net(net_name).pin_names.push_back(dev.name + ":" + pin_name);
        }
        ctx.ir.devices.push_back(dev);

        if (callbacks) {
            ParsedCell cell;
            cell.name = dev.name;
            cell.properties = dev.properties;
            cell.properties["type"] = dev.type;
            for (const auto& [pin_name, net_name] : dev.pins) {
                cell.properties[pin_name] = net_name;
            }
            callbacks->on_cell(cell);
        }
    }

    if (!subckt_stack.empty()) {
        ctx.errors.push_back("Missing .ENDS for subcircuit '" + subckt_stack.back().name + "'");
        return false;
    }

    for (auto& [name, net] : net_map) {
        auto end = std::unique(net.pin_names.begin(), net.pin_names.end());
        net.pin_names.erase(end, net.pin_names.end());
        Net ir_net;
        ir_net.name = name;
        ir_net.pin_names = std::move(net.pin_names);
        ir_net.properties = std::move(net.properties);
        ctx.ir.nets.push_back(std::move(ir_net));
        if (callbacks) {
            ParsedNet parsed;
            parsed.name = ctx.ir.nets.back().name;
            parsed.pin_names = ctx.ir.nets.back().pin_names;
            parsed.properties = ctx.ir.nets.back().properties;
            callbacks->on_net(parsed);
        }
    }

    if (ctx.ir.design_name.empty()) {
        if (!ctx.ir.devices.empty()) {
            ctx.ir.design_name = ctx.ir.devices.front().name + "_circuit";
        } else {
            ctx.ir.design_name = path.stem().generic_string();
        }
    }
    ctx.ir.metadata["source_format"] = "spice";
    ctx.ir.metadata["annotation_count"] = std::to_string(annotation_count);
    return ctx.errors.empty();
}

} // namespace aegis::parsing
