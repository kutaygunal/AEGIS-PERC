#include <catch2/catch_test_macros.hpp>
#include "aegis/rules/rule_engine.hpp"

using namespace aegis::rules;

// ---------------------------------------------------------------------------
// Mock rule for interface contract verification
// ---------------------------------------------------------------------------
class MockRule : public IRule {
public:
    explicit MockRule(std::string rule_id) : m_id(std::move(rule_id)) {}
    std::string id() const override { return m_id; }
    std::string name() const override { return m_id; }
    std::string category() const override { return "mock"; }
    std::vector<Violation> execute(const RuleContext& /*ctx*/) const override {
        ++eval_count;
        return {};
    }
    mutable int eval_count = 0;
private:
    std::string m_id;
};

TEST_CASE("RuleEngine constructs and counts rules", "[rules][p1-010][fast][ModuleBoundary]")
{
    RuleEngine engine;
    REQUIRE(engine.rule_count() == 0);

    engine.register_rule(std::make_unique<MockRule>("R1"));
    REQUIRE(engine.rule_count() == 1);

    engine.register_rule(nullptr); // should be safely ignored
    REQUIRE(engine.rule_count() == 1);
}
