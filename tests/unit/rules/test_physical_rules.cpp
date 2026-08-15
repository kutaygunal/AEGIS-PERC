#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/data_model.hpp"
#include "aegis/rules/physical_rules.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

#include <map>
#include <optional>
#include <string>

using namespace aegis::graph;
using namespace aegis::rules;

namespace {

RuleContext make_ctx(const ConnectivityGraph& graph)
{
    return RuleContext{graph, PropertyMap{}, "test_design"};
}

RulePackRuleDefinition make_def(double max_ratio)
{
    RulePackRuleDefinition def;
    def.id = "ANTENNA_RATIO_TEST";
    def.type = "antenna_ratio";
    def.severity = Severity::Error;
    def.parameters = PropertyMap{}.with("max_ratio", max_ratio);
    return def;
}

/// Adds a net with a single MOSFET device connected via its "gate"
/// terminal, with the given w/l device properties (string form, matching
/// how SPICE ingestion stores them) and the given net metal_area_um2
/// property. Returns the net id.
NodeId add_gate_net(ConnectivityGraph& graph,
                     const std::string& net_name,
                     const std::string& device_name,
                     const std::map<std::string, std::string>& device_properties,
                     std::optional<std::string> metal_area_um2)
{
    std::map<std::string, std::string> net_props;
    if (metal_area_um2.has_value()) {
        net_props["metal_area_um2"] = *metal_area_um2;
    }
    NodeId net = graph.add_net(NetNode{net_name, net_props});
    NodeId dev = graph.add_device(DeviceNode{device_name, "NMOS", device_properties});
    NodeId pin = graph.add_pin(PinNode{device_name + ".gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});
    return net;
}

} // namespace

// ---------------------------------------------------------------------------
// Core execution semantics
// ---------------------------------------------------------------------------

TEST_CASE("AntennaRatioRule no violations on empty graph",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule flags net exceeding threshold",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // gate area = 0.5 * 0.18 = 0.09; ratio = 50 / 0.09 ~= 555.6 > 400
    add_gate_net(graph, "n1", "M1", {{"w", "0.5"}, {"l", "0.18"}}, "50");

    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule_id == "ANTENNA_RATIO_TEST");
    CHECK(violations[0].severity == Severity::Error);
    CHECK(violations[0].message.find("n1") != std::string::npos);
    REQUIRE(violations[0].location.net_name.has_value());
    CHECK(violations[0].location.net_name.value() == "n1");

    const auto ratio = violations[0].metadata.get<double>("ratio");
    REQUIRE(ratio.has_value());
    CHECK(*ratio > 400.0);
}

TEST_CASE("AntennaRatioRule no violation when ratio under threshold",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // gate area = 0.5 * 0.18 = 0.09; ratio = 10 / 0.09 ~= 111.1 < 400
    add_gate_net(graph, "n1", "M1", {{"w", "0.5"}, {"l", "0.18"}}, "10");

    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule not flagged when ratio exactly equals threshold",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // gate area = 1.0 * 1.0 = 1.0; ratio = 400 / 1.0 = 400 == threshold
    add_gate_net(graph, "n1", "M1", {{"w", "1.0"}, {"l", "1.0"}}, "400");

    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty()); // strictly greater-than semantics, matches em_current_limit
}

TEST_CASE("AntennaRatioRule sums gate area across multiple gate-connected devices",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    NodeId net = graph.add_net(NetNode{"bus", {{"metal_area_um2", "50"}}});

    for (const std::string& name : {std::string("M1"), std::string("M2")}) {
        NodeId dev = graph.add_device(DeviceNode{name, "NMOS", {{"w", "0.5"}, {"l", "0.18"}}});
        NodeId pin = graph.add_pin(PinNode{name + ".gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
        graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "gate"});
        graph.add_edge(pin, net, {EdgeType::NetToPin, ""});
    }

    // total gate area = 0.09 + 0.09 = 0.18; ratio = 50 / 0.18 ~= 277.8
    AntennaRatioRule strict(make_def(300.0));
    CHECK(strict.execute(make_ctx(graph)).empty()); // below 300 once both devices are summed

    AntennaRatioRule loose(make_def(200.0));
    auto violations = loose.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    const auto gate_area = violations[0].metadata.get<double>("gate_area_um2");
    REQUIRE(gate_area.has_value());
    CHECK(*gate_area > 0.17);
    CHECK(*gate_area < 0.19);
}

