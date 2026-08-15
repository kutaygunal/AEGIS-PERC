#pragma once

#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/violation.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aegis::rules {

/**
 * A single predicate evaluated against a field of a graph node
 * (device / net / pin) for a `type: condition` rule pack entry.
 *
 * `value` is unused (and may be empty) for the `exists` / `not_exists`
 * operators. See DeclarativeConditionRule for evaluation semantics.
 */
struct RuleCondition {
    std::string field;
    std::string op; // equals | not_equals | greater_than | greater_or_equal |
                     // less_than | less_or_equal | contains | exists | not_exists
    std::optional<aegis::graph::PropertyValue> value;

    [[nodiscard]] bool operator==(const RuleCondition&) const = default;
};

struct RulePackRuleDefinition {
    std::string id;
    std::string type;
    Severity severity = Severity::Error;
    std::string description;
    aegis::graph::PropertyMap parameters;

    // --- fields used only by `type: condition` (declarative rules) ---
    std::string target; // "device" | "net" | "pin"
    std::vector<RuleCondition> conditions; // ANDed together
    std::string message; // optional template; see DeclarativeConditionRule
};

struct RulePack {
    int version = 1;
    std::vector<RulePackRuleDefinition> rules;
};

class RulePackValidationException : public std::runtime_error {
public:
    explicit RulePackValidationException(const std::string& message);
};

/**
 * Loads and validates rule packs (JSON or YAML) and instantiates the
 * corresponding IRule objects.
 *
 * Built-in rule `type`s:
 *   - "floating_net"           — wraps FloatingNetRule
 *   - "power_domain_mismatch"  — wraps DomainTaggingRule
 *   - "em_current_limit"       — built-in per-layer current threshold check
 *   - "antenna_ratio"          — built-in physical/reliability check:
 *     accumulated connected metal area vs. gate area, per net. See
 *     AntennaRatioRule and docs/design/physical_reliability_checks.md.
 *   - "condition"              — declarative, data-defined check: flags any
 *     device/net/pin whose fields satisfy every entry in `conditions`,
 *     without requiring a new C++ rule class. See DeclarativeConditionRule.
 */
class RulePackLoader {
public:
    RulePack load_from_string(const std::string& content,
                              const std::string& source_name = "<memory>") const;
    RulePack load_from_file(const std::filesystem::path& path) const;

    std::vector<std::unique_ptr<IRule>> instantiate_rules(const RulePack& pack) const;
};

} // namespace aegis::rules
