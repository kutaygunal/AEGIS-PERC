#include "aegis/parsing/verilog_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aegis::parsing {
namespace {

struct LogicalStatement {
    std::size_t line = 0;
    std::string text;
};

struct PortDecl {
    std::string name;
    std::string direction;
    std::optional<std::string> range;
};

struct NetDecl {
    std::string name;
    std::string kind;
    std::optional<std::string> range;
};

struct InstanceConnection {
    std::string pin_name;
    std::string signal;
};

struct InstanceDecl {
    std::string type;
    std::string name;
    std::vector<InstanceConnection> connections;
    std::size_t source_line = 0;
};

struct AssignDecl {
    std::string lhs;
    std::string rhs;
    std::vector<std::string> rhs_signals;
    std::size_t source_line = 0;
};

struct ModuleDecl {
    std::string name;
    std::vector<std::string> header_port_order;
    std::map<std::string, PortDecl> ports_by_name;
    std::vector<NetDecl> declarations;
    std::vector<InstanceDecl> instances;
    std::vector<AssignDecl> assigns;
    std::size_t source_line = 0;
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

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool starts_with_keyword(std::string_view text, std::string_view keyword)
{
    const auto trimmed = trim_view(text);
    if (trimmed.size() < keyword.size() || trimmed.substr(0, keyword.size()) != keyword) {
        return false;
    }
    if (trimmed.size() == keyword.size()) {
        return true;
    }
    const char next = trimmed[keyword.size()];
    return is_space(next) || next == '(' || next == ';';
}

bool is_identifier_start(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

bool is_identifier_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

bool is_numeric_literal(std::string_view token)
{
    if (token.empty()) {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
        return true;
    }
    return token.front() == '\'';
}

std::string strip_comments_preserve_newlines(const std::string& content)
{
    std::string out;
    out.reserve(content.size());

    bool in_string = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        const char next = (i + 1 < content.size()) ? content[i + 1] : '\0';

        if (in_line_comment) {
            if (c == '\n') {
                in_line_comment = false;
                out.push_back(c);
            }
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && next == '/') {
                in_block_comment = false;
                ++i;
                continue;
            }
            if (c == '\n') {
                out.push_back(c);
            }
            continue;
        }
        if (!in_string && c == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (!in_string && c == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
        }
        out.push_back(c);
    }
    return out;
}

std::size_t count_keyword_occurrences(std::string_view text, std::string_view keyword)
{
    std::size_t count = 0;
    for (std::size_t pos = 0; pos + keyword.size() <= text.size(); ++pos) {
        if (text.substr(pos, keyword.size()) != keyword) {
            continue;
        }
        const bool left_ok = pos == 0 || !is_identifier_char(text[pos - 1]);
        const bool right_ok = pos + keyword.size() == text.size() || !is_identifier_char(text[pos + keyword.size()]);
        if (left_ok && right_ok) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> split_top_level(std::string_view text, char delimiter)
{
    std::vector<std::string> parts;
    std::string current;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    for (char c : text) {
        if (c == '(') {
            ++paren_depth;
        } else if (c == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']' && bracket_depth > 0) {
            --bracket_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}' && brace_depth > 0) {
            --brace_depth;
        }

        if (c == delimiter && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
            const auto trimmed = trim_view(current);
            if (!trimmed.empty()) {
                parts.emplace_back(trimmed.begin(), trimmed.end());
            }
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    const auto trimmed = trim_view(current);
    if (!trimmed.empty()) {
        parts.emplace_back(trimmed.begin(), trimmed.end());
    }
    return parts;
}

std::vector<std::string> tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (char c : text) {
        if (is_space(c)) {
            flush();
            continue;
        }
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == ',' || c == ':' || c == '=' || c == '.' || c == '#' || c == '{' || c == '}') {
            flush();
            tokens.emplace_back(1, c);
            continue;
        }
        current.push_back(c);
    }
    flush();
    return tokens;
}

std::optional<std::string> extract_leading_range(std::string_view& text)
{
    text = trim_view(text);
    if (text.empty() || text.front() != '[') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '[') {
            ++depth;
        } else if (text[i] == ']') {
            --depth;
            if (depth == 0) {
                const std::string range(text.substr(0, i + 1));
                text = trim_view(text.substr(i + 1));
                return range;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> parse_identifier_list(std::string_view text)
{
    std::vector<std::string> names;
    for (std::string part : split_top_level(text, ',')) {
        auto view = trim_view(part);
        (void)extract_leading_range(view);
        if (view.empty()) {
            continue;
        }
        auto eq = view.find('=');
        if (eq != std::string_view::npos) {
            view = trim_view(view.substr(0, eq));
        }
        if (!view.empty()) {
            names.emplace_back(view.begin(), view.end());
        }
    }
    return names;
}

std::vector<PortDecl> parse_ansi_ports(std::string_view text)
{
    std::vector<PortDecl> ports;
    for (std::string part : split_top_level(text, ',')) {
        auto view = trim_view(part);
        if (view.empty()) {
            continue;
        }

        std::string direction;
        if (starts_with_keyword(view, "input")) {
            direction = "INPUT";
            view = trim_view(view.substr(5));
        } else if (starts_with_keyword(view, "output")) {
            direction = "OUTPUT";
            view = trim_view(view.substr(6));
        } else if (starts_with_keyword(view, "inout")) {
            direction = "INOUT";
            view = trim_view(view.substr(5));
        } else {
            ports.push_back({std::string(view), "INOUT", std::nullopt});
            continue;
        }

        for (const std::string_view keyword : {"wire", "reg", "logic", "tri", "signed"}) {
            if (starts_with_keyword(view, keyword)) {
                view = trim_view(view.substr(keyword.size()));
            }
        }

        const auto range = extract_leading_range(view);
        const auto names = parse_identifier_list(view);
        for (const auto& name : names) {
            ports.push_back({name, direction, range});
        }
    }
    return ports;
}

std::optional<std::pair<std::string, std::string>> split_assignment(std::string_view text)
{
    int paren_depth = 0;
    int bracket_depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') {
            ++paren_depth;
        } else if (c == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']' && bracket_depth > 0) {
            --bracket_depth;
        } else if (c == '=' && paren_depth == 0 && bracket_depth == 0) {
            const auto lhs = trim_view(text.substr(0, i));
            const auto rhs = trim_view(text.substr(i + 1));
            if (lhs.empty() || rhs.empty()) {
                return std::nullopt;
            }
            return std::make_pair(std::string(lhs), std::string(rhs));
        }
    }
    return std::nullopt;
}

std::vector<std::string> extract_signal_names(std::string_view expr)
{
    static const std::unordered_set<std::string> keywords = {
        "and", "or", "xor", "not", "buf", "if", "else", "begin", "end"
    };

    std::set<std::string> ordered;
    for (std::size_t i = 0; i < expr.size();) {
        if (!is_identifier_start(expr[i])) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        ++i;
        while (i < expr.size() && is_identifier_char(expr[i])) {
            ++i;
        }
        std::string name(expr.substr(begin, i - begin));
        while (i < expr.size() && expr[i] == '[') {
            const std::size_t range_begin = i;
            int depth = 0;
            do {
                if (expr[i] == '[') ++depth;
                if (expr[i] == ']') --depth;
                ++i;
            } while (i < expr.size() && depth > 0);
            name.append(expr.substr(range_begin, i - range_begin));
        }
        const auto lower = to_lower(name);
        if (!keywords.count(lower) && !is_numeric_literal(name)) {
            ordered.insert(name);
        }
    }
    return {ordered.begin(), ordered.end()};
}

bool parse_instance_body(std::string_view body,
                         std::vector<InstanceConnection>& out_connections,
                         std::string& error)
{
    const auto entries = split_top_level(body, ',');
    if (entries.empty()) {
        return true;
    }

    bool named = false;
    for (const auto& entry : entries) {
        const auto view = trim_view(entry);
        if (!view.empty() && view.front() == '.') {
            named = true;
            break;
        }
    }

    if (named) {
        for (const auto& entry : entries) {
            auto view = trim_view(entry);
            if (view.empty() || view.front() != '.') {
                error = "Mixed or malformed named instance connectivity";
                return false;
            }
            view.remove_prefix(1);
            const auto open = view.find('(');
            const auto close = view.rfind(')');
            if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
                error = "Malformed named connection";
                return false;
            }
            const std::string pin(trim_view(view.substr(0, open)));
            const std::string signal(trim_copy(std::string(trim_view(view.substr(open + 1, close - open - 1)))));
            if (!signal.empty()) {
                out_connections.push_back({pin, signal});
            }
        }
        return true;
    }

    std::size_t index = 0;
    for (const auto& entry : entries) {
        const std::string signal = trim_copy(entry);
        if (signal.empty()) {
            continue;
        }
        out_connections.push_back({"p" + std::to_string(index++), signal});
    }
    return true;
}

std::optional<std::pair<std::size_t, std::size_t>> find_balanced_parens(std::string_view text,
                                                                         std::size_t open_pos)
{
    if (open_pos >= text.size() || text[open_pos] != '(') {
        return std::nullopt;
    }
    int depth = 0;
    for (std::size_t i = open_pos; i < text.size(); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if (text[i] == ')') {
            --depth;
            if (depth == 0) {
                return std::make_pair(open_pos, i);
            }
        }
    }
    return std::nullopt;
}

bool parse_instance_statement(std::string_view body,
                              std::size_t line_no,
                              InstanceDecl& out_instance,
                              std::string& error)
{
    auto view = trim_view(body);
    if (view.empty()) {
        error = "Empty instance statement";
        return false;
    }

    std::size_t pos = 0;
    while (pos < view.size() && !is_space(view[pos])) {
        ++pos;
    }
    out_instance.type = std::string(view.substr(0, pos));
    view = trim_view(view.substr(pos));
    if (out_instance.type.empty() || view.empty()) {
        error = "Instance is missing type or name";
        return false;
    }

    if (!view.empty() && view.front() == '#') {
        const auto paren = view.find('(');
        if (paren == std::string_view::npos) {
            error = "Malformed parameter override in instance";
            return false;
        }
        const auto balanced = find_balanced_parens(view, paren);
        if (!balanced.has_value()) {
            error = "Unbalanced parameter override in instance";
            return false;
        }
        view = trim_view(view.substr(balanced->second + 1));
    }

    pos = 0;
    while (pos < view.size() && !is_space(view[pos]) && view[pos] != '(') {
        ++pos;
    }
    out_instance.name = std::string(view.substr(0, pos));
    view = trim_view(view.substr(pos));
    if (out_instance.name.empty() || view.empty() || view.front() != '(') {
        error = "Instance is missing connectivity list";
        return false;
    }

    const auto balanced = find_balanced_parens(view, 0);
    if (!balanced.has_value()) {
        error = "Unbalanced instance connectivity list";
        return false;
    }

    out_instance.source_line = line_no;
    const auto body_text = view.substr(1, balanced->second - 1);
    return parse_instance_body(body_text, out_instance.connections, error);
}

std::vector<LogicalStatement> read_logical_statements(const std::filesystem::path& path,
                                                      std::size_t& out_line_count)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    content = strip_comments_preserve_newlines(content);

