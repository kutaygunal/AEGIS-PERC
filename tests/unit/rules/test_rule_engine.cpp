#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/rule_engine.hpp"

using namespace aegis::graph;
using namespace aegis::rules;

// ---------------------------------------------------------------------------
// Mock rules for contract testing
// ---------------------------------------------------------------------------

class AlwaysPassRule : public IRule {
public:
    std::string id() const override { return "PASS-01"; }
    std::string name() const override { return "Always Pass"; }
    std::string category() const override { return "mock"; }
    std::vector<Violation> execute(const RuleContext&) const override {
        return {};
    }
};

class AlwaysFailRule : public IRule {
public:
    std::string id() const override { return "FAIL-01"; }
    std::string name() const override { return "Always Fail"; }
    std::string category() const override { return "mock"; }
    std::vector<Violation> execute(const RuleContext&) const override {
        return {Violation{this->id(), Severity::Error, "expected failure"}};
    }
};

class GraphQueryRule : public IRule {
public:
    std::string id() const override { return "QUERY-01"; }
    std::string name() const override { return "Graph Query"; }
    std::string category() const override { return "electrical"; }
    std::vector<Violation> execute(const RuleContext& ctx) const override {
        if (ctx.graph.empty()) {
            return {Violation{this->id(), Severity::Error, "graph is empty"}};
        }
        if (ctx.graph.node_count() < 5) {
            return {Violation{this->id(), Severity::Warning, "too few nodes"}};
        }
        return {};
    }
};

class DependentRule : public IRule {
public:
    explicit DependentRule(std::vector<std::string> deps)
        : m_deps(std::move(deps)) {}
    std::string id() const override { return "DEP-01"; }
    std::string name() const override { return "Dependent"; }
    std::string category() const override { return "mock"; }
    std::vector<std::string> dependencies() const override { return m_deps; }
    std::vector<Violation> execute(const RuleContext&) const override {
        return {};
    }
private:
    std::vector<std::string> m_deps;
};

class ThrowingRule : public IRule {
public:
    std::string id() const override { return "THROW-01"; }
    std::string name() const override { return "Throws"; }
    std::string category() const override { return "mock"; }
    std::vector<Violation> execute(const RuleContext&) const override {
        throw std::runtime_error("boom");
    }
};

class ParameterCheckingRule : public IRule {
public:
    std::string id() const override { return "PARAM-01"; }
    std::string name() const override { return "Parameter Check"; }
    std::string category() const override { return "mock"; }
    std::vector<Violation> execute(const RuleContext& ctx) const override {
        auto threshold = ctx.parameters.get<double>("threshold");
        if (!threshold.has_value() || threshold.value() > 1.0) {
            return {Violation{this->id(), Severity::Warning, "threshold too high"}};
        }
        return {};
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static RuleContext make_context(const ConnectivityGraph& graph,
                                PropertyMap params = PropertyMap()) {
    return RuleContext{graph, std::move(params), "test_design"};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("RuleEngine constructs with zero rules",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    REQUIRE(engine.rule_count() == 0);
    REQUIRE(engine.rule_ids().empty());
    REQUIRE(engine.categories().empty());
}

TEST_CASE("RuleEngine registers and counts rules",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    REQUIRE(engine.rule_count() == 1);
    REQUIRE(engine.has_rule("PASS-01"));
    REQUIRE(!engine.has_rule("PASS-02"));

    engine.register_rule(std::make_unique<AlwaysFailRule>());
    REQUIRE(engine.rule_count() == 2);
    REQUIRE(engine.has_rule("FAIL-01"));
}

TEST_CASE("RuleEngine rejects duplicate registration",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    REQUIRE_THROWS_AS(
        engine.register_rule(std::make_unique<AlwaysPassRule>()),
        std::invalid_argument);
}

TEST_CASE("RuleEngine rejects empty id",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    struct EmptyId : public IRule {
        std::string id() const override { return ""; }
        std::string name() const override { return "bad"; }
        std::string category() const override { return "mock"; }
        std::vector<Violation> execute(const RuleContext&) const override { return {}; }
    };
    REQUIRE_THROWS_AS(
        engine.register_rule(std::make_unique<EmptyId>()),
        std::invalid_argument);
}

TEST_CASE("RuleEngine ignores null registration",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::unique_ptr<IRule>{nullptr});
    REQUIRE(engine.rule_count() == 0);
}

TEST_CASE("RuleEngine unregisters rule",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    engine.register_rule(std::make_unique<AlwaysFailRule>());
    REQUIRE(engine.rule_count() == 2);

    engine.unregister_rule("PASS-01");
    REQUIRE(engine.rule_count() == 1);
    REQUIRE(!engine.has_rule("PASS-01"));
    REQUIRE(engine.has_rule("FAIL-01"));
}

TEST_CASE("RuleEngine unregister is no-op for missing id",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.unregister_rule("nonexistent");
    REQUIRE(engine.rule_count() == 0);
}

TEST_CASE("RuleEngine lists rule ids",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    engine.register_rule(std::make_unique<AlwaysFailRule>());
    auto ids = engine.rule_ids();
    REQUIRE(ids.size() == 2);
    REQUIRE((ids[0] == "PASS-01" || ids[1] == "PASS-01"));
}

TEST_CASE("RuleEngine lists unique categories",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    engine.register_rule(std::make_unique<GraphQueryRule>());
    auto cats = engine.categories();
    REQUIRE(cats.size() == 2);
    REQUIRE((cats[0] == "mock" || cats[1] == "mock"));
    REQUIRE((cats[0] == "electrical" || cats[1] == "electrical"));
}

TEST_CASE("RuleEngine run_all returns empty for empty engine",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    ConnectivityGraph graph;
    auto v = engine.run_all(make_context(graph));
    REQUIRE(v.empty());
}

TEST_CASE("RuleEngine run_all executes pass and fail rules",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    engine.register_rule(std::make_unique<AlwaysFailRule>());

    ConnectivityGraph graph;
    auto violations = engine.run_all(make_context(graph));

    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].rule_id == "FAIL-01");
    REQUIRE(violations[0].severity == Severity::Error);
    REQUIRE(violations[0].message == "expected failure");
}

