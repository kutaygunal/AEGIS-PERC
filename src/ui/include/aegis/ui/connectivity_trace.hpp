#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QPointF>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aegis::ui {

struct ConnectivityTraceResult {
    std::string requested_name;
    bool resolved = false;
    std::string message;
    std::set<std::string> traced_net_names;
    std::set<std::string> traced_pin_names;
    std::set<std::string> traced_layers;
    std::vector<std::string> related_scene_item_ids;
    std::optional<QPointF> focus_point;
};

class ConnectivityTraceAdapter {
public:
    ConnectivityTraceAdapter() = default;

    void set_graph(const aegis::graph::ConnectivityGraph* graph);
    [[nodiscard]] const aegis::graph::ConnectivityGraph* graph() const noexcept;
    [[nodiscard]] ConnectivityTraceResult trace_by_name(const std::string& stable_name,
                                                        const UiScene& scene) const;

private:
    const aegis::graph::ConnectivityGraph* m_graph = nullptr;
};

} // namespace aegis::ui