    std::vector<LogicalStatement> statements;
    std::istringstream lines(content);
    std::string raw_line;
    std::string current;
    std::size_t line_no = 0;
    std::size_t start_line = 0;
    int procedural_depth = 0;

    auto flush_current = [&]() {
        const auto trimmed = trim_view(current);
        if (!trimmed.empty()) {
            statements.push_back({start_line, std::string(trimmed)});
        }
        current.clear();
        start_line = 0;
    };

    while (std::getline(lines, raw_line)) {
        ++line_no;
        const std::string trimmed = trim_copy(raw_line);
        if (trimmed.empty()) {
            continue;
        }

        const std::string lower = to_lower(trimmed);
        if (!trimmed.empty() && trimmed.front() == '`') {
            continue;
        }

        const bool starts_procedural = starts_with_keyword(lower, "always") || starts_with_keyword(lower, "initial");
        if (procedural_depth > 0 || starts_procedural) {
            const auto begin_count = static_cast<int>(count_keyword_occurrences(lower, "begin"));
            const auto end_count = static_cast<int>(count_keyword_occurrences(lower, "end"));
            if (starts_procedural && procedural_depth == 0 && begin_count == 0) {
                procedural_depth = 1;
            }
            procedural_depth += begin_count;
            procedural_depth -= end_count;
            if (procedural_depth < 0) {
                procedural_depth = 0;
            }
            continue;
        }

        if (current.empty()) {
            start_line = line_no;
        } else {
            current.push_back(' ');
        }
        current += trimmed;

        const bool statement_ends = trimmed.find(';') != std::string::npos || trim_view(trimmed) == "endmodule";
        if (statement_ends) {
            flush_current();
        }
    }

