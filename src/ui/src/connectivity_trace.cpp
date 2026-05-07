#include "aegis/ui/connectivity_trace.hpp"

#include <queue>
#include <set>

namespace aegis::ui {
namespace {

std::optional<aegis::graph::NodeId> find_pin_by_stable_name(
    const aegis::graph::ConnectivityGraph& graph,
    const std::string& stable_name,
    std::set<std::string>& out_layers,
    std::set<std::string>& out_pin_names,
    std::optional<QPointF>& out_focus)
{
    for (const auto node_id : graph.nodes_of_type(aegis::graph::NodeType::Pin)) {
        const auto& pin = std::get<aegis::graph::PinNode>(graph.node_data(node_id));
        for (const auto edge_id : graph.incoming_edges(node_id)) {
            if (graph.edge_data(edge_id).type != aegis::graph::EdgeType::DeviceToPin) {
                continue;
            }
            const auto& device = std::get<aegis::graph::DeviceNode>(graph.node_data(graph.edge_from(edge_id)));
            const std::string candidate = device.name + "." + graph.edge_data(edge_id).terminal_name;
            if (candidate == stable_name) {
                out_pin_names.insert(pin.name);
                if (pin.layer.has_value()) out_layers.insert(*pin.layer);
                if (pin.x.has_value() && pin.y.has_value()) out_focus = QPointF(*pin.x, *pin.y);
                return node_id;
            }
        }

        if (pin.name == stable_name) {
            out_pin_names.insert(pin.name);
            if (pin.layer.has_value()) out_layers.insert(*pin.layer);
            if (pin.x.has_value() && pin.y.has_value()) out_focus = QPointF(*pin.x, *pin.y);
            return node_id;
        }
    }
    return std::nullopt;
}

void collect_scene_matches(const UiScene& scene,
                           const std::set<std::string>& net_names,
                           const std::set<std::string>& pin_names,
                           const std::set<std::string>& layers,
                           std::vector<std::string>& out_item_ids,
                           std::optional<QPointF>& out_focus)
{
    for (const auto& item : scene.items) {
        const auto net_it = item.source_metadata.find("net_name");
        const auto name_it = item.source_metadata.find("name");
        const bool net_match = net_it != item.source_metadata.end() && net_names.contains(net_it->second);
        const bool pin_match = name_it != item.source_metadata.end() && pin_names.contains(name_it->second);
        const bool layer_match = !item.layer_name.empty() && layers.contains(item.layer_name);
        if (!(net_match || pin_match || layer_match)) {
            continue;
        }

        out_item_ids.push_back(item.id);
        if (!out_focus.has_value() && !item.points.empty()) {
            out_focus = QPointF(item.points.front().x, item.points.front().y);
        } else if (!out_focus.has_value() && item.bounds.valid) {
            out_focus = QPointF((item.bounds.min_x + item.bounds.max_x) * 0.5,
                                (item.bounds.min_y + item.bounds.max_y) * 0.5);
        }
    }
}

} // namespace

void ConnectivityTraceAdapter::set_graph(const aegis::graph::ConnectivityGraph* graph)
{
    m_graph = graph;
}

const aegis::graph::ConnectivityGraph* ConnectivityTraceAdapter::graph() const noexcept
{
    return m_graph;
}

ConnectivityTraceResult ConnectivityTraceAdapter::trace_by_name(const std::string& stable_name,
                                                                const UiScene& scene) const
{
    ConnectivityTraceResult result;
    result.requested_name = stable_name;

    if (m_graph == nullptr || stable_name.empty()) {
        result.message = "No connectivity graph available for trace request";
        return result;
    }

    std::optional<aegis::graph::NodeId> start = m_graph->find_net(stable_name);
    if (start.has_value()) {
        result.traced_net_names.insert(stable_name);
    } else {
        start = find_pin_by_stable_name(*m_graph, stable_name, result.traced_layers,
                                        result.traced_pin_names, result.focus_point);
    }

    if (!start.has_value()) {
        result.message = "Trace target not found: " + stable_name;
        return result;
    }

    std::queue<aegis::graph::NodeId> queue;
    std::set<aegis::graph::NodeId> visited;
    queue.push(*start);
    visited.insert(*start);

    while (!queue.empty()) {
        const auto node_id = queue.front();
        queue.pop();
        const auto type = m_graph->node_type(node_id);
        const auto& data = m_graph->node_data(node_id);

        if (type == aegis::graph::NodeType::Net) {
            result.traced_net_names.insert(std::get<aegis::graph::NetNode>(data).name);
        } else if (type == aegis::graph::NodeType::Pin) {
            const auto& pin = std::get<aegis::graph::PinNode>(data);
            result.traced_pin_names.insert(pin.name);
            if (pin.layer.has_value()) result.traced_layers.insert(*pin.layer);
            if (!result.focus_point.has_value() && pin.x.has_value() && pin.y.has_value()) {
                result.focus_point = QPointF(*pin.x, *pin.y);
            }
        }

        for (const auto edge_id : m_graph->outgoing_edges(node_id)) {
            const auto next = m_graph->edge_to(edge_id);
            if (visited.insert(next).second) {
                queue.push(next);
            }
        }
        for (const auto edge_id : m_graph->incoming_edges(node_id)) {
            const auto next = m_graph->edge_from(edge_id);
            if (visited.insert(next).second) {
                queue.push(next);
            }
        }
    }

    collect_scene_matches(scene, result.traced_net_names, result.traced_pin_names,
                          result.traced_layers, result.related_scene_item_ids, result.focus_point);

    result.resolved = true;
    result.message = "Trace resolved for " + stable_name;
    return result;
}

} // namespace aegis::ui
