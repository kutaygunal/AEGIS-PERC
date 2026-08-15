#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

using namespace aegis::graph;
using namespace aegis::rules;

namespace {

RuleContext make_ctx(const ConnectivityGraph& graph)
{
    return RuleContext{graph, PropertyMap{}, "declarative_test_design"};
}

std::unique_ptr<IRule> single_rule_from_pack(const std::string& text)
{
    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(text);
    auto rules = loader.instantiate_rules(pack);
    REQUIRE(rules.size() == 1);
    return std::move(rules.front());
}

} // namespace

// ---------------------------------------------------------------------------
// Parsing — JSON
// ---------------------------------------------------------------------------

TEST_CASE("RulePackLoader parses a declarative condition rule from JSON",
          "[rules][S2-001][DeclarativeRule]")
{
    const std::string json = R"({
        "version": 1,
        "rules": [
            {
                "id": "NET-CURRENT-CUSTOM",
                "type": "condition",
                "target": "net",
                "severity": "error",
                "conditions": [
                    {"field": "current_mA", "operator": "greater_than", "value": 100},
                    {"field": "layer", "operator": "equals", "value": "M2"}
                ],
                "message": "Net '{name}' at {current_mA}mA over budget on {layer}"
            }
        ]
    })";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(json);
    REQUIRE(pack.rules.size() == 1);

    const auto& def = pack.rules[0];
    REQUIRE(def.type == "condition");
    REQUIRE(def.target == "net");
    REQUIRE(def.conditions.size() == 2);
    REQUIRE(def.conditions[0].field == "current_mA");
    REQUIRE(def.conditions[0].op == "greater_than");
    REQUIRE(def.conditions[1].field == "layer");
    REQUIRE(def.conditions[1].op == "equals");
    REQUIRE(def.message == "Net '{name}' at {current_mA}mA over budget on {layer}");
}

// ---------------------------------------------------------------------------
// Parsing — YAML
// ---------------------------------------------------------------------------

TEST_CASE("RulePackLoader parses a declarative condition rule from YAML",
          "[rules][S2-001][DeclarativeRule]")
{
    const std::string yaml = R"(version: 1
rules:
  - id: NET-CURRENT-CUSTOM
    type: condition
    target: net
    severity: warning
    conditions:
      - field: current_mA
        operator: greater_than
        value: 100
      - field: layer
        operator: equals
        value: M2
    message: "Net '{name}' at {current_mA}mA over budget on {layer}"
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);
    REQUIRE(pack.rules.size() == 1);

    const auto& def = pack.rules[0];
    REQUIRE(def.type == "condition");
    REQUIRE(def.target == "net");
    REQUIRE(def.severity == Severity::Warning);
    REQUIRE(def.conditions.size() == 2);
    REQUIRE(def.conditions[0].field == "current_mA");
    REQUIRE(def.conditions[0].op == "greater_than");
    REQUIRE(def.conditions[1].field == "layer");
    REQUIRE(def.conditions[1].op == "equals");
    REQUIRE(def.message == "Net '{name}' at {current_mA}mA over budget on {layer}");
}

TEST_CASE("RulePackLoader parses a YAML rule pack mixing built-in and declarative rules",
          "[rules][S2-001][DeclarativeRule]")
{
    const std::string yaml = R"(rules:
  - id: FLOATING_NET
    type: floating_net
    severity: high
  - id: CUSTOM-DEVICE-CHECK
    type: condition
    target: device
    severity: medium
    conditions:
      - field: device_type
        operator: equals
        value: NMOS
      - field: w
        operator: less_than
        value: 0.2
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);
    REQUIRE(pack.rules.size() == 2);
    REQUIRE(pack.rules[1].id == "CUSTOM-DEVICE-CHECK");
    REQUIRE(pack.rules[1].target == "device");
    REQUIRE(pack.rules[1].conditions.size() == 2);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST_CASE("RulePackLoader rejects malformed declarative condition rules",
          "[rules][S2-001][DeclarativeRule]")
{
    RulePackLoader loader;

    // Missing target
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: BAD_TARGET
    type: condition
    conditions:
      - field: current_mA
        operator: greater_than
        value: 10
)"), RulePackValidationException);

    // No conditions
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: NO_CONDITIONS
    type: condition
    target: net
)"), RulePackValidationException);

    // Unsupported operator
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: BAD_OP
    type: condition
    target: net
    conditions:
      - field: current_mA
        operator: roughly_equals
        value: 10
)"), RulePackValidationException);

    // Missing value for a comparison operator (exists/not_exists are exempt)
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: MISSING_VALUE
    type: condition
    target: net
    conditions:
      - field: current_mA
        operator: greater_than
)"), RulePackValidationException);

    // exists does NOT require a value
    REQUIRE_NOTHROW(loader.load_from_string(R"(rules:
  - id: HAS_CURRENT
    type: condition
    target: net
    conditions:
      - field: current_mA
        operator: exists
)"));
}

// ---------------------------------------------------------------------------
// Execution semantics
// ---------------------------------------------------------------------------

