#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/current_activity.hpp"

#include <vector>

namespace aegis::graph {

struct CurrentActivityApplyResult {
    std::size_t applied_count = 0;
    std::vector<aegis::parsing::CurrentActivityDiagnostic> diagnostics;
};

CurrentActivityApplyResult apply_current_activity(ConnectivityGraph& graph,
                                                  const aegis::parsing::CurrentActivityData& data);

} // namespace aegis::graph
