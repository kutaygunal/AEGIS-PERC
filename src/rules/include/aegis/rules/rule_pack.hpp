#pragma once

#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/violation.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aegis::rules {

struct RulePackRuleDefinition {
    std::string id;
    std::string type;
    Severity severity = Severity::Error;
    std::string description;
    aegis::graph::PropertyMap parameters;
};

struct RulePack {
    int version = 1;
    std::vector<RulePackRuleDefinition> rules;
};

class RulePackValidationException : public std::runtime_error {
public:
    explicit RulePackValidationException(const std::string& message);
};

class RulePackLoader {
public:
    RulePack load_from_string(const std::string& content,
                              const std::string& source_name = "<memory>") const;
    RulePack load_from_file(const std::filesystem::path& path) const;

    std::vector<std::unique_ptr<IRule>> instantiate_rules(const RulePack& pack) const;
};

} // namespace aegis::rules