TEST_CASE("AntennaRatioRule skips net with no metal_area_um2 property",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    add_gate_net(graph, "n1", "M1", {{"w", "0.5"}, {"l", "0.18"}}, std::nullopt);

    AntennaRatioRule rule(make_def(0.0)); // even a permissive threshold must not fire
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule skips net with no gate-connected devices",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // Net with metal area but nothing connected via a "gate" terminal.
    NodeId net = graph.add_net(NetNode{"n1", {{"metal_area_um2", "1000"}}});
    NodeId dev = graph.add_device(DeviceNode{"R1", "RES", {}});
    NodeId pin = graph.add_pin(PinNode{"R1.p1", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    AntennaRatioRule rule(make_def(0.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule treats a device missing w/l as contributing zero area, not a crash",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    NodeId net = graph.add_net(NetNode{"n1", {{"metal_area_um2", "50"}}});

    // M1: valid w/l. M2: missing both properties entirely.
    NodeId dev1 = graph.add_device(DeviceNode{"M1", "NMOS", {{"w", "0.5"}, {"l", "0.18"}}});
    NodeId pin1 = graph.add_pin(PinNode{"M1.gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev1, pin1, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});

    NodeId dev2 = graph.add_device(DeviceNode{"M2", "NMOS", {}});
    NodeId pin2 = graph.add_pin(PinNode{"M2.gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph)); // must not crash
    REQUIRE(violations.size() == 1);
    const auto gate_area = violations[0].metadata.get<double>("gate_area_um2");
    REQUIRE(gate_area.has_value());
    CHECK(*gate_area > 0.08);
    CHECK(*gate_area < 0.10); // only M1's area counted
}

TEST_CASE("AntennaRatioRule treats unparsable numeric device data as non-triggering",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // w is not a valid number; the device contributes zero, and since it's
    // the only gate-connected device, the net has zero total gate area and
    // must be skipped entirely (no divide-by-zero, no crash).
    add_gate_net(graph, "n1", "M1", {{"w", "not_a_number"}, {"l", "0.18"}}, "1000");

    AntennaRatioRule rule(make_def(0.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule treats unparsable metal_area_um2 as non-triggering",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    add_gate_net(graph, "n1", "M1", {{"w", "0.5"}, {"l", "0.18"}}, "not_a_number");

    AntennaRatioRule rule(make_def(0.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule prefers explicit gate_area_um2 property over w*l",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    // w/l would give area 0.09; explicit gate_area_um2 overrides to 10.0,
    // which changes whether the ratio crosses the threshold.
    add_gate_net(graph, "n1", "M1",
                 {{"w", "0.5"}, {"l", "0.18"}, {"gate_area_um2", "10.0"}}, "50");

    // ratio via w*l would be 50/0.09 ~= 555.6 (over 400); via override it's
    // 50/10 = 5 (well under 400) -- confirms the override actually wins.
    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("AntennaRatioRule respects field-name override parameters",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    NodeId net = graph.add_net(NetNode{"n1", {{"routed_area", "50"}}});
    NodeId dev = graph.add_device(DeviceNode{"M1", "NMOS", {{"W", "0.5"}, {"L", "0.18"}}});
    NodeId pin = graph.add_pin(PinNode{"M1.g", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "g"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    RulePackRuleDefinition def = make_def(400.0);
    def.parameters = def.parameters
        .with("gate_terminal", std::string("g"))
        .with("gate_width_field", std::string("W"))
        .with("gate_length_field", std::string("L"))
        .with("metal_area_field", std::string("routed_area"));

    AntennaRatioRule rule(def);
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].location.net_name.value() == "n1");
}

TEST_CASE("AntennaRatioRule id/category/description metadata",
          "[PhysicalRules][AntennaRatio][fast]")
{
    RulePackRuleDefinition def = make_def(400.0);
    def.id = "PHYS-001";
    AntennaRatioRule rule(def);
    CHECK(rule.id() == "PHYS-001");
    CHECK(rule.category() == "physical");
    CHECK_FALSE(rule.description().empty());
}

TEST_CASE("AntennaRatioRule multiple independent nets are evaluated independently",
          "[PhysicalRules][AntennaRatio][fast]")
{
    ConnectivityGraph graph;
    add_gate_net(graph, "bad_net", "M1", {{"w", "0.5"}, {"l", "0.18"}}, "50");  // ratio ~555.6
    add_gate_net(graph, "good_net", "M2", {{"w", "0.5"}, {"l", "0.18"}}, "10"); // ratio ~111.1

    AntennaRatioRule rule(make_def(400.0));
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].location.net_name.value() == "bad_net");
}

// ---------------------------------------------------------------------------
// RulePack integration (parsing, validation, instantiate_rules, RuleEngine)
// ---------------------------------------------------------------------------

TEST_CASE("RulePackLoader parses type: antenna_ratio from YAML",
          "[PhysicalRules][RulePack][fast]")
{
    const std::string yaml = R"(rules:
  - id: ANTENNA_M1
    type: antenna_ratio
    severity: high
    description: Antenna ratio on M1
    parameters:
      max_ratio: 400
      gate_terminal: gate
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);
    REQUIRE(pack.rules.size() == 1);
    CHECK(pack.rules[0].id == "ANTENNA_M1");
    CHECK(pack.rules[0].type == "antenna_ratio");
    CHECK(pack.rules[0].severity == Severity::Error); // "high" maps to Severity::Error
    REQUIRE(pack.rules[0].parameters.get<double>("max_ratio").has_value());
    CHECK(pack.rules[0].parameters.get<double>("max_ratio").value() == 400.0);
}

TEST_CASE("RulePackLoader parses type: antenna_ratio from JSON",
          "[PhysicalRules][RulePack][fast]")
{
    const std::string json = R"({
        "rules": [
            {
                "id": "ANTENNA_M1",
                "type": "antenna_ratio",
                "severity": "error",
                "parameters": { "max_ratio": 350 }
            }
        ]
    })";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(json);
    REQUIRE(pack.rules.size() == 1);
    CHECK(pack.rules[0].type == "antenna_ratio");
    REQUIRE(pack.rules[0].parameters.get<double>("max_ratio").has_value());
}

TEST_CASE("RulePackLoader rejects antenna_ratio rule missing max_ratio",
          "[PhysicalRules][RulePack][fast]")
{
    const std::string yaml = R"(rules:
  - id: ANTENNA_BAD
    type: antenna_ratio
    severity: high
)";

    RulePackLoader loader;
    REQUIRE_THROWS_AS(loader.load_from_string(yaml), RulePackValidationException);
}

TEST_CASE("RulePackLoader instantiates antenna_ratio rule and runs through RuleEngine",
          "[PhysicalRules][RulePack][Integration][fast]")
{
    const std::string yaml = R"(rules:
  - id: FLOATING_NET
    type: floating_net
    severity: high
  - id: ANTENNA_M1
    type: antenna_ratio
    severity: medium
    parameters:
      max_ratio: 400
)";

    RulePackLoader loader;
    const RulePack pack = loader.load_from_string(yaml);
    auto instantiated = loader.instantiate_rules(pack);

    RuleEngine engine;
    for (auto& rule : instantiated) {
        engine.register_rule(std::move(rule));
    }

    ConnectivityGraph graph;
    graph.add_net(NetNode{"floating", {}}); // triggers FLOATING_NET
    add_gate_net(graph, "hot_net", "M1", {{"w", "0.5"}, {"l", "0.18"}}, "50"); // triggers ANTENNA_M1

    const auto violations = engine.run_all(RuleContext{graph, PropertyMap{}, "FalconCPU"});
    REQUIRE(violations.size() == 2);

    bool saw_floating = false;
    bool saw_antenna = false;
    for (const auto& violation : violations) {
        if (violation.rule_id == "FLOATING_NET") saw_floating = true;
        if (violation.rule_id == "ANTENNA_M1") {
            saw_antenna = true;
            CHECK(violation.severity == Severity::Warning);
            REQUIRE(violation.location.net_name.has_value());
            CHECK(violation.location.net_name.value() == "hot_net");
        }
    }
    CHECK(saw_floating);
    CHECK(saw_antenna);
}