    flush_current();
    out_line_count = line_no;
    return statements;
}

std::string normalize_direction(std::string direction)
{
    direction = to_upper(std::move(direction));
    if (direction == "INPUT" || direction == "OUTPUT" || direction == "INOUT") {
        return direction;
    }
    return "INOUT";
}

LayoutIR build_layout_ir(const std::vector<ModuleDecl>& modules,
                         std::vector<std::string>& errors)
{
    if (modules.empty()) {
        errors.push_back("No Verilog modules found");
        return {};
    }

    std::unordered_set<std::string> instantiated_modules;
    for (const auto& module : modules) {
        for (const auto& instance : module.instances) {
            instantiated_modules.insert(instance.type);
        }
    }

    const ModuleDecl* root = nullptr;
    for (const auto& module : modules) {
        if (!instantiated_modules.count(module.name)) {
            root = &module;
        }
    }
    if (root == nullptr) {
        root = &modules.back();
    }

    LayoutIR ir;
    ir.design_name = root->name;
    ir.metadata["source_format"] = "verilog";
    ir.metadata["module_count"] = std::to_string(modules.size());
    ir.metadata["top_module"] = root->name;

    std::map<std::string, parsing::Net> nets_by_name;
    auto ensure_net = [&](const std::string& name) -> parsing::Net& {
        auto [it, inserted] = nets_by_name.emplace(name, parsing::Net{name, {}, {}});
        if (inserted) {
            it->second.properties["declared_in"] = root->name;
        }
        return it->second;
    };

    for (const auto& [name, port] : root->ports_by_name) {
        parsing::Port ir_port;
        ir_port.name = name;
        ir_port.net_name = name;
        ir_port.direction = normalize_direction(port.direction);
        ir.ports.push_back(ir_port);
        auto& net = ensure_net(name);
        net.pin_names.push_back(name);
        if (port.range.has_value()) {
            net.properties["range"] = *port.range;
        }
    }

    for (const auto& decl : root->declarations) {
        auto& net = ensure_net(decl.name);
        net.properties["kind"] = decl.kind;
        if (decl.range.has_value()) {
            net.properties["range"] = *decl.range;
        }
    }

    for (const auto& instance : root->instances) {
        parsing::Device device;
        device.name = instance.name;
        device.type = instance.type;
        device.properties["source_line"] = std::to_string(instance.source_line);
        for (const auto& connection : instance.connections) {
            if (connection.signal.empty()) {
                continue;
            }
            device.pins[connection.pin_name] = connection.signal;
            auto& net = ensure_net(connection.signal);
            net.pin_names.push_back(instance.name + "." + connection.pin_name);
        }
        ir.devices.push_back(std::move(device));
    }

    for (const auto& assign : root->assigns) {
        parsing::Device device;
        device.name = "$assign_" + std::to_string(assign.source_line);
        device.type = "ASSIGN";
        device.pins["lhs"] = assign.lhs;
        device.properties["expression"] = assign.rhs;
        device.properties["source_line"] = std::to_string(assign.source_line);
        ensure_net(assign.lhs).pin_names.push_back(device.name + ".lhs");
        for (std::size_t i = 0; i < assign.rhs_signals.size(); ++i) {
            const std::string pin_name = assign.rhs_signals.size() == 1 ? "rhs" : "rhs" + std::to_string(i);
            device.pins[pin_name] = assign.rhs_signals[i];
            ensure_net(assign.rhs_signals[i]).pin_names.push_back(device.name + "." + pin_name);
        }
        ir.devices.push_back(std::move(device));
    }

    for (auto& [name, net] : nets_by_name) {
        auto end = std::unique(net.pin_names.begin(), net.pin_names.end());
        net.pin_names.erase(end, net.pin_names.end());
        ir.nets.push_back(std::move(net));
    }

    return ir;
}

} // namespace

