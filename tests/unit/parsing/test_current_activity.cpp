#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/current_activity_application.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

using namespace aegis::graph;
using namespace aegis::parsing;
using namespace aegis::rules;

namespace {

RuleContext make_ctx(const ConnectivityGraph& graph)
{
    return RuleContext{graph, PropertyMap{}, "FalconCPU"};
}

} // namespace

TEST_CASE("CurrentActivityParser imports valid CSV and applies to graph nets", "[parsing][p6-006][CurrentImport]")
{
    const std::string csv =
        "net_name,current_mA,voltage_domain,layer\n"
        "VDD_CPU,52.4,CORE_0V8,M4\n"
        "IO_3V3,12.1,IO_3V3,M3\n";

    CurrentActivityParser parser;
    const auto data = parser.parse_csv_string(csv);

    REQUIRE(!data.has_errors());
    REQUIRE(data.records.size() == 2);
    REQUIRE(data.records[0].net_name == "VDD_CPU");
    REQUIRE(data.records[0].current_milliamps == 52.4);

    ConnectivityGraph graph;
    graph.add_net(NetNode{"VDD_CPU", {}});
    graph.add_net(NetNode{"IO_3V3", {}});

    const auto result = apply_current_activity(graph, data);
    REQUIRE(result.applied_count == 2);
    REQUIRE(result.diagnostics.empty());

    const auto vdd = graph.find_net("VDD_CPU");
    REQUIRE(vdd.has_value());
    const auto& vdd_net = std::get<NetNode>(graph.node_data(*vdd));
    REQUIRE(vdd_net.properties.at("current_mA") == "52.400000");
    REQUIRE(vdd_net.properties.at("voltage_domain") == "CORE_0V8");
    REQUIRE(vdd_net.properties.at("layer") == "M4");
}

TEST_CASE("CurrentActivityParser rejects malformed units and reports duplicate conflicts", "[parsing][p6-006][CurrentImport]")
{
    const std::string bad_units =
        "net_name,current_mA,voltage_domain,layer\n"
        "VDD_CPU,52.4mV,CORE_0V8,M4\n";

    CurrentActivityParser parser;
    const auto bad = parser.parse_csv_string(bad_units);
    REQUIRE(bad.has_errors());

    bool saw_invalid = false;
    for (const auto& diagnostic : bad.diagnostics) {
        if (diagnostic.code == "CURRENT_VALUE_INVALID") {
            saw_invalid = true;
        }
    }
    REQUIRE(saw_invalid);

    const std::string duplicates =
        "net_name,current_mA,voltage_domain,layer\n"
        "VDD_CPU,52.4,CORE_0V8,M4\n"
        "VDD_CPU,60.0,CORE_0V8,M5\n";
    const auto dup = parser.parse_csv_string(duplicates);
    REQUIRE(dup.records.size() == 1);
    REQUIRE(dup.records[0].current_milliamps == 60.0);
    REQUIRE(dup.records[0].layer == "M5");

    bool saw_conflict = false;
    for (const auto& diagnostic : dup.diagnostics) {
        if (diagnostic.code == "CURRENT_NET_CONFLICT") {
            saw_conflict = true;
        }
    }
    REQUIRE(saw_conflict);
}

TEST_CASE("Current activity application warns on unknown net references", "[parsing][p6-006][CurrentImport]")
{
    const std::string csv =
        "net_name,current_mA,voltage_domain,layer\n"
        "UNKNOWN_NET,52.4,CORE_0V8,M4\n";

    CurrentActivityParser parser;
    const auto data = parser.parse_csv_string(csv);

    ConnectivityGraph graph;
    graph.add_net(NetNode{"VDD_CPU", {}});

    const auto result = apply_current_activity(graph, data);
    REQUIRE(result.applied_count == 0);

    bool saw_unknown = false;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == "CURRENT_NET_NOT_FOUND") {
            saw_unknown = true;
        }
    }
    REQUIRE(saw_unknown);
}

TEST_CASE("Current activity data drives threshold-based EM rule violations", "[parsing][p6-006][CurrentImport]")
{
    const std::string csv =
        "net_name,current_mA,voltage_domain,layer\n"
        "VDD_CPU,52.4,CORE_0V8,M4\n";

    CurrentActivityParser parser;
    const auto data = parser.parse_csv_string(csv);

    ConnectivityGraph graph;
    const auto net = graph.add_net(NetNode{"VDD_CPU", {}});
    const auto pin = graph.add_pin(PinNode{"port_VDD_CPU", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});
    REQUIRE(apply_current_activity(graph, data).applied_count == 1);

    RulePackLoader loader;
    const auto pack = loader.load_from_string(R"(rules:
  - id: EM_CURRENT_LIMIT
    type: em_current_limit
    severity: medium
    parameters:
      M4_max_mA: 40
)");

    RuleEngine engine;
    for (auto& rule : loader.instantiate_rules(pack)) {
        engine.register_rule(std::move(rule));
    }

    const auto violations = engine.run_all(make_ctx(graph));
    REQUIRE(violations.size() == 1);
    REQUIRE(violations[0].rule_id == "EM_CURRENT_LIMIT");
    REQUIRE(violations[0].severity == Severity::Warning);
    REQUIRE(violations[0].message.find("52.4") != std::string::npos);
}
