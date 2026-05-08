#include "aegis/graph/current_activity_application.hpp"

namespace aegis::graph {

CurrentActivityApplyResult apply_current_activity(ConnectivityGraph& graph,
                                                  const aegis::parsing::CurrentActivityData& data)
{
    CurrentActivityApplyResult result;
    result.diagnostics = data.diagnostics;

    for (const auto& record : data.records) {
        if (!graph.set_net_property(record.net_name, "current_mA", std::to_string(record.current_milliamps))) {
            result.diagnostics.push_back({
                aegis::parsing::CurrentActivityDiagnostic::Severity::Warning,
                "CURRENT_NET_NOT_FOUND",
                "Current/activity data references unknown net '" + record.net_name + "'",
                record.source_row,
                record.net_name
            });
            continue;
        }

        graph.set_net_property(record.net_name, "voltage_domain", record.voltage_domain);
        graph.set_net_property(record.net_name, "layer", record.layer);
        ++result.applied_count;
    }

    return result;
}

} // namespace aegis::graph