TEST_CASE("RuleEngine run_all passes graph context",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<GraphQueryRule>());

    ConnectivityGraph empty;
    auto v_empty = engine.run_all(make_context(empty));
    REQUIRE(v_empty.size() == 1);
    REQUIRE(v_empty[0].message == "graph is empty");

    ConnectivityGraph graph;
    graph.add_device(DeviceNode{"D1", "NMOS", {}});
    graph.add_device(DeviceNode{"D2", "NMOS", {}});
    graph.add_net(NetNode{"nA"});
    auto v_warn = engine.run_all(make_context(graph));
    REQUIRE(v_warn.size() == 1);
    REQUIRE(v_warn[0].severity == Severity::Warning);
    REQUIRE(v_warn[0].message == "too few nodes");

    ConnectivityGraph big;
    for (int i = 0; i < 5; ++i) {
        big.add_device(DeviceNode{"D" + std::to_string(i), "NMOS", {}});
    }
    auto v_ok = engine.run_all(make_context(big));
    REQUIRE(v_ok.empty());
}

TEST_CASE("RuleEngine run_all respects dependency order",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;

    struct OrderRule : public IRule {
        OrderRule(std::string id_, std::vector<std::string> deps,
                  std::vector<std::string>& out_order)
            : m_id(std::move(id_)), m_deps(std::move(deps)),
              m_order(out_order) {}
        std::string id() const override { return m_id; }
        std::string name() const override { return m_id; }
        std::string category() const override { return "mock"; }
        std::vector<std::string> dependencies() const override { return m_deps; }
        std::vector<Violation> execute(const RuleContext&) const override {
            m_order.push_back(m_id);
            return {};
        }
    private:
        std::string m_id;
        std::vector<std::string> m_deps;
        std::vector<std::string>& m_order;
    };

    std::vector<std::string> order;
    engine.register_rule(std::make_unique<OrderRule>(
        "C", std::vector<std::string>{"A", "B"}, order));
    engine.register_rule(std::make_unique<OrderRule>(
        "A", std::vector<std::string>{}, order));
    engine.register_rule(std::make_unique<OrderRule>(
        "B", std::vector<std::string>{"A"}, order));

    ConnectivityGraph graph;
    auto v = engine.run_all(make_context(graph));
    REQUIRE(v.empty());

    auto posA = std::find(order.begin(), order.end(), "A");
    auto posB = std::find(order.begin(), order.end(), "B");
    auto posC = std::find(order.begin(), order.end(), "C");
    REQUIRE(posA < posB);
    REQUIRE(posA < posC);
    REQUIRE(posB < posC);
}

