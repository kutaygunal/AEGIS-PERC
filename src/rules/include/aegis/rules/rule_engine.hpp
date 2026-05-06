#pragma once

#include <memory>
#include <string>
#include <vector>

namespace aegis::rules {

struct RuleContext {
    std::string design_name;
};

class IRule {
public:
    virtual ~IRule() = default;
    virtual std::string id() const = 0;
    virtual void evaluate(const RuleContext& ctx) = 0;
};

class RuleEngine {
public:
    RuleEngine();
    ~RuleEngine();

    RuleEngine(const RuleEngine&) = delete;
    RuleEngine& operator=(const RuleEngine&) = delete;
    RuleEngine(RuleEngine&&) noexcept;
    RuleEngine& operator=(RuleEngine&&) noexcept;

    void add_rule(std::unique_ptr<IRule> rule);
    std::size_t rule_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::rules
