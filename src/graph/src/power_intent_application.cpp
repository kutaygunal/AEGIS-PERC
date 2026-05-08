#include "aegis/graph/power_intent_application.hpp"

namespace aegis::graph {

PowerIntentApplyResult apply_power_intent(ConnectivityGraph& graph,
                                          const aegis::parsing::PowerIntentData& data)
{
    PowerIntentApplyResult result;
    result.diagnostics = data.diagnostics;

    for (const auto& assignment : data.assignments) {
        if (!graph.set_device_property(assignment.instance_name, "domain", assignment.domain_name)) {
            result.diagnostics.push_back({
                aegis::parsing::PowerIntentDiagnostic::Severity::Warning,
                "POWER_INSTANCE_NOT_FOUND",
                "Power-domain mapping references unknown instance '" + assignment.instance_name + "'",
                assignment.source_row,
                assignment.instance_name
            });
            continue;
        }

        graph.set_device_property(assignment.instance_name, "voltage", std::to_string(assignment.voltage));
        ++result.applied_count;
    }

    return result;
}

} // namespace aegis::graph
