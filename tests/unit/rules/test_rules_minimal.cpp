#include <catch2/catch_test_macros.hpp>
#include "aegis/rules/rule_engine.hpp"

using namespace aegis::rules;

// ---------------------------------------------------------------------------
// Mock rule for interface contract verification
// ---------------------------------------------------------------------------
class MockRule : public IRule {
public:
    explicit MockRule(std::string id) : m_id(std::move(id)) {}
    std::string id() const override { return m_id; }
    void evaluate(const RuleContext& /*ctx*/) override { ++eval_count; }
    int eval_count = 0;
private:
    std::string m_id;
};

TEST_CASE("RuleEngine constructs and counts rules", "[rules][p1-010][fast][ModuleBoundary]")
{
    RuleEngine engine;
    REQUIRE(engine.rule_count() == 0);

    engine.add_rule(std::make_unique<MockRule>("R1"));
    REQUIRE(engine.rule_count() == 1);

    engine.add_rule(nullptr); // should be safely ignored
    REQUIRE(engine.rule_count() == 1);
}