std::string VerilogParser::format_name() const
{
    return "Verilog";
}

bool VerilogParser::parse(const std::filesystem::path& path,
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

LayoutIR VerilogParser::parse_to_layout_ir(const std::filesystem::path& path,
                                           const ICancellationToken& token)
{
    Context ctx;
    const bool ok = parse_file(path, ctx, nullptr, token);
    if (!ok) {
        throw LayoutIRError(ctx.errors.empty() ? "Verilog parse failed" : ctx.errors.front());
    }
    return std::move(ctx.ir);
}

bool VerilogParser::parse_file(const std::filesystem::path& path,
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

    if (statements.empty()) {
        ctx.errors.push_back("No Verilog statements found");
        if (callbacks) {
            callbacks->on_error(ctx.errors.back(), std::nullopt);
        }
        return false;
    }

    if (callbacks) {
        callbacks->on_progress({0, 100, "Opening Verilog netlist"});
    }

    std::vector<ModuleDecl> modules;
    std::optional<ModuleDecl> current_module;
    std::size_t last_pct_reported = 0;

    auto report_error = [&](std::size_t line, const std::string& message) {
        const std::string full = "Line " + std::to_string(line) + ": " + message;
        ctx.errors.push_back(full);
        if (callbacks) {
            callbacks->on_error(full, static_cast<int>(line));
        }
    };

    auto add_declared_nets = [](ModuleDecl& module,
                                const std::vector<std::string>& names,
                                const std::string& kind,
                                const std::optional<std::string>& range) {
        for (const auto& name : names) {
            if (name.empty()) {
                continue;
            }
            module.declarations.push_back({name, kind, range});
        }
    };

    for (std::size_t i = 0; i < statements.size(); ++i) {
        if (i % 32 == 0 && token.is_cancelled()) {
            ctx.errors.push_back("Verilog parse cancelled by user");
            return false;
        }

        if (starts_with_keyword(to_lower(statements[i].text), "module") &&
            to_lower(statements[i].text).find("endmodule") != std::string::npos) {
            const std::string lower_stmt = to_lower(statements[i].text);
            const auto endmodule_pos = lower_stmt.rfind("endmodule");
            const auto line = statements[i].line;
            statements.insert(statements.begin() + static_cast<std::ptrdiff_t>(i + 1),
                              LogicalStatement{line, "endmodule"});
            statements[i].text = trim_copy(statements[i].text.substr(0, endmodule_pos));
        }
        const auto stmt = statements[i];
        const std::string lower = to_lower(stmt.text);
        if (callbacks && total_lines > 0) {
            const std::size_t pct = (stmt.line * 100) / total_lines;
            if (pct > last_pct_reported) {
                last_pct_reported = pct;
                callbacks->on_progress({pct, 100, "Parsed " + std::to_string(stmt.line) + " Verilog lines"});
            }
        }

        if (starts_with_keyword(lower, "module")) {
            if (current_module.has_value()) {
                report_error(stmt.line, "Nested module declaration is not supported");
                return false;
            }

            auto body = trim_view(std::string_view(stmt.text).substr(6));
            if (!body.empty() && body.back() == ';') {
                body.remove_suffix(1);
            }
            const auto open = body.find('(');
            if (open == std::string_view::npos) {
                report_error(stmt.line, "MODULE requires a port list");
                return false;
            }
            const auto balanced = find_balanced_parens(body, open);
            if (!balanced.has_value()) {
                report_error(stmt.line, "MODULE port list is unbalanced");
                return false;
            }

            ModuleDecl module;
            module.name = trim_copy(std::string(body.substr(0, open)));
            module.source_line = stmt.line;
            if (module.name.empty()) {
                report_error(stmt.line, "MODULE requires a name");
                return false;
            }

            const auto ports = parse_ansi_ports(body.substr(open + 1, balanced->second - open - 1));
            for (const auto& port : ports) {
                module.header_port_order.push_back(port.name);
                module.ports_by_name[port.name] = port;
                module.declarations.push_back({port.name, "port", port.range});
            }
            current_module = std::move(module);
            continue;
        }

        if (trim_view(stmt.text) == "endmodule") {
            if (!current_module.has_value()) {
                report_error(stmt.line, "ENDMODULE without MODULE");
                return false;
            }
            modules.push_back(std::move(*current_module));
            current_module.reset();
            continue;
        }

        if (!current_module.has_value()) {
            continue;
        }

        if (starts_with_keyword(lower, "input") || starts_with_keyword(lower, "output") || starts_with_keyword(lower, "inout")) {
            auto view = trim_view(stmt.text);
            const std::string direction = starts_with_keyword(lower, "input") ? "INPUT"
                : starts_with_keyword(lower, "output") ? "OUTPUT"
                : "INOUT";
            view.remove_prefix(direction == "INPUT" ? 5 : direction == "OUTPUT" ? 6 : 5);
            view = trim_view(view);
            if (!view.empty() && view.back() == ';') {
                view.remove_suffix(1);
            }
            for (const std::string_view keyword : {"wire", "reg", "logic", "tri", "signed"}) {
                if (starts_with_keyword(view, keyword)) {
                    view = trim_view(view.substr(keyword.size()));
                }
            }
            const auto range = extract_leading_range(view);
            const auto names = parse_identifier_list(view);
            for (const auto& name : names) {
                current_module->ports_by_name[name] = PortDecl{name, direction, range};
                current_module->declarations.push_back({name, "port", range});
            }
            continue;
        }

        if (starts_with_keyword(lower, "wire") || starts_with_keyword(lower, "tri") || starts_with_keyword(lower, "logic") || starts_with_keyword(lower, "reg")) {
            auto view = trim_view(stmt.text);
            std::string kind;
            for (const std::string_view keyword : {"wire", "tri", "logic", "reg"}) {
                if (starts_with_keyword(view, keyword)) {
                    kind = std::string(keyword);
                    view = trim_view(view.substr(keyword.size()));
                    break;
                }
            }
            if (!view.empty() && view.back() == ';') {
                view.remove_suffix(1);
            }
            const auto range = extract_leading_range(view);
            add_declared_nets(*current_module, parse_identifier_list(view), kind, range);
            continue;
        }

        if (starts_with_keyword(lower, "assign")) {
            auto view = trim_view(std::string_view(stmt.text).substr(6));
            if (!view.empty() && view.back() == ';') {
                view.remove_suffix(1);
            }
            const auto assignment = split_assignment(view);
            if (!assignment.has_value()) {
                report_error(stmt.line, "Malformed ASSIGN statement");
                return false;
            }
            AssignDecl assign;
            assign.lhs = assignment->first;
            assign.rhs = assignment->second;
            assign.rhs_signals = extract_signal_names(assign.rhs);
            assign.source_line = stmt.line;
            current_module->assigns.push_back(std::move(assign));
            continue;
        }

        if (starts_with_keyword(lower, "parameter") || starts_with_keyword(lower, "localparam") ||
            starts_with_keyword(lower, "supply0") || starts_with_keyword(lower, "supply1") ||
            starts_with_keyword(lower, "genvar")) {
            continue;
        }

        InstanceDecl instance;
        std::string parse_error;
        std::string instance_text = stmt.text;
        if (!instance_text.empty() && instance_text.back() == ';') {
            instance_text.pop_back();
        }
        if (!parse_instance_statement(instance_text, stmt.line, instance, parse_error)) {
            report_error(stmt.line, parse_error);
            return false;
        }
        current_module->instances.push_back(std::move(instance));
    }

    if (current_module.has_value()) {
        report_error(current_module->source_line, "MODULE is missing matching ENDMODULE");
        return false;
    }

    ctx.ir = build_layout_ir(modules, ctx.errors);
    if (!ctx.errors.empty()) {
        return false;
    }

    if (callbacks) {
        for (const auto& device : ctx.ir.devices) {
            ParsedCell cell;
            cell.name = device.name;
            cell.properties = device.properties;
            cell.properties["type"] = device.type;
            callbacks->on_cell(cell);
        }
        for (const auto& net : ctx.ir.nets) {
            ParsedNet parsed;
            parsed.name = net.name;
            parsed.pin_names = net.pin_names;
            parsed.properties = net.properties;
            callbacks->on_net(parsed);
        }
        for (const auto& port : ctx.ir.ports) {
            ParsedPin pin;
            pin.name = port.name;
            pin.net_name = port.net_name;
            pin.direction = port.direction;
            callbacks->on_pin(pin);
        }
    }

    return true;
}

} // namespace aegis::parsing
