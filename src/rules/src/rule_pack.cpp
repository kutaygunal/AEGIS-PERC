#include "aegis/rules/rule_pack.hpp"

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/electrical_rules.hpp"
#include "aegis/rules/physical_rules.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace aegis::rules {
namespace {

using json = nlohmann::json;
using aegis::graph::DeviceNode;
using aegis::graph::NetNode;
using aegis::graph::NodeType;
using aegis::graph::PinNode;
using aegis::graph::PropertyMap;
using aegis::graph::PropertyValue;

std::string trim(std::string s)
{
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

int indentation_of(const std::string& line)
{
    int indent = 0;
    for (char c : line) {
        if (c == ' ') {
            ++indent;
        } else {
            break;
        }
    }
    return indent;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw RulePackValidationException("Unable to open rule pack file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

Severity severity_from_pack_string(const std::string& value)
{
    const std::string normalized = to_lower(trim(value));
    if (normalized == "critical" || normalized == "fatal") return Severity::Fatal;
    if (normalized == "high" || normalized == "error") return Severity::Error;
    if (normalized == "medium" || normalized == "warning") return Severity::Warning;
    if (normalized == "low" || normalized == "info") return Severity::Info;
    throw RulePackValidationException("Unsupported rule severity: '" + value + "'");
}

std::string strip_quotes(std::string value)
{
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

// ---------------------------------------------------------------------------
// Scalar value coercion (shared by "parameters" and "conditions" values)
// ---------------------------------------------------------------------------

PropertyValue coerce_yaml_scalar(const std::string& raw_value)
{
    const std::string value = strip_quotes(raw_value);
    if (value == "true" || value == "false") {
        return value == "true";
    }

    try {
        std::size_t pos = 0;
        const int as_int = std::stoi(value, &pos);
        if (pos == value.size()) {
            return as_int;
        }
    } catch (...) {
    }

    try {
        std::size_t pos = 0;
        const double as_double = std::stod(value, &pos);
        if (pos == value.size()) {
            return as_double;
        }
    } catch (...) {
    }

    return value;
}

PropertyValue property_value_from_json_scalar(const json& v, const std::string& rule_id)
{
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_float()) return v.get<double>();
    if (v.is_string()) return v.get<std::string>();
    throw RulePackValidationException("Rule '" + rule_id + "' has a condition/parameter value with an unsupported JSON type");
}

std::string property_value_as_string(const PropertyValue& v)
{
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    if (auto* i = std::get_if<int>(&v)) return std::to_string(*i);
    if (auto* d = std::get_if<double>(&v)) return std::to_string(*d);
    if (auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    return {};
}

std::optional<double> property_value_as_double(const PropertyValue& v)
{
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* i = std::get_if<int>(&v)) return static_cast<double>(*i);
    if (auto* b = std::get_if<bool>(&v)) return *b ? 1.0 : 0.0;
    if (auto* s = std::get_if<std::string>(&v)) {
        try { return std::stod(*s); } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

PropertyMap property_map_from_json(const json& j)
{
    PropertyMap map;
    if (!j.is_object()) {
        throw RulePackValidationException("Rule parameters must be a JSON object");
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.value().is_boolean()) {
            map = map.with(it.key(), it.value().get<bool>());
        } else if (it.value().is_number_integer()) {
            map = map.with(it.key(), it.value().get<int>());
        } else if (it.value().is_number_float()) {
            map = map.with(it.key(), it.value().get<double>());
        } else if (it.value().is_string()) {
            map = map.with(it.key(), it.value().get<std::string>());
        } else {
            throw RulePackValidationException("Rule parameter '" + it.key() + "' has unsupported type");
        }
    }
    return map;
}

PropertyMap property_map_from_yaml_pairs(const std::vector<std::pair<std::string, std::string>>& pairs)
{
    PropertyMap map;
    for (const auto& [key, raw_value] : pairs) {
        map = map.with(key, coerce_yaml_scalar(raw_value));
    }
    return map;
}

std::string default_rule_type_for_id(const std::string& id)
{
    const std::string upper = to_lower(id);
    if (upper == "floating_net" || upper == "elec-001") return "floating_net";
    if (upper == "power_domain_mismatch" || upper == "domain-001") return "power_domain_mismatch";
    if (upper == "em_current_limit") return "em_current_limit";
    if (upper == "antenna_ratio" || upper == "phys-001") return "antenna_ratio";
    return {};
}

// ---------------------------------------------------------------------------
// JSON loading
// ---------------------------------------------------------------------------

RulePackRuleDefinition rule_def_from_json(const json& j)
{
    if (!j.is_object()) {
        throw RulePackValidationException("Each rule entry must be an object");
    }

    RulePackRuleDefinition def;
    def.id = j.value("id", "");
    if (def.id.empty()) {
        throw RulePackValidationException("Rule entry is missing required field 'id'");
    }
    def.type = j.value("type", default_rule_type_for_id(def.id));
    if (def.type.empty()) {
        throw RulePackValidationException("Rule '" + def.id + "' is missing required field 'type' and no default mapping exists");
    }
    def.severity = severity_from_pack_string(j.value("severity", "error"));
    def.description = j.value("description", "");
    def.target = j.value("target", "");
    def.message = j.value("message", "");
    if (j.contains("parameters")) {
        def.parameters = property_map_from_json(j.at("parameters"));
    }
    if (j.contains("conditions")) {
        if (!j.at("conditions").is_array()) {
            throw RulePackValidationException("Rule '" + def.id + "' field 'conditions' must be an array");
        }
        for (const auto& c : j.at("conditions")) {
            if (!c.is_object() || !c.contains("field") || !c.contains("operator")) {
                throw RulePackValidationException(
                    "Rule '" + def.id + "' has a malformed condition entry (requires 'field' and 'operator')");
            }
            RuleCondition cond;
            cond.field = c.at("field").get<std::string>();
            cond.op = to_lower(c.at("operator").get<std::string>());
            if (c.contains("value")) {
                cond.value = property_value_from_json_scalar(c.at("value"), def.id);
            }
            def.conditions.push_back(std::move(cond));
        }
    }
    return def;
}

RulePack load_json_rule_pack(const std::string& content)
{
    const json root = json::parse(content);
    if (!root.contains("rules") || !root.at("rules").is_array()) {
        throw RulePackValidationException("Rule pack must contain a 'rules' array");
    }

    RulePack pack;
    pack.version = root.value("version", 1);
    for (const auto& entry : root.at("rules")) {
        pack.rules.push_back(rule_def_from_json(entry));
    }
    return pack;
}

// ---------------------------------------------------------------------------
// YAML loading — a small hand-rolled, indentation-based subset parser.
//
// Not a general YAML implementation: it supports exactly the shapes rule
// packs need — a top-level "rules:" list of maps, each map optionally
// containing a nested flat "parameters:" map and/or a nested "conditions:"
// list of flat maps (field/operator/value).
// ---------------------------------------------------------------------------

struct YamlLine {
    int indent;
    std::string text; // already trimmed, comments/blank lines excluded
};

std::vector<YamlLine> tokenize_yaml_lines(const std::string& content)
{
    std::vector<YamlLine> lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string stripped = trim(line);
        if (stripped.empty() || starts_with(stripped, "#")) {
            continue;
        }
        lines.push_back(YamlLine{indentation_of(line), stripped});
    }
    return lines;
}

/// Parses a nested "conditions:" list of flat maps.  On entry, `i` points
/// just past the "conditions:" key line; on exit it points to the first
/// line that is not part of the block (i.e. indent <= field_indent).
std::vector<RuleCondition> parse_conditions_block(
    const std::vector<YamlLine>& lines, std::size_t& i, int field_indent)
{
    std::vector<RuleCondition> conditions;
    if (i >= lines.size() || lines[i].indent <= field_indent) {
        return conditions;
    }

    const int list_indent = lines[i].indent;
    while (i < lines.size() && lines[i].indent == list_indent) {
        if (!starts_with(lines[i].text, "- ") && lines[i].text != "-") {
            throw RulePackValidationException("Malformed condition entry: '" + lines[i].text + "'");
        }

        RuleCondition cond;
        auto apply_kv = [&](const std::string& key, const std::string& raw_value) {
            if (key == "field") cond.field = strip_quotes(raw_value);
            else if (key == "operator") cond.op = to_lower(strip_quotes(raw_value));
            else if (key == "value") cond.value = coerce_yaml_scalar(raw_value);
            else throw RulePackValidationException("Unsupported condition field '" + key + "'");
        };

        const std::string remainder = lines[i].text.size() > 1 ? trim(lines[i].text.substr(2)) : std::string{};
        ++i;
        if (!remainder.empty()) {
            const auto pos = remainder.find(':');
            if (pos == std::string::npos) {
                throw RulePackValidationException("Malformed condition entry: '" + remainder + "'");
            }
            apply_kv(trim(remainder.substr(0, pos)), trim(remainder.substr(pos + 1)));
        }

        const int cond_field_indent = list_indent + 2;
        while (i < lines.size() && lines[i].indent > list_indent) {
            if (lines[i].indent != cond_field_indent) {
                throw RulePackValidationException(
                    "Malformed condition entry (unexpected indentation) near '" + lines[i].text + "'");
            }
            const auto pos = lines[i].text.find(':');
            if (pos == std::string::npos) {
                throw RulePackValidationException("Malformed condition field: '" + lines[i].text + "'");
            }
            apply_kv(trim(lines[i].text.substr(0, pos)), trim(lines[i].text.substr(pos + 1)));
            ++i;
        }

        if (cond.field.empty() || cond.op.empty()) {
            throw RulePackValidationException("Condition entry requires both 'field' and 'operator'");
        }
        conditions.push_back(std::move(cond));
    }
    return conditions;
}

/// Parses the field lines of a single rule item.  `lines` holds only that
/// item's own lines (already collected by the caller); `field_indent` is
/// the indentation shared by this item's top-level keys.
RulePackRuleDefinition parse_rule_item(const std::vector<YamlLine>& lines, int field_indent)
{
    RulePackRuleDefinition def;
    def.severity = Severity::Error;

    std::size_t i = 0;
    while (i < lines.size()) {
        const YamlLine& ln = lines[i];
        if (ln.indent != field_indent) {
            throw RulePackValidationException("Malformed rule entry (unexpected indentation) near '" + ln.text + "'");
        }
        const auto pos = ln.text.find(':');
        if (pos == std::string::npos) {
            throw RulePackValidationException("Malformed rule field: '" + ln.text + "'");
        }
        const std::string key = trim(ln.text.substr(0, pos));
        const std::string value = trim(ln.text.substr(pos + 1));

        if (key == "parameters") {
            ++i;
            std::vector<std::pair<std::string, std::string>> pairs;
            while (i < lines.size() && lines[i].indent > field_indent) {
                const auto p = lines[i].text.find(':');
                if (p == std::string::npos) {
                    throw RulePackValidationException("Malformed parameter entry: '" + lines[i].text + "'");
                }
                pairs.emplace_back(trim(lines[i].text.substr(0, p)), trim(lines[i].text.substr(p + 1)));
                ++i;
            }
            def.parameters = property_map_from_yaml_pairs(pairs);
            continue;
        }

        if (key == "conditions") {
            ++i;
            def.conditions = parse_conditions_block(lines, i, field_indent);
            continue;
        }

        if (key == "id") def.id = strip_quotes(value);
        else if (key == "type") def.type = strip_quotes(value);
        else if (key == "severity") def.severity = severity_from_pack_string(strip_quotes(value));
        else if (key == "description") def.description = strip_quotes(value);
        else if (key == "target") def.target = strip_quotes(value);
        else if (key == "message") def.message = strip_quotes(value);
        else {
            throw RulePackValidationException("Unsupported rule field '" + key + "'");
        }
        ++i;
    }

    if (def.id.empty()) {
        throw RulePackValidationException("Rule entry is missing required field 'id'");
    }
    if (def.type.empty()) {
        def.type = default_rule_type_for_id(def.id);
    }
    if (def.type.empty()) {
        throw RulePackValidationException(
            "Rule '" + def.id + "' is missing required field 'type' and no default mapping exists");
    }
    return def;
}

RulePack load_yaml_rule_pack(const std::string& content)
{
    const std::vector<YamlLine> lines = tokenize_yaml_lines(content);
    RulePack pack;
    std::size_t i = 0;

    if (i < lines.size() && starts_with(lines[i].text, "version:")) {
        pack.version = std::stoi(trim(lines[i].text.substr(std::string("version:").size())));
        ++i;
    }

    if (i >= lines.size() || lines[i].text != "rules:") {
        throw RulePackValidationException("Rule pack must contain a 'rules:' section");
    }
    ++i;

    if (i >= lines.size() || !starts_with(lines[i].text, "- ")) {
        throw RulePackValidationException("Rule pack contains no rules");
    }

    const int list_indent = lines[i].indent;
    const int field_indent = list_indent + 2;

    while (i < lines.size() && lines[i].indent == list_indent) {
        if (!starts_with(lines[i].text, "- ")) {
            throw RulePackValidationException("Malformed rule list item: '" + lines[i].text + "'");
        }

        std::vector<YamlLine> item_lines;
        const std::string remainder = trim(lines[i].text.substr(2));
        if (!remainder.empty()) {
            item_lines.push_back(YamlLine{field_indent, remainder});
        }
        ++i;
        while (i < lines.size() && lines[i].indent > list_indent) {
            item_lines.push_back(lines[i]);
            ++i;
        }

        pack.rules.push_back(parse_rule_item(item_lines, field_indent));
    }

    if (pack.rules.empty()) {
        throw RulePackValidationException("Rule pack contains no rules");
    }
    return pack;
}

// ---------------------------------------------------------------------------
// Rule adapters
// ---------------------------------------------------------------------------

class ConfiguredRuleAdapter final : public IRule {
public:
    ConfiguredRuleAdapter(RulePackRuleDefinition definition, std::unique_ptr<IRule> inner)
        : m_definition(std::move(definition)), m_inner(std::move(inner)) {}

    std::string id() const override { return m_definition.id; }
    std::string name() const override { return m_inner->name(); }
    std::string category() const override { return m_inner->category(); }
    std::string description() const override {
        return !m_definition.description.empty() ? m_definition.description : m_inner->description();
    }
    std::vector<std::string> dependencies() const override { return m_inner->dependencies(); }

    std::vector<Violation> execute(const RuleContext& ctx) const override
    {
        RuleContext delegated = ctx;
        for (const auto& [key, value] : m_definition.parameters) {
            delegated.parameters = delegated.parameters.with(key, value);
        }

        auto violations = m_inner->execute(delegated);
        for (auto& violation : violations) {
            violation.rule_id = m_definition.id;
            violation.severity = m_definition.severity;
            if (!m_definition.description.empty()) {
                violation.metadata = violation.metadata.with("configured_description", m_definition.description);
            }
        }
        return violations;
    }

private:
    RulePackRuleDefinition m_definition;
    std::unique_ptr<IRule> m_inner;
};

class CurrentLimitRule final : public IRule {
public:
    explicit CurrentLimitRule(RulePackRuleDefinition definition)
        : m_definition(std::move(definition)) {}

    std::string id() const override { return m_definition.id; }
    std::string name() const override { return "Current limit check"; }
    std::string category() const override { return "electrical"; }
    std::string description() const override {
        return !m_definition.description.empty()
            ? m_definition.description
            : "Detects nets whose current exceeds configured per-layer thresholds.";
    }

    std::vector<Violation> execute(const RuleContext& ctx) const override
    {
        std::vector<Violation> violations;
        for (std::size_t net_id : ctx.graph.nodes_of_type(NodeType::Net)) {
            const auto& net = std::get<NetNode>(ctx.graph.node_data(net_id));
            const auto current_it = net.properties.find("current_mA");
            const auto layer_it = net.properties.find("layer");
            if (current_it == net.properties.end() || layer_it == net.properties.end()) {
                continue;
            }

            double current = 0.0;
            try {
                current = std::stod(current_it->second);
            } catch (...) {
                continue;
            }

            const std::string threshold_key = layer_it->second + "_max_mA";
            const auto threshold = m_definition.parameters.get<double>(threshold_key);
            if (!threshold.has_value()) {
                continue;
            }

            if (current > *threshold) {
                Violation violation{m_definition.id,
                                    m_definition.severity,
                                    "Net '" + net.name + "' current " + current_it->second +
                                        "mA exceeds configured limit " + std::to_string(*threshold) +
                                        "mA for layer '" + layer_it->second + "'.",
                                    net.name};
                violation.metadata = violation.metadata
                    .with("layer", layer_it->second)
                    .with("current_mA", current)
                    .with("threshold_mA", *threshold);
                violations.push_back(std::move(violation));
            }
        }
        return violations;
    }

private:
    RulePackRuleDefinition m_definition;
};

/**
 * DeclarativeConditionRule — `type: condition` in a rule pack.
 *
 * Iterates every node of `target` type (device/net/pin) in the connectivity
 * graph and flags any node where *every* entry in `conditions` evaluates to
 * true (AND semantics; there is no OR/NOT combinator in this first cut —
 * express alternatives as separate rule entries instead).
 *
 * Field resolution:
 *   - "name" always resolves to the node's name.
 *   - device: "device_type" plus any key in DeviceNode::properties.
 *   - net:    any key in NetNode::properties.
 *   - pin:    "direction", "layer", "x", "y".
 *
 * All graph-side values are compared as strings (parsed to double for the
 * numeric operators), matching how the rest of the graph stores properties.
 */
class DeclarativeConditionRule final : public IRule {
public:
    explicit DeclarativeConditionRule(RulePackRuleDefinition definition)
        : m_definition(std::move(definition)), m_target(to_lower(m_definition.target)) {}

    std::string id() const override { return m_definition.id; }
    std::string name() const override { return "Declarative condition check"; }
    std::string category() const override { return "electrical"; }
    std::string description() const override {
        if (!m_definition.description.empty()) {
            return m_definition.description;
        }
        return "Flags every '" + m_definition.target + "' matching " +
               std::to_string(m_definition.conditions.size()) + " configured condition(s).";
    }

    std::vector<Violation> execute(const RuleContext& ctx) const override
    {
        std::vector<Violation> violations;
        using FieldResolver = std::function<std::optional<std::string>(const std::string&)>;

        auto evaluate_and_emit = [&](const std::string& node_name, const FieldResolver& resolve,
                                      ViolationLocation loc) {
            for (const auto& cond : m_definition.conditions) {
                if (!evaluate_condition(cond, resolve(cond.field))) {
                    return;
                }
            }
            Violation v{m_definition.id, m_definition.severity,
                        m_definition.message.empty() ? default_message(node_name)
                                                      : format_message(m_definition.message, node_name, resolve),
                        loc};
            v.metadata = v.metadata.with("declarative_target", m_definition.target);
            violations.push_back(std::move(v));
        };

        if (m_target == "device") {
            for (std::size_t node_id : ctx.graph.nodes_of_type(NodeType::Device)) {
                const auto& dev = std::get<DeviceNode>(ctx.graph.node_data(node_id));
                ViolationLocation loc;
                loc.device_name = dev.name;
                evaluate_and_emit(dev.name, [&dev](const std::string& field) { return resolve_device_field(dev, field); }, loc);
            }
        } else if (m_target == "net") {
            for (std::size_t node_id : ctx.graph.nodes_of_type(NodeType::Net)) {
                const auto& net = std::get<NetNode>(ctx.graph.node_data(node_id));
                ViolationLocation loc;
                loc.net_name = net.name;
                evaluate_and_emit(net.name, [&net](const std::string& field) { return resolve_net_field(net, field); }, loc);
            }
        } else if (m_target == "pin") {
            for (std::size_t node_id : ctx.graph.nodes_of_type(NodeType::Pin)) {
                const auto& pin = std::get<PinNode>(ctx.graph.node_data(node_id));
                ViolationLocation loc;
                loc.pin_name = pin.name;
                loc.layer = pin.layer;
                evaluate_and_emit(pin.name, [&pin](const std::string& field) { return resolve_pin_field(pin, field); }, loc);
            }
        }
        return violations;
    }

private:
    static std::optional<std::string> resolve_device_field(const DeviceNode& dev, const std::string& field)
    {
        if (field == "name") return dev.name;
        if (field == "device_type") return dev.device_type;
        const auto it = dev.properties.find(field);
        if (it != dev.properties.end()) return it->second;
        return std::nullopt;
    }

    static std::optional<std::string> resolve_net_field(const NetNode& net, const std::string& field)
    {
        if (field == "name") return net.name;
        const auto it = net.properties.find(field);
        if (it != net.properties.end()) return it->second;
        return std::nullopt;
    }

    static std::optional<std::string> resolve_pin_field(const PinNode& pin, const std::string& field)
    {
        if (field == "name") return pin.name;
        if (field == "direction") return pin.direction;
        if (field == "layer") return pin.layer;
        if (field == "x" && pin.x.has_value()) return std::to_string(*pin.x);
        if (field == "y" && pin.y.has_value()) return std::to_string(*pin.y);
        return std::nullopt;
    }

    static bool evaluate_condition(const RuleCondition& cond, const std::optional<std::string>& field_value)
    {
        if (cond.op == "exists") return field_value.has_value();
        if (cond.op == "not_exists") return !field_value.has_value();
        if (!field_value.has_value() || !cond.value.has_value()) return false;

        if (cond.op == "equals") return *field_value == property_value_as_string(*cond.value);
        if (cond.op == "not_equals") return *field_value != property_value_as_string(*cond.value);
        if (cond.op == "contains") return field_value->find(property_value_as_string(*cond.value)) != std::string::npos;

        double lhs = 0.0;
        try {
            lhs = std::stod(*field_value);
        } catch (...) {
            return false;
        }
        const auto rhs = property_value_as_double(*cond.value);
        if (!rhs.has_value()) return false;

        if (cond.op == "greater_than") return lhs > *rhs;
        if (cond.op == "greater_or_equal") return lhs >= *rhs;
        if (cond.op == "less_than") return lhs < *rhs;
        if (cond.op == "less_or_equal") return lhs <= *rhs;
        return false; // unreachable once validate_pack() has run
    }

    static std::string format_message(const std::string& tmpl, const std::string& node_name,
                                       const std::function<std::optional<std::string>(const std::string&)>& resolve)
    {
        std::string result;
        result.reserve(tmpl.size());
        for (std::size_t i = 0; i < tmpl.size();) {
            if (tmpl[i] == '{') {
                const auto close = tmpl.find('}', i);
                if (close != std::string::npos) {
                    const std::string key = tmpl.substr(i + 1, close - i - 1);
                    const std::optional<std::string> value =
                        (key == "name") ? std::optional<std::string>(node_name) : resolve(key);
                    result += value.value_or("{" + key + "}");
                    i = close + 1;
                    continue;
                }
            }
            result += tmpl[i++];
        }
        return result;
    }

    std::string default_message(const std::string& node_name) const
    {
        std::ostringstream oss;
        oss << m_definition.target << " '" << node_name << "' matched all conditions for rule '"
            << m_definition.id << "': ";
        for (std::size_t i = 0; i < m_definition.conditions.size(); ++i) {
            if (i != 0) oss << "; ";
            const auto& cond = m_definition.conditions[i];
            oss << cond.field << " " << cond.op;
            if (cond.value.has_value()) oss << " " << property_value_as_string(*cond.value);
        }
        return oss.str();
    }

    RulePackRuleDefinition m_definition;
    std::string m_target;
};

void validate_pack(const RulePack& pack)
{
    if (pack.rules.empty()) {
        throw RulePackValidationException("Rule pack contains no rules");
    }

    static const std::unordered_set<std::string> valid_condition_ops = {
        "equals", "not_equals", "greater_than", "greater_or_equal",
        "less_than", "less_or_equal", "contains", "exists", "not_exists"};

    std::unordered_map<std::string, int> counts;
    for (const auto& rule : pack.rules) {
        if (rule.id.empty()) {
            throw RulePackValidationException("Rule entry is missing required field 'id'");
        }
        if (rule.type.empty()) {
            throw RulePackValidationException("Rule '" + rule.id + "' has empty type");
        }
        if (++counts[rule.id] > 1) {
            throw RulePackValidationException("Duplicate rule id in pack: '" + rule.id + "'");
        }

        const std::string type = to_lower(rule.type);
        if (type != "floating_net" && type != "power_domain_mismatch" &&
            type != "em_current_limit" && type != "antenna_ratio" && type != "condition") {
            throw RulePackValidationException("Unsupported rule type: '" + rule.type + "'");
        }

        if (type == "em_current_limit") {
            bool has_threshold = false;
            for (const auto& [key, value] : rule.parameters) {
                (void)value;
                if (key.find("_max_mA") != std::string::npos) {
                    has_threshold = true;
                    break;
                }
            }
            if (!has_threshold) {
                throw RulePackValidationException("Rule '" + rule.id + "' requires at least one '*_max_mA' parameter");
            }
        }

        if (type == "antenna_ratio") {
            if (!rule.parameters.get<double>("max_ratio").has_value()) {
                throw RulePackValidationException(
                    "Rule '" + rule.id + "' (type 'antenna_ratio') requires a numeric 'max_ratio' parameter");
            }
        }

        if (type == "condition") {
            const std::string target = to_lower(rule.target);
            if (target != "device" && target != "net" && target != "pin") {
                throw RulePackValidationException(
                    "Rule '" + rule.id + "' has invalid or missing 'target' (expected device, net, or pin)");
            }
            if (rule.conditions.empty()) {
                throw RulePackValidationException(
                    "Rule '" + rule.id + "' (type 'condition') requires at least one entry in 'conditions'");
            }
            for (const auto& cond : rule.conditions) {
                if (cond.field.empty()) {
                    throw RulePackValidationException("Rule '" + rule.id + "' has a condition with an empty 'field'");
                }
                if (valid_condition_ops.find(cond.op) == valid_condition_ops.end()) {
                    throw RulePackValidationException(
                        "Rule '" + rule.id + "' has an unsupported condition operator: '" + cond.op + "'");
                }
                if (cond.op != "exists" && cond.op != "not_exists" && !cond.value.has_value()) {
                    throw RulePackValidationException(
                        "Rule '" + rule.id + "' condition on field '" + cond.field + "' requires a 'value'");
                }
            }
        }
    }
}

} // namespace

RulePackValidationException::RulePackValidationException(const std::string& message)
    : std::runtime_error(message) {}

RulePack RulePackLoader::load_from_string(const std::string& content,
                                          const std::string& /*source_name*/) const
{
    const std::string trimmed = trim(content);
    if (trimmed.empty()) {
        throw RulePackValidationException("Rule pack content is empty");
    }

    RulePack pack = (!trimmed.empty() && trimmed.front() == '{')
        ? load_json_rule_pack(content)
        : load_yaml_rule_pack(content);
    validate_pack(pack);
    return pack;
}

RulePack RulePackLoader::load_from_file(const std::filesystem::path& path) const
{
    return load_from_string(read_text_file(path), path.string());
}

std::vector<std::unique_ptr<IRule>> RulePackLoader::instantiate_rules(const RulePack& pack) const
{
    validate_pack(pack);

    std::vector<std::unique_ptr<IRule>> rules;
    rules.reserve(pack.rules.size());
    for (const auto& definition : pack.rules) {
        const std::string type = to_lower(definition.type);
        if (type == "floating_net") {
            rules.push_back(std::make_unique<ConfiguredRuleAdapter>(definition, std::make_unique<FloatingNetRule>()));
        } else if (type == "power_domain_mismatch") {
            rules.push_back(std::make_unique<ConfiguredRuleAdapter>(definition, std::make_unique<DomainTaggingRule>()));
        } else if (type == "em_current_limit") {
            rules.push_back(std::make_unique<CurrentLimitRule>(definition));
        } else if (type == "antenna_ratio") {
            rules.push_back(std::make_unique<AntennaRatioRule>(definition));
        } else if (type == "condition") {
            rules.push_back(std::make_unique<DeclarativeConditionRule>(definition));
        }
    }
    return rules;
}

} // namespace aegis::rules