TEST_CASE("DeclarativeConditionRule flags nets matching all ANDed conditions",
          "[rules][S2-001][DeclarativeRule]")
{
    auto rule = single_rule_from_pack(R"(rules:
  - id: NET-CURRENT-CUSTOM
    type: condition
    target: net
    severity: error
    conditions:
      - field: current_mA
        operator: greater_than
        value: 100
      - field: layer
        operator: equals
        value: M2
    message: "Net '{name}' at {current_mA}mA over budget on {layer}"
)");

    ConnectivityGraph graph;
    graph.add_net(NetNode{"ok_net", {{"current_mA", "50"}, {"layer", "M2"}}});      // under threshold
    graph.add_net(NetNode{"wrong_layer", {{"current_mA", "150"}, {"layer", "M3"}}}); // wrong layer
    graph.add_net(NetNode{"hot_net", {{"current_mA", "150"}, {"layer", "M2"}}});     // matches both

    const auto violations = rule->execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].location.net_name == "hot_net");
    REQUIRE(violations[0].message == "Net 'hot_net' at 150mA over budget on M2");
    REQUIRE(violations[0].severity == Severity::Error);
}

TEST_CASE("DeclarativeConditionRule supports exists / not_exists",
          "[rules][S2-001][DeclarativeRule]")
{
    auto rule = single_rule_from_pack(R"(rules:
  - id: NET-MISSING-DOMAIN
    type: condition
    target: net
    severity: warning
    conditions:
      - field: domain
        operator: not_exists
)");

    ConnectivityGraph graph;
    graph.add_net(NetNode{"tagged", {{"domain", "VDD"}}});
    graph.add_net(NetNode{"untagged", {}});

    const auto violations = rule->execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].location.net_name == "untagged");
}

TEST_CASE("DeclarativeConditionRule evaluates device fields including device_type",
          "[rules][S2-001][DeclarativeRule]")
{
    auto rule = single_rule_from_pack(R"(rules:
  - id: NARROW_NMOS
    type: condition
    target: device
    severity: medium
    conditions:
      - field: device_type
        operator: equals
        value: NMOS
      - field: w
        operator: less_than
        value: 0.2
)");

    ConnectivityGraph graph;
    graph.add_device(DeviceNode{"M1", "NMOS", {{"w", "0.12"}}});
    graph.add_device(DeviceNode{"M2", "NMOS", {{"w", "0.5"}}});
    graph.add_device(DeviceNode{"M3", "PMOS", {{"w", "0.1"}}});

    const auto violations = rule->execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].location.device_name == "M1");
}

TEST_CASE("DeclarativeConditionRule evaluates pin direction/layer fields",
          "[rules][S2-001][DeclarativeRule]")
{
    auto rule = single_rule_from_pack(R"(rules:
  - id: OUTPUT_ON_M5
    type: condition
    target: pin
    severity: info
    conditions:
      - field: direction
        operator: equals
        value: OUTPUT
      - field: layer
        operator: equals
        value: M5
)");

    ConnectivityGraph graph;
    graph.add_pin(PinNode{"p_out_m5", "OUTPUT", std::string("M5"), std::nullopt, std::nullopt});
    graph.add_pin(PinNode{"p_out_m1", "OUTPUT", std::string("M1"), std::nullopt, std::nullopt});
    graph.add_pin(PinNode{"p_in_m5", "INPUT", std::string("M5"), std::nullopt, std::nullopt});

    const auto violations = rule->execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].location.pin_name == "p_out_m5");
}

TEST_CASE("DeclarativeConditionRule falls back to a generated message when no template is given",
          "[rules][S2-001][DeclarativeRule]")
{
    auto rule = single_rule_from_pack(R"(rules:
  - id: NO_TEMPLATE
    type: condition
    target: net
    severity: error
    conditions:
      - field: current_mA
        operator: greater_than
        value: 10
)");

    ConnectivityGraph graph;
    graph.add_net(NetNode{"n1", {{"current_mA", "99"}}});

    const auto violations = rule->execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].message.find("n1") != std::string::npos);
    REQUIRE(violations[0].message.find("current_mA") != std::string::npos);
    REQUIRE(violations[0].message.find("greater_than") != std::string::npos);
}

TEST_CASE("DeclarativeConditionRule integrates end-to-end through RuleEngine",
          "[rules][S2-001][DeclarativeRule]")
{
    const std::string yaml = R"(rules:
  - id: FLOATING_NET
    type: floating_net
    severity: high
  - id: HOT_NET
    type: condition
    target: net
    severity: error
    conditions:
      - field: current_mA
        operator: greater_than
        value: 100
    message: "Net '{name}' exceeds 100mA"
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);
    auto instantiated = loader.instantiate_rules(pack);

    RuleEngine engine;
    for (auto& rule : instantiated) {
        engine.register_rule(std::move(rule));
    }

    ConnectivityGraph graph;
    graph.add_net(NetNode{"floating", {}});
    graph.add_net(NetNode{"VDD_CPU", {{"current_mA", "150"}}});

    const auto violations = engine.run_all(make_ctx(graph));

    bool saw_floating = false;
    bool saw_hot = false;
    for (const auto& v : violations) {
        if (v.rule_id == "FLOATING_NET") saw_floating = true;
        if (v.rule_id == "HOT_NET") {
            saw_hot = true;
            REQUIRE(v.message == "Net 'VDD_CPU' exceeds 100mA");
        }
    }
    REQUIRE(saw_floating);
    REQUIRE(saw_hot);
}
