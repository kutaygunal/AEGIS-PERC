#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/power_intent_application.hpp"
#include "aegis/parsing/power_intent.hpp"

using namespace aegis::graph;
using namespace aegis::parsing;

TEST_CASE("PowerIntentParser imports valid CSV and attaches to graph devices", "[parsing][p6-005][PowerDomainImport]")
{
    const std::string csv =
        "instance,domain,voltage\n"
        "CPU_CORE,CORE_0V8,0.8\n"
        "IO_RING,IO_3V3,3.3\n";

    PowerIntentParser parser;
    const auto data = parser.parse_csv_string(csv);

    REQUIRE(!data.has_errors());
    REQUIRE(data.assignments.size() == 2);
    REQUIRE(data.assignments[0].instance_name == "CPU_CORE");
    REQUIRE(data.assignments[0].domain_name == "CORE_0V8");
    REQUIRE(data.assignments[0].voltage == 0.8);

    ConnectivityGraph graph;
    graph.add_device(DeviceNode{"CPU_CORE", "BLOCK", {}});
    graph.add_device(DeviceNode{"IO_RING", "BLOCK", {}});

    const auto result = apply_power_intent(graph, data);
    REQUIRE(result.applied_count == 2);
    REQUIRE(result.diagnostics.empty());

    const auto cpu = graph.find_device("CPU_CORE");
    REQUIRE(cpu.has_value());
    const auto& cpu_dev = std::get<DeviceNode>(graph.node_data(*cpu));
    REQUIRE(cpu_dev.properties.at("domain") == "CORE_0V8");
    REQUIRE(cpu_dev.properties.at("voltage") == "0.800000");
}

TEST_CASE("PowerIntentParser reports duplicate instances and latest row wins", "[parsing][p6-005][PowerDomainImport]")
{
    const std::string csv =
        "instance,domain,voltage\n"
        "CPU_CORE,CORE_0V8,0.8\n"
        "CPU_CORE,CORE_0V9,0.9\n";

    PowerIntentParser parser;
    const auto data = parser.parse_csv_string(csv);

    REQUIRE(data.assignments.size() == 1);
    REQUIRE(data.assignments[0].domain_name == "CORE_0V9");

    bool saw_duplicate = false;
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.code == "POWER_INSTANCE_DUPLICATE") {
            saw_duplicate = true;
        }
    }
    REQUIRE(saw_duplicate);
}

TEST_CASE("PowerIntentParser rejects missing voltage", "[parsing][p6-005][PowerDomainImport]")
{
    const std::string csv =
        "instance,domain,voltage\n"
        "CPU_CORE,CORE_0V8,\n";

    PowerIntentParser parser;
    const auto data = parser.parse_csv_string(csv);

    REQUIRE(data.has_errors());
    REQUIRE(data.assignments.empty());

    bool saw_missing_voltage = false;
    for (const auto& diagnostic : data.diagnostics) {
        if (diagnostic.code == "POWER_VOLTAGE_MISSING") {
            saw_missing_voltage = true;
        }
    }
    REQUIRE(saw_missing_voltage);
}

TEST_CASE("Power intent application warns on unknown instance references", "[parsing][p6-005][PowerDomainImport]")
{
    const std::string csv =
        "instance,domain,voltage\n"
        "UNKNOWN_BLOCK,CORE_0V8,0.8\n";

    PowerIntentParser parser;
    const auto data = parser.parse_csv_string(csv);

    ConnectivityGraph graph;
    graph.add_device(DeviceNode{"CPU_CORE", "BLOCK", {}});

    const auto result = apply_power_intent(graph, data);
    REQUIRE(result.applied_count == 0);

    bool saw_unknown = false;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == "POWER_INSTANCE_NOT_FOUND") {
            saw_unknown = true;
        }
    }
    REQUIRE(saw_unknown);
}
