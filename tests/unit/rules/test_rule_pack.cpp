#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

using namespace aegis::graph;
using namespace aegis::rules;

namespace {

RuleContext make_ctx(const ConnectivityGraph& graph)
{
    return RuleContext{graph, PropertyMap{}, "FalconCPU"};
}

} // namespace

TEST_CASE("RulePackLoader loads YAML rule packs with MVP rules", "[rules][p6-004][RulePack]")
{
    const std::string yaml = R"(version: 1
rules:
  - id: FLOATING_NET
    type: floating_net
    severity: high
    description: Net has no driver or power source
  - id: POWER_DOMAIN_MISMATCH
    type: power_domain_mismatch
    severity: critical
    description: Signal crosses voltage domains without allowed bridge
  - id: EM_CURRENT_LIMIT
    type: em_current_limit
    severity: medium
    description: Net current exceeds configured limit
    parameters:
      M3_max_mA: 20
      M4_max_mA: 40
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);

    REQUIRE(pack.version == 1);
    REQUIRE(pack.rules.size() == 3);
    REQUIRE(pack.rules[0].id == "FLOATING_NET");
    REQUIRE(pack.rules[0].type == "floating_net");
    REQUIRE(pack.rules[0].severity == Severity::Error);
    REQUIRE(pack.rules[1].severity == Severity::Fatal);
    REQUIRE(pack.rules[2].severity == Severity::Warning);
    REQUIRE(pack.rules[2].parameters.get<int>("M3_max_mA").value_or(0) == 20);
    REQUIRE(pack.rules[2].parameters.get<int>("M4_max_mA").value_or(0) == 40);
}

TEST_CASE("RulePackLoader loads JSON rule packs and rejects invalid schema", "[rules][p6-004][RulePack]")
{
    const std::string json = R"({
        "version": 1,
        "rules": [
            {
                "id": "FLOATING_NET",
                "type": "floating_net",
                "severity": "error",
                "description": "Detect nets with no driver"
            }
        ]
    })";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(json);
    REQUIRE(pack.rules.size() == 1);
    REQUIRE(pack.rules[0].id == "FLOATING_NET");

    REQUIRE_THROWS_AS(loader.load_from_string(R"({"rules":[{"severity":"high"}]})"), RulePackValidationException);
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: BAD_RULE
    type: unsupported
    severity: high
)"), RulePackValidationException);
    REQUIRE_THROWS_AS(loader.load_from_string(R"(rules:
  - id: EM_CURRENT_LIMIT
    type: em_current_limit
    severity: medium
)"), RulePackValidationException);
}

TEST_CASE("RulePackLoader instantiates rules and populates RuleEngine without hard-coded thresholds", "[rules][p6-004][RulePack]")
{
    const std::string yaml = R"(rules:
  - id: FLOATING_NET
    type: floating_net
    severity: high
  - id: EM_CURRENT_LIMIT
    type: em_current_limit
    severity: medium
    parameters:
      M3_max_mA: 20
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
    const auto current_net = graph.add_net(NetNode{"VDD_CPU", {{"current_mA", "52.4"}, {"layer", "M3"}}});
    const auto current_pin = graph.add_pin(PinNode{"port_VDD_CPU", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(current_pin, current_net, {EdgeType::NetToPin, ""});

    const auto violations = engine.run_all(make_ctx(graph));
    REQUIRE(violations.size() == 2);

    bool saw_floating = false;
    bool saw_current = false;
    for (const auto& violation : violations) {
        if (violation.rule_id == "FLOATING_NET") {
            saw_floating = true;
            REQUIRE(violation.severity == Severity::Error);
        }
        if (violation.rule_id == "EM_CURRENT_LIMIT") {
            saw_current = true;
            REQUIRE(violation.severity == Severity::Warning);
            REQUIRE(violation.message.find("52.4") != std::string::npos);
        }
    }

    REQUIRE(saw_floating);
    REQUIRE(saw_current);
}
