#pragma once

#include "aegis/parsing/layout_ir.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aegis::graph {

// ---------------------------------------------------------------------------
// Identifiers — stable, dense, monotonically increasing
// ---------------------------------------------------------------------------

using NodeId = std::size_t;
using EdgeId = std::size_t;

static constexpr NodeId INVALID_NODE = static_cast<NodeId>(-1);
static constexpr EdgeId INVALID_EDGE = static_cast<EdgeId>(-1);

// ---------------------------------------------------------------------------
// Node types and data
// ---------------------------------------------------------------------------

enum class NodeType { Device, Net, Pin };

/**
 * Per-node data for a transistor/resistor/capacitor/etc.
 */
struct DeviceNode {
    std::string                    name;
    std::string                    device_type;   // e.g. "NMOS", "PMOS", "RES"
    std::map<std::string, std::string> properties; // w=0.5um, l=0.18um, ...

    bool operator==(const DeviceNode& o) const noexcept = default;
};

/**
 * Per-node data for an electrical net (wire).
 */
struct NetNode {
    std::string                    name;
    std::map<std::string, std::string> properties; // width, spacing, ...

    bool operator==(const NetNode& o) const noexcept = default;
};

/**
 * Per-node data for a pin (terminal on a device or external IO port).
 */
struct PinNode {
    std::string           name;
    std::string           direction;   // "INPUT", "OUTPUT", "INOUT", "BIDIR"
    std::optional<std::string> layer; // geometry layer reference
    std::optional<double>       x;     // placement x
    std::optional<double>       y;     // placement y

    bool operator==(const PinNode& o) const noexcept = default;
};

using NodeData = std::variant<DeviceNode, NetNode, PinNode>;

// ---------------------------------------------------------------------------
// Edge types and data
// ---------------------------------------------------------------------------

enum class EdgeType { DeviceToPin, NetToPin };

/**
 * Edge data — for device-to-pin edges the terminal_name names the pin
 * (e.g. "gate", "source", "drain").
 */
struct EdgeData {
    EdgeType    type;
    std::string terminal_name;  // "", "gate", "source", "drain", ...

    bool operator==(const EdgeData& o) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Node / Edge view (read-only accessor)
// ---------------------------------------------------------------------------

struct NodeView {
    NodeId   id;
    NodeType type;
    const NodeData& data;
};

struct EdgeView {
    EdgeId   id;
    NodeId   from;
    NodeId   to;
    const EdgeData& data;
};

// ---------------------------------------------------------------------------
// Visitor interface
// ---------------------------------------------------------------------------

class INodeVisitor {
public:
    virtual ~INodeVisitor() = default;

    /// Called exactly once for each node visited.
    virtual void visit(NodeId id, NodeType type, const NodeData& data) = 0;

    /// Optional: invoked when entering a node in DFS (before children).
    virtual void pre_visit(NodeId, NodeType, const NodeData&) {}

    /// Optional: invoked when leaving a node in DFS (after children).
    virtual void post_visit(NodeId, NodeType, const NodeData&) {}
};

// ---------------------------------------------------------------------------
// Connectivity graph
// ---------------------------------------------------------------------------

/**
 * An adjacency-list graph storing devices, nets, and pins as typed nodes.
 *
 * Design guarantees
 *   - Node/Edge IDs are dense indices and never change (no deletion API).
 *   - Adjacency is kept in outgoing and incoming lists for fast traverse.
 *   - Memory footprint is O(|V| + |E|).
 *   - No Qt or ML dependencies.
 */
class ConnectivityGraph {
public:
    ConnectivityGraph();
    ~ConnectivityGraph();

    ConnectivityGraph(const ConnectivityGraph&) = delete;
    ConnectivityGraph& operator=(const ConnectivityGraph&) = delete;

    ConnectivityGraph(ConnectivityGraph&&) noexcept;
    ConnectivityGraph& operator=(ConnectivityGraph&&) noexcept;

    // -----------------------------------------------------------------------
    // Node insertion — monotonic ID allocation
    // -----------------------------------------------------------------------

    NodeId add_device(const DeviceNode& data);
    NodeId add_net(const NetNode& data);
    NodeId add_pin(const PinNode& data);

    // -----------------------------------------------------------------------
    // Edge insertion
    // -----------------------------------------------------------------------

    EdgeId add_edge(NodeId from, NodeId to, const EdgeData& data);

    // -----------------------------------------------------------------------
    // Basic queries
    // -----------------------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t edge_count() const noexcept;

    [[nodiscard]] NodeType node_type(NodeId id) const;
    [[nodiscard]] const NodeData& node_data(NodeId id) const;
    [[nodiscard]] const EdgeData& edge_data(EdgeId id) const;

    // -----------------------------------------------------------------------
    // Connectivity
    // -----------------------------------------------------------------------

    /// Neighbors accessible via outgoing edges (for undirected feel use both dirs).
    [[nodiscard]] const std::vector<EdgeId>& outgoing_edges(NodeId id) const;
    [[nodiscard]] const std::vector<EdgeId>& incoming_edges(NodeId id) const;

    /// Convenience: resolve edge endpoint IDs.
    [[nodiscard]] NodeId edge_from(EdgeId id) const;
    [[nodiscard]] NodeId edge_to(EdgeId id) const;

    /// All node IDs of a given type.
    [[nodiscard]] std::vector<NodeId> nodes_of_type(NodeType type) const;

    /// Find a node by name (linear scan; O(|V|)).
    [[nodiscard]] std::optional<NodeId> find_device(const std::string& name) const;
    [[nodiscard]] std::optional<NodeId> find_net(const std::string& name) const;
    [[nodiscard]] std::optional<NodeId> find_pin(const std::string& name) const;

    // -----------------------------------------------------------------------
    // Property mutation helpers
    // -----------------------------------------------------------------------

    bool set_device_property(const std::string& device_name,
                             const std::string& key,
                             const std::string& value);
    bool set_net_property(const std::string& net_name,
                          const std::string& key,
                          const std::string& value);

    // -----------------------------------------------------------------------
    // Traversal
    // -----------------------------------------------------------------------

    void bfs(NodeId start, INodeVisitor& visitor) const;
    void dfs(NodeId start, INodeVisitor& visitor) const;

    /**
     * Topological walk over the directed acyclic subgraph formed by
     * Device -> Pin -> Net connections.  Useful for parameter propagation
     * and forward analysis.
     */
    std::vector<NodeId> topological_order() const;

    // -----------------------------------------------------------------------
    // Batch construction from intermediate layout representation
    // -----------------------------------------------------------------------

    /**
     * Build a ConnectivityGraph from a LayoutIR document.
     *
     *   - Creates a Net node for each LayoutIR::Net.
     *   - Creates a Device node for each LayoutIR::Device.
     *   - Creates a Pin node for each external LayoutIR::Port.
     *   - Creates internal Pin nodes from Device::pins (pin_name → net_name).
     *   - Edges:
     *       Device -> Pin (terminal_name = pin_name)
     *       Pin    -> Net
     *
     * @return The populated graph and a list of any unresolved net references.
     */
    static ConnectivityGraph from_layout_ir(
        const aegis::parsing::LayoutIR& ir,
        std::vector<std::string>& out_unresolved);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::graph
