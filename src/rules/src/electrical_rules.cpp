#include "aegis/rules/electrical_rules.hpp"
#include "aegis/graph/domain_tagging.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace aegis::rules {

using aegis::graph::ConnectivityGraph;
using aegis::graph::EdgeType;
using aegis::graph::NodeType;
using aegis::graph::PinNode;
using aegis::graph::NetNode;
using aegis::graph::DeviceNode;
using aegis::graph::NodeData;
using aegis::graph::DomainTagger;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> required_terminals_for_device(
    const std::string& device_type)
{
    static const std::unordered_set<std::string> FET_TYPES{
        "NMOS", "PMOS", "nmos", "pmos",
        "NFET", "PFET", "nfet", "pfet"};

    static const std::vector<std::string> FET_REQUIRED{
        "gate", "source", "drain"};

    if (FET_TYPES.count(device_type) != 0) {
        return FET_REQUIRED;
    }
    return {};
}

static bool pin_has_net_connection(const ConnectivityGraph& graph,
                                    std::size_t pin_id)
{
    for (const auto& eid : graph.outgoing_edges(pin_id)) {
        if (graph.edge_data(eid).type == EdgeType::NetToPin) {
            return true;
        }
    }
    return false;
}

static bool pin_has_device_connection(const ConnectivityGraph& graph,
                                       std::size_t pin_id)
{
    for (const auto& eid : graph.incoming_edges(pin_id)) {
        if (graph.edge_data(eid).type == EdgeType::DeviceToPin) {
            return true;
        }
    }
    return false;
}

static std::size_t count_driver_pins_on_net(const ConnectivityGraph& graph,
                                             std::size_t net_id)
{
    std::size_t drivers = 0;
    for (const auto& eid : graph.incoming_edges(net_id)) {
        if (graph.edge_data(eid).type != EdgeType::NetToPin) continue;
        std::size_t pin_id = graph.edge_from(eid);
        if (graph.node_type(pin_id) != NodeType::Pin) continue;

        const auto& pin_data = std::get<PinNode>(graph.node_data(pin_id));
        if (pin_data.direction == "OUTPUT" || pin_data.direction == "INOUT" ||
            pin_data.direction == "BIDIR") {
            ++drivers;
        }
    }
    return drivers;
}

// ---------------------------------------------------------------------------
// ELEC-001 — Floating Net Check
// ---------------------------------------------------------------------------

std::string FloatingNetRule::id() const { return "ELEC-001"; }
std::string FloatingNetRule::name() const { return "Floating net check"; }
std::string FloatingNetRule::category() const { return "electrical"; }
std::string FloatingNetRule::description() const {
    return "Finds nets with no connected device pins or external ports.";
}

std::vector<Violation> FloatingNetRule::execute(
    const RuleContext& ctx) const
{
    std::vector<Violation> violations;
    const auto& graph = ctx.graph;

    for (std::size_t net_id : graph.nodes_of_type(NodeType::Net)) {
        // A net is floating if it has zero NetToPin incoming edges.
        bool has_pin = false;
        for (const auto& eid : graph.incoming_edges(net_id)) {
            if (graph.edge_data(eid).type == EdgeType::NetToPin) {
                has_pin = true;
                break;
            }
        }
        if (!has_pin) {
            const auto& net_data = std::get<NetNode>(graph.node_data(net_id));
            violations.push_back(Violation{
                id(),
                Severity::Error,
                "Net '" + net_data.name + "' has no connected pins; "
                "it is electrically floating.",
                net_data.name});
        }
    }
    return violations;
}

// ---------------------------------------------------------------------------
// ELEC-002 — Open-Circuit Check
// ---------------------------------------------------------------------------

std::string OpenCircuitRule::id() const { return "ELEC-002"; }
std::string OpenCircuitRule::name() const { return "Open-circuit check"; }
std::string OpenCircuitRule::category() const { return "electrical"; }
std::string OpenCircuitRule::description() const {
    return "Finds device pins not connected to any net, and devices "
           "missing required terminals.";
}