TEST_CASE("RuleEngine run_all detects dependency cycle",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    struct CycleRule : public IRule {
        CycleRule(std::string id_, std::vector<std::string> deps)
            : m_id(std::move(id_)), m_deps(std::move(deps)) {}
        std::string id() const override { return m_id; }
        std::string name() const override { return m_id; }
        std::string category() const override { return "mock"; }
        std::vector<std::string> dependencies() const override { return m_deps; }
        std::vector<Violation> execute(const RuleContext&) const override { return {}; }
    private:
        std::string m_id;
        std::vector<std::string> m_deps;
    };

    engine.register_rule(std::make_unique<CycleRule>("A", std::vector<std::string>{"B"}));
    engine.register_rule(std::make_unique<CycleRule>("B", std::vector<std::string>{"A"}));

    ConnectivityGraph graph;
    REQUIRE_THROWS(engine.run_all(make_context(graph)));
}

TEST_CASE("RuleEngine run_all detects unknown dependency",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<DependentRule>(
        std::vector<std::string>{"MISSING"}));

    ConnectivityGraph graph;
    REQUIRE_THROWS(engine.run_all(make_context(graph)));
}

TEST_CASE("RuleEngine catches rule exceptions as fatal violations",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<ThrowingRule>());
    engine.register_rule(std::make_unique<AlwaysPassRule>());

    ConnectivityGraph graph;
    auto v = engine.run_all(make_context(graph));

    REQUIRE(v.size() == 1);
    REQUIRE(v[0].rule_id == "THROW-01");
    REQUIRE(v[0].severity == Severity::Fatal);
    REQUIRE(v[0].message == "boom");
}

TEST_CASE("RuleEngine run_one by id",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysFailRule>());

    ConnectivityGraph graph;
    auto v = engine.run_one("FAIL-01", make_context(graph));
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].rule_id == "FAIL-01");
}

TEST_CASE("RuleEngine run_one throws for unknown id",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    ConnectivityGraph graph;
    REQUIRE_THROWS_AS(
        engine.run_one("UNKNOWN", make_context(graph)),
        std::invalid_argument);
}

TEST_CASE("RuleEngine run_category filters and runs",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    engine.register_rule(std::make_unique<GraphQueryRule>());

    ConnectivityGraph graph;

    auto v_elec = engine.run_category("electrical", make_context(graph));
    REQUIRE(v_elec.size() == 1);
    REQUIRE(v_elec[0].message == "graph is empty");

    auto v_mock = engine.run_category("mock", make_context(graph));
    REQUIRE(v_mock.empty());
}

TEST_CASE("RuleEngine run_category throws for unknown category",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    ConnectivityGraph graph;
    REQUIRE_THROWS_AS(
        engine.run_category("nonexistent", make_context(graph)),
        std::invalid_argument);
}

TEST_CASE("RuleEngine run_category with empty category is error",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    ConnectivityGraph graph;
    REQUIRE_THROWS_AS(
        engine.run_category("anything", make_context(graph)),
        std::invalid_argument);
}

TEST_CASE("RuleEngine passes parameters through context",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<ParameterCheckingRule>());

    ConnectivityGraph graph;
    auto low = engine.run_all(make_context(graph, PropertyMap().with("threshold", 0.5)));
    REQUIRE(low.empty());

    auto high = engine.run_all(make_context(graph, PropertyMap().with("threshold", 2.0)));
    REQUIRE(high.size() == 1);
    REQUIRE(high[0].message == "threshold too high");

    auto missing = engine.run_all(make_context(graph));
    REQUIRE(missing.size() == 1);
    REQUIRE(missing[0].message == "threshold too high");
}

TEST_CASE("RuleEngine run_one catches exception as fatal",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<ThrowingRule>());
    ConnectivityGraph graph;

    auto v = engine.run_one("THROW-01", make_context(graph));
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].severity == Severity::Fatal);
    REQUIRE(v[0].rule_id == "THROW-01");
}

TEST_CASE("RuleEngine move semantics",
          "[rules][RuleEngine][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<AlwaysPassRule>());
    REQUIRE(engine.rule_count() == 1);

    auto engine2 = std::move(engine);
    REQUIRE(engine2.rule_count() == 1);
    REQUIRE(engine2.has_rule("PASS-01"));
}
