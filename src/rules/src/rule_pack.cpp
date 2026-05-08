#include "aegis/rules/rule_pack.hpp"

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/electrical_rules.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace aegis::rules {
namespace {

using json = nlohmann::json;
using aegis::graph::NetNode;
using aegis::graph::NodeType;
using aegis::graph::PropertyMap;

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
        const std::string value = strip_quotes(raw_value);
        if (value == "true" || value == "false") {
            map = map.with(key, value == "true");
            continue;
        }

        try {
            std::size_t pos = 0;
            const int as_int = std::stoi(value, &pos);
            if (pos == value.size()) {
                map = map.with(key, as_int);
                continue;
            }
        } catch (...) {
        }

        try {
            std::size_t pos = 0;
            const double as_double = std::stod(value, &pos);
            if (pos == value.size()) {
                map = map.with(key, as_double);
                continue;
            }
        } catch (...) {
        }

        map = map.with(key, value);
    }
    return map;
}

std::string default_rule_type_for_id(const std::string& id)
{
    const std::string upper = to_lower(id);
    if (upper == "floating_net" || upper == "elec-001") return "floating_net";
    if (upper == "power_domain_mismatch" || upper == "domain-001") return "power_domain_mismatch";
    if (upper == "em_current_limit") return "em_current_limit";
    return {};
}

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
    if (j.contains("parameters")) {
        def.parameters = property_map_from_json(j.at("parameters"));
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

RulePack load_yaml_rule_pack(const std::string& content)
{
    std::istringstream input(content);
    std::string line;

    RulePack pack;
    bool in_rules = false;
    RulePackRuleDefinition current;
    bool has_current = false;
    bool in_parameters = false;
    int parameters_indent = 0;
    std::vector<std::pair<std::string, std::string>> parameter_pairs;

    auto flush_current = [&]() {
        if (!has_current) return;
        if (current.id.empty()) {
            throw RulePackValidationException("Rule entry is missing required field 'id'");
        }
        if (current.type.empty()) {
            current.type = default_rule_type_for_id(current.id);
        }
        if (current.type.empty()) {
            throw RulePackValidationException("Rule '" + current.id + "' is missing required field 'type' and no default mapping exists");
        }
        current.parameters = property_map_from_yaml_pairs(parameter_pairs);
        pack.rules.push_back(current);
        current = {};
        has_current = false;
        in_parameters = false;
        parameters_indent = 0;
        parameter_pairs.clear();
    };

    while (std::getline(input, line)) {
        const std::string raw = line;
        const std::string stripped = trim(raw);
        if (stripped.empty() || starts_with(stripped, "#")) {
            continue;
        }

        if (!in_rules) {
            if (starts_with(stripped, "version:")) {
                pack.version = std::stoi(trim(stripped.substr(std::string("version:").size())));
                continue;
            }
            if (stripped == "rules:") {
                in_rules = true;
                continue;
            }
            throw RulePackValidationException("Unsupported YAML structure before 'rules:' section");
        }

        const int indent = indentation_of(raw);
        if (starts_with(stripped, "- ")) {
            flush_current();
            has_current = true;
            current.severity = Severity::Error;

            const std::string remainder = trim(stripped.substr(2));
            if (!remainder.empty()) {
                const auto pos = remainder.find(':');
                if (pos == std::string::npos) {
                    throw RulePackValidationException("Malformed rule list item: '" + remainder + "'");
                }
                const std::string key = trim(remainder.substr(0, pos));
                const std::string value = strip_quotes(remainder.substr(pos + 1));
                if (key == "id") current.id = value;
                else if (key == "type") current.type = value;
                else if (key == "severity") current.severity = severity_from_pack_string(value);
                else if (key == "description") current.description = value;
                else {
                    throw RulePackValidationException("Unsupported rule field '" + key + "'");
                }
            }
            continue;
        }

        if (!has_current) {
            throw RulePackValidationException("Encountered YAML rule field before any rule item");
        }

        if (in_parameters && indent <= parameters_indent) {
            in_parameters = false;
        }

        if (in_parameters) {
            const auto pos = stripped.find(':');
            if (pos == std::string::npos) {
                throw RulePackValidationException("Malformed parameter entry: '" + stripped + "'");
            }
            parameter_pairs.emplace_back(trim(stripped.substr(0, pos)), trim(stripped.substr(pos + 1)));
            continue;
        }

        const auto pos = stripped.find(':');
        if (pos == std::string::npos) {
            throw RulePackValidationException("Malformed rule field: '" + stripped + "'");
        }

        const std::string key = trim(stripped.substr(0, pos));
        const std::string value = trim(stripped.substr(pos + 1));
        if (key == "id") current.id = strip_quotes(value);
        else if (key == "type") current.type = strip_quotes(value);
        else if (key == "severity") current.severity = severity_from_pack_string(strip_quotes(value));
        else if (key == "description") current.description = strip_quotes(value);
        else if (key == "parameters") {
            in_parameters = true;
            parameters_indent = indent;
        } else {
            throw RulePackValidationException("Unsupported rule field '" + key + "'");
        }
    }

    if (!in_rules) {
        throw RulePackValidationException("Rule pack must contain a 'rules:' section");
    }

    flush_current();
    if (pack.rules.empty()) {
        throw RulePackValidationException("Rule pack contains no rules");
    }
    return pack;
}

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

void validate_pack(const RulePack& pack)
{
    if (pack.rules.empty()) {
        throw RulePackValidationException("Rule pack contains no rules");
    }

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
        if (type != "floating_net" && type != "power_domain_mismatch" && type != "em_current_limit") {
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
        }
    }
    return rules;
}

} // namespace aegis::rules
