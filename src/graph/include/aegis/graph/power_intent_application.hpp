#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/power_intent.hpp"

#include <vector>

namespace aegis::graph {

struct PowerIntentApplyResult {
    std::size_t applied_count = 0;
    std::vector<aegis::parsing::PowerIntentDiagnostic> diagnostics;
};

PowerIntentApplyResult apply_power_intent(ConnectivityGraph& graph,
                                          const aegis::parsing::PowerIntentData& data);

} // namespace aegis::graph
