#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/data_model.hpp"
#include "aegis/rules/electrical_rules.hpp"
#include "aegis/rules/rule_engine.hpp"

#include <string>

using namespace aegis::graph;
using namespace aegis::rules;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static RuleContext make_ctx(const ConnectivityGraph& graph) {
    return RuleContext{graph, PropertyMap{}, "test_design"};
}

TEST_CASE("FloatingNetRule no violations on empty graph",
          "[ElectricalRules][FloatingNet][fast]")
{
    ConnectivityGraph graph;
    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("FloatingNetRule no violation when net has device pin",
          "[ElectricalRules][FloatingNet][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto net = graph.add_net(NetNode{"n1", {}});
    auto pin = graph.add_pin(PinNode{"M1.drain", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "drain"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("FloatingNetRule no violation when net has port pin",
          "[ElectricalRules][FloatingNet][fast]")
{
    // External port pin connected directly to net (no device)
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"VDD", {}});
    auto pin = graph.add_pin(PinNode{"port_VDD", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("FloatingNetRule reports isolated net",
          "[ElectricalRules][FloatingNet][fast]")
{
    ConnectivityGraph graph;
    graph.add_net(NetNode{"floating_net", {}});

    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule_id == "ELEC-001");
    CHECK(violations[0].severity == Severity::Error);
    CHECK(violations[0].message.find("floating_net") != std::string::npos);
    REQUIRE(violations[0].location.net_name.has_value());
    CHECK(violations[0].location.net_name.value() == "floating_net");
}

TEST_CASE("FloatingNetRule reports multiple isolated nets",
          "[ElectricalRules][FloatingNet][fast]")
{
    ConnectivityGraph graph;
    graph.add_net(NetNode{"float_a", {}});
    graph.add_net(NetNode{"float_b", {}});

    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 2);
}

TEST_CASE("FloatingNetRule ignores nets with both device and port pins",
          "[ElectricalRules][FloatingNet][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    // Device pin
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto dpin = graph.add_pin(PinNode{"M1.drain", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, dpin, {EdgeType::DeviceToPin, "drain"});
    graph.add_edge(dpin, net, {EdgeType::NetToPin, ""});

    // Port pin
    auto ppin = graph.add_pin(PinNode{"port1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(ppin, net, {EdgeType::NetToPin, ""});

    FloatingNetRule rule;
    auto violations = rule.execute(make_ctx(graph));
    CHECK(violations.empty());
}

// ---------------------------------------------------------------------------
// Open-Circuit Rule
// ---------------------------------------------------------------------------

TEST_CASE("OpenCircuitRule no violations on empty graph",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("OpenCircuitRule no violation for fully connected pin",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"D1", "RES", {}});
    auto net = graph.add_net(NetNode{"n1", {}});
    auto pin = graph.add_pin(PinNode{"D1.p1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("OpenCircuitRule reports pin with device but no net",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"D1", "RES", {}});
    auto pin = graph.add_pin(PinNode{"D1.p1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    // No edge from pin to net

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule_id == "ELEC-002");
    CHECK(violations[0].severity == Severity::Error);
    CHECK(violations[0].message.find("open circuit") != std::string::npos);
    REQUIRE(violations[0].location.net_name.has_value());
    CHECK(violations[0].location.net_name.value() == "D1.p1");
}

TEST_CASE("OpenCircuitRule reports multiple open pins",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"D1", "RES", {}});

    auto pg = graph.add_pin(PinNode{"D1.p1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    auto ps = graph.add_pin(PinNode{"D1.p2", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pg, {EdgeType::DeviceToPin, "p1"});
    graph.add_edge(dev, ps, {EdgeType::DeviceToPin, "p2"});
    // Neither pin connected to net

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 2);
}

TEST_CASE("OpenCircuitRule port-only pin is not open",
          "[ElectricalRules][OpenCircuit][fast]")
{
    // Port pins don't have a DeviceToPin incoming edge, so they
    // shouldn't trigger an open-circuit error.
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"VDD", {}});
    auto pin = graph.add_pin(PinNode{"vdd_port", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("OpenCircuitRule reports missing required NMOS terminals",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});

    // Only connect source (missing gate and drain)
    auto ps = graph.add_pin(PinNode{"M1.source", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    auto net = graph.add_net(NetNode{"src", {}});
    graph.add_edge(dev, ps, {EdgeType::DeviceToPin, "source"});
    graph.add_edge(ps, net, {EdgeType::NetToPin, ""});

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 2); // missing gate and drain
    for (const auto& v : violations) {
        CHECK(v.rule_id == "ELEC-002");
        CHECK(v.severity == Severity::Error);
    }
}

TEST_CASE("OpenCircuitRule no violation for complete NMOS",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto n1 = graph.add_net(NetNode{"g", {}});
    auto n2 = graph.add_net(NetNode{"s", {}});
    auto n3 = graph.add_net(NetNode{"d", {}});

    auto pg = graph.add_pin(PinNode{"M1.gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    auto ps = graph.add_pin(PinNode{"M1.source", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    auto pd = graph.add_pin(PinNode{"M1.drain", "BIDIR", std::nullopt, std::nullopt, std::nullopt});

    graph.add_edge(dev, pg, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(dev, ps, {EdgeType::DeviceToPin, "source"});
    graph.add_edge(dev, pd, {EdgeType::DeviceToPin, "drain"});

    graph.add_edge(pg, n1, {EdgeType::NetToPin, ""});
    graph.add_edge(ps, n2, {EdgeType::NetToPin, ""});
    graph.add_edge(pd, n3, {EdgeType::NetToPin, ""});

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("OpenCircuitRule no required terminals for unknown device type",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    graph.add_device(DeviceNode{"R1", "RES", {}});
    // RES has no required terminals in our rule set

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("OpenCircuitRule reports missing terminal for PMOS too",
          "[ElectricalRules][OpenCircuit][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"M2", "PMOS", {}});

    auto pg = graph.add_pin(PinNode{"M2.gate", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    auto pd = graph.add_pin(PinNode{"M2.drain", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    auto n1 = graph.add_net(NetNode{"g", {}});
    auto n2 = graph.add_net(NetNode{"d", {}});

    graph.add_edge(dev, pg, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(dev, pd, {EdgeType::DeviceToPin, "drain"});
    graph.add_edge(pg, n1, {EdgeType::NetToPin, ""});
    graph.add_edge(pd, n2, {EdgeType::NetToPin, ""});
    // missing source

    OpenCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].message.find("source") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Short-Circuit / Driver Contention Rule
// ---------------------------------------------------------------------------

TEST_CASE("ShortCircuitRule no violations on empty graph",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("ShortCircuitRule no violation for single driver",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    // One OUTPUT driver
    auto dev = graph.add_device(DeviceNode{"INV1", "NMOS", {}});
    auto pin = graph.add_pin(PinNode{"INV1.out", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "out"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    // One INPUT load
    auto dev2 = graph.add_device(DeviceNode{"INV2", "PMOS", {}});
    auto pin2 = graph.add_pin(PinNode{"INV2.in", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "in"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("ShortCircuitRule reports two OUTPUT drivers on same net",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    auto dev1 = graph.add_device(DeviceNode{"INV1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"INV1.out", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev1, pin1, {EdgeType::DeviceToPin, "out"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});

    auto dev2 = graph.add_device(DeviceNode{"INV2", "NMOS", {}});
    auto pin2 = graph.add_pin(PinNode{"INV2.out", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "out"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule_id == "ELEC-003");
    CHECK(violations[0].severity == Severity::Error);
    CHECK(violations[0].message.find("driver contention") != std::string::npos);
    REQUIRE(violations[0].location.net_name.has_value());
    CHECK(violations[0].location.net_name.value() == "n1");
}

TEST_CASE("ShortCircuitRule reports OUTPUT plus INOUT drivers",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"bus", {}});

    auto dev1 = graph.add_device(DeviceNode{"D1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"D1.out", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev1, pin1, {EdgeType::DeviceToPin, "out"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});

    auto dev2 = graph.add_device(DeviceNode{"D2", "NMOS", {}});
    auto pin2 = graph.add_pin(PinNode{"D2.io", "INOUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "io"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
}

TEST_CASE("ShortCircuitRule counts BIDIR as driver",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    auto dev1 = graph.add_device(DeviceNode{"INV1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"INV1.d", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev1, pin1, {EdgeType::DeviceToPin, "d"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});

    auto dev2 = graph.add_device(DeviceNode{"INV2", "NMOS", {}});
    auto pin2 = graph.add_pin(PinNode{"INV2.d", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "d"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
}

TEST_CASE("ShortCircuitRule no violation with only INPUT pins",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"in_net", {}});

    auto dev1 = graph.add_device(DeviceNode{"G1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"G1.in1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev1, pin1, {EdgeType::DeviceToPin, "in1"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});

    auto dev2 = graph.add_device(DeviceNode{"G2", "NMOS", {}});
    auto pin2 = graph.add_pin(PinNode{"G2.in2", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev2, pin2, {EdgeType::DeviceToPin, "in2"});
    graph.add_edge(pin2, net, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.empty());
}

TEST_CASE("ShortCircuitRule multiple independent nets",
          "[ElectricalRules][ShortCircuit][fast]")
{
    ConnectivityGraph graph;
    auto net1 = graph.add_net(NetNode{"out1", {}});
    auto net2 = graph.add_net(NetNode{"out2", {}});

    // net1: two drivers (bad)
    auto d1a = graph.add_device(DeviceNode{"D1a", "NMOS", {}});
    auto p1a = graph.add_pin(PinNode{"D1a.o", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(d1a, p1a, {EdgeType::DeviceToPin, "o"});
    graph.add_edge(p1a, net1, {EdgeType::NetToPin, ""});

    auto d1b = graph.add_device(DeviceNode{"D1b", "NMOS", {}});
    auto p1b = graph.add_pin(PinNode{"D1b.o", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(d1b, p1b, {EdgeType::DeviceToPin, "o"});
    graph.add_edge(p1b, net1, {EdgeType::NetToPin, ""});

    // net2: one driver (good)
    auto d2 = graph.add_device(DeviceNode{"D2", "NMOS", {}});
    auto p2 = graph.add_pin(PinNode{"D2.o", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(d2, p2, {EdgeType::DeviceToPin, "o"});
    graph.add_edge(p2, net2, {EdgeType::NetToPin, ""});

    ShortCircuitRule rule;
    auto violations = rule.execute(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].location.net_name.has_value());
    CHECK(violations[0].location.net_name.value() == "out1");
}

// ---------------------------------------------------------------------------
// Integration with RuleEngine
// ---------------------------------------------------------------------------

TEST_CASE("RuleEngine runs electrical rules in dependency order",
          "[ElectricalRules][Integration][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<FloatingNetRule>());
    engine.register_rule(std::make_unique<OpenCircuitRule>());
    engine.register_rule(std::make_unique<ShortCircuitRule>());

    ConnectivityGraph graph;
    graph.add_net(NetNode{"float_net", {}});

    auto dev = graph.add_device(DeviceNode{"R1", "RES", {}});
    auto pin = graph.add_pin(PinNode{"R1.p1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    // pin not connected to net → open circuit
    // net has no pins → floating net

    auto violations = engine.run_all(make_ctx(graph));
    REQUIRE(violations.size() == 2);

    std::size_t floating_count = 0;
    std::size_t open_count = 0;
    for (const auto& v : violations) {
        if (v.rule_id == "ELEC-001") ++floating_count;
        if (v.rule_id == "ELEC-002") ++open_count;
    }
    CHECK(floating_count == 1);
    CHECK(open_count == 1);
}

TEST_CASE("Electrical rules run deterministically",
          "[ElectricalRules][Determinism][fast]")
{
    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"R1", "RES", {}});
    auto pin = graph.add_pin(PinNode{"R1.p1", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    // open circuit (RES has no required terminals)

    OpenCircuitRule rule;
    auto v1 = rule.execute(make_ctx(graph));
    auto v2 = rule.execute(make_ctx(graph));
    auto v3 = rule.execute(make_ctx(graph));

    REQUIRE(v1.size() == 1);
    REQUIRE(v2.size() == 1);
    REQUIRE(v3.size() == 1);
    CHECK(v1[0].message == v2[0].message);
    CHECK(v2[0].message == v3[0].message);
}

TEST_CASE("Electrical rules respect rule IDs and metadata",
          "[ElectricalRules][Metadata][fast]")
{
    FloatingNetRule r1;
    OpenCircuitRule r2;
    ShortCircuitRule r3;

    CHECK(r1.id() == "ELEC-001");
    CHECK(r1.category() == "electrical");
    CHECK_FALSE(r1.description().empty());

    CHECK(r2.id() == "ELEC-002");
    CHECK(r2.category() == "electrical");
    CHECK_FALSE(r2.description().empty());

    CHECK(r3.id() == "ELEC-003");
    CHECK(r3.category() == "electrical");
    CHECK_FALSE(r3.description().empty());
}

TEST_CASE("run_category electrical selects only electrical rules",
          "[ElectricalRules][Integration][fast]")
{
    RuleEngine engine;
    engine.register_rule(std::make_unique<FloatingNetRule>());
    engine.register_rule(std::make_unique<OpenCircuitRule>());

    ConnectivityGraph graph;
    auto dev = graph.add_device(DeviceNode{"R1", "RES", {}});
    auto pin = graph.add_pin(PinNode{"R1.p1", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "p1"});
    // open circuit only; floating net check finds no nets at all

    auto violations = engine.run_category("electrical", make_ctx(graph));
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule_id == "ELEC-002");
}
