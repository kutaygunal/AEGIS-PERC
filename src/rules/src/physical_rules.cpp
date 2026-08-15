#include "aegis/rules/physical_rules.hpp"

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

namespace aegis::rules {

using aegis::graph::DeviceNode;
using aegis::graph::EdgeType;
using aegis::graph::NetNode;
using aegis::graph::NodeId;
using aegis::graph::NodeType;

namespace {

std::optional<double> parse_double(const std::string& value)
{
    try {
        std::size_t pos = 0;
        const double parsed = std::stod(value, &pos);
        if (pos == value.size()) {
            return parsed;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<double> device_property_as_double(const DeviceNode& dev, const std::string& key)
{
    const auto it = dev.properties.find(key);
    if (it == dev.properties.end()) {
        return std::nullopt;
    }
    return parse_double(it->second);
}

std::optional<double> net_property_as_double(const NetNode& net, const std::string& key)
{
    const auto it = net.properties.find(key);
    if (it == net.properties.end()) {
        return std::nullopt;
    }
    return parse_double(it->second);
}

/// Resolves a gate area for a single device: prefer an explicit gate-area
/// property; fall back to width * length. Returns nullopt (not zero) when
/// neither is available/parseable, so callers can distinguish "no data"
/// from "zero area".
std::optional<double> resolve_gate_area(const DeviceNode& dev,
                                         const std::string& gate_area_field,
                                         const std::string& width_field,
                                         const std::string& length_field)
{
    if (const auto direct = device_property_as_double(dev, gate_area_field); direct.has_value()) {
        return direct;
    }
    const auto width = device_property_as_double(dev, width_field);
    const auto length = device_property_as_double(dev, length_field);
    if (!width.has_value() || !length.has_value()) {
        return std::nullopt;
    }
    return *width * *length;
}

std::string double_to_string(double value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

} // namespace

AntennaRatioRule::AntennaRatioRule(RulePackRuleDefinition definition)
    : m_definition(std::move(definition)) {}

std::string AntennaRatioRule::id() const { return m_definition.id; }
std::string AntennaRatioRule::name() const { return "Antenna ratio check"; }
std::string AntennaRatioRule::category() const { return "physical"; }

std::string AntennaRatioRule::description() const
{
    return !m_definition.description.empty()
        ? m_definition.description
        : "Detects nets whose accumulated connected metal area exceeds a "
          "configured multiple of total connected gate area (antenna ratio).";
}

std::vector<Violation> AntennaRatioRule::execute(const RuleContext& ctx) const
{
    std::vector<Violation> violations;

    const auto max_ratio = m_definition.parameters.get<double>("max_ratio");
    if (!max_ratio.has_value()) {
        // validate_pack() should already have rejected packs missing this,
        // but defend against direct construction with an incomplete
        // definition rather than dividing by / comparing against nothing.
        return violations;
    }

    const std::string gate_terminal =
        m_definition.parameters.get<std::string>("gate_terminal").value_or("gate");
    const std::string width_field =
        m_definition.parameters.get<std::string>("gate_width_field").value_or("w");
    const std::string length_field =
        m_definition.parameters.get<std::string>("gate_length_field").value_or("l");
    const std::string gate_area_field =
        m_definition.parameters.get<std::string>("gate_area_field").value_or("gate_area_um2");
    const std::string metal_area_field =
        m_definition.parameters.get<std::string>("metal_area_field").value_or("metal_area_um2");

    const auto& graph = ctx.graph;

    for (NodeId net_id : graph.nodes_of_type(NodeType::Net)) {
        const auto& net = std::get<NetNode>(graph.node_data(net_id));

        double gate_area_sum = 0.0;
        bool has_gate_connection = false;

        for (const auto& net_edge_id : graph.incoming_edges(net_id)) {
            if (graph.edge_data(net_edge_id).type != EdgeType::NetToPin) continue;
            const NodeId pin_id = graph.edge_from(net_edge_id);
            if (graph.node_type(pin_id) != NodeType::Pin) continue;

            for (const auto& pin_edge_id : graph.incoming_edges(pin_id)) {
                const auto& pin_edge = graph.edge_data(pin_edge_id);
                if (pin_edge.type != EdgeType::DeviceToPin) continue;
                if (pin_edge.terminal_name != gate_terminal) continue;

                const NodeId device_id = graph.edge_from(pin_edge_id);
                if (graph.node_type(device_id) != NodeType::Device) continue;
                const auto& dev = std::get<DeviceNode>(graph.node_data(device_id));

                has_gate_connection = true;
                const auto area = resolve_gate_area(dev, gate_area_field, width_field, length_field);
                if (area.has_value()) {
                    gate_area_sum += *area;
                }
            }
        }

        if (!has_gate_connection || gate_area_sum <= 0.0) {
            continue; // no meaningful antenna metric without gate-connected devices
        }

        const auto metal_area = net_property_as_double(net, metal_area_field);
        if (!metal_area.has_value()) {
            continue; // no accumulated metal-area data available; not triggering
        }

        const double ratio = *metal_area / gate_area_sum;
        if (ratio <= *max_ratio) {
            continue;
        }

        Violation v{m_definition.id, m_definition.severity,
                    "Net '" + net.name + "' antenna ratio " + double_to_string(ratio) +
                        " (metal area " + double_to_string(*metal_area) +
                        " / gate area " + double_to_string(gate_area_sum) +
                        ") exceeds configured limit " + double_to_string(*max_ratio) + ".",
                    net.name};
        v.metadata = v.metadata
            .with("ratio", ratio)
            .with("metal_area_um2", *metal_area)
            .with("gate_area_um2", gate_area_sum)
            .with("max_ratio", *max_ratio);
        violations.push_back(std::move(v));
    }

    return violations;
}

} // namespace aegis::rules