std::vector<Violation> OpenCircuitRule::execute(
    const RuleContext& ctx) const
{
    std::vector<Violation> violations;
    const auto& graph = ctx.graph;

    // --- 1. Individual pins attached to a device but not to a net ---
    for (std::size_t pin_id : graph.nodes_of_type(NodeType::Pin)) {
        if (pin_has_device_connection(graph, pin_id) &&
            !pin_has_net_connection(graph, pin_id)) {
            const auto& pin_data = std::get<PinNode>(graph.node_data(pin_id));
            violations.push_back(Violation{
                id(),
                Severity::Error,
                "Pin '" + pin_data.name + "' is attached to a device "
                "but has no net connection (open circuit).",
                pin_data.name});
        }
    }

    // --- 2. Devices missing required terminals ---
    for (std::size_t dev_id : graph.nodes_of_type(NodeType::Device)) {
        const auto& dev_data =
            std::get<DeviceNode>(graph.node_data(dev_id));

        const auto required =
            required_terminals_for_device(dev_data.device_type);
        if (required.empty()) continue;

        std::unordered_set<std::string> present;
        for (const auto& eid : graph.outgoing_edges(dev_id)) {
            const auto& ed = graph.edge_data(eid);
            if (ed.type == EdgeType::DeviceToPin) {
                present.insert(ed.terminal_name);
            }
        }

        for (const auto& term : required) {
            if (present.count(term) == 0) {
                violations.push_back(Violation{
                    id(),
                    Severity::Error,
                    "Device '" + dev_data.name + "' (" +
                    dev_data.device_type +
                    ") is missing required terminal '" +
                    term + "' (open circuit).",
                    dev_data.name});
            }
        }
    }

    return violations;
}

// ---------------------------------------------------------------------------
// ELEC-003 — Short-Circuit / Driver Contention Check
// ---------------------------------------------------------------------------

std::string ShortCircuitRule::id() const { return "ELEC-003"; }
std::string ShortCircuitRule::name() const { return "Short-circuit check"; }
std::string ShortCircuitRule::category() const { return "electrical"; }
std::string ShortCircuitRule::description() const {
    return "Finds nets driven by more than one output-capable pin, "
           "indicating a potential short or contention.";
}

std::vector<Violation> ShortCircuitRule::execute(
    const RuleContext& ctx) const
{
    std::vector<Violation> violations;
    const auto& graph = ctx.graph;

    for (std::size_t net_id : graph.nodes_of_type(NodeType::Net)) {
        std::size_t drivers = count_driver_pins_on_net(graph, net_id);
        if (drivers > 1) {
            const auto& net_data =
                std::get<NetNode>(graph.node_data(net_id));
            violations.push_back(Violation{
                id(),
                Severity::Error,
                "Net '" + net_data.name + "' is driven by " +
                std::to_string(drivers) +
                " output pins (short-circuit / driver contention).",
                net_data.name});
        }
    }
    return violations;
}

// ---------------------------------------------------------------------------
// DOMAIN-001 — Power/Signal Domain Tagging Check
// ---------------------------------------------------------------------------

std::string DomainTaggingRule::id() const { return "DOMAIN-001"; }
std::string DomainTaggingRule::name() const { return "Domain tagging check"; }
std::string DomainTaggingRule::category() const { return "domain"; }
std::string DomainTaggingRule::description() const {
    return "Propagates power/signal domain tags and detects conflicts.";
}

std::vector<Violation> DomainTaggingRule::execute(
    const RuleContext& ctx) const
{
    std::vector<Violation> violations;
    const auto& graph = ctx.graph;

    DomainTagger tagger;

    // Seed tags from net properties
    for (std::size_t net_id : graph.nodes_of_type(NodeType::Net)) {
        const auto& net = std::get<NetNode>(graph.node_data(net_id));
        auto it = net.properties.find("domain");
        if (it != net.properties.end()) {
            std::string signal = "unknown";
            auto sig_it = net.properties.find("signal_type");
            if (sig_it != net.properties.end()) signal = sig_it->second;
            tagger.seed(net_id, {it->second, signal});
        }
    }

    // Seed tags from device properties
    for (std::size_t dev_id : graph.nodes_of_type(NodeType::Device)) {
        const auto& dev = std::get<DeviceNode>(graph.node_data(dev_id));
        auto it = dev.properties.find("domain");
        if (it != dev.properties.end()) {
            std::string signal = "unknown";
            auto sig_it = dev.properties.find("signal_type");
            if (sig_it != dev.properties.end()) signal = sig_it->second;
            tagger.seed(dev_id, {it->second, signal});
        }
    }

    tagger.propagate(graph);

    // Convert conflicts to violations
    for (const auto& conflict : tagger.conflicts()) {
        std::string node_name = std::to_string(conflict.node_id);
        std::visit(
            [&](const auto& node) { node_name = node.name; },
            graph.node_data(conflict.node_id));

        violations.push_back(Violation{
            id(),
            Severity::Error,
            "Domain conflict on '" + node_name + "': existing '" +
                conflict.existing_domain + "' vs conflicting '" +
                conflict.conflicting_domain + "'. " + conflict.message,
            node_name});
    }

    return violations;
}

} // namespace aegis::rules
