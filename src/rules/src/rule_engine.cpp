#include "aegis/rules/rule_engine.hpp"

namespace aegis::rules {

struct RuleEngine::Impl {
    std::vector<std::unique_ptr<IRule>> rules;
};

RuleEngine::RuleEngine() : m_impl(std::make_unique<Impl>()) {}
RuleEngine::~RuleEngine() = default;
RuleEngine::RuleEngine(RuleEngine&&) noexcept = default;
RuleEngine& RuleEngine::operator=(RuleEngine&&) noexcept = default;

void RuleEngine::add_rule(std::unique_ptr<IRule> rule) {
    if (rule) {
        m_impl->rules.push_back(std::move(rule));
    }
}

std::size_t RuleEngine::rule_count() const {
    return m_impl->rules.size();
}

} // namespace aegis::rules
