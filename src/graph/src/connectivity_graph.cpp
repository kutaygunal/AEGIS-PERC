#include "aegis/graph/connectivity_graph.hpp"

#include <deque>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace aegis::graph {

// ---------------------------------------------------------------------------
// Internal storage
// ---------------------------------------------------------------------------

struct ConnectivityGraph::Impl {
    struct StoredNode {
        NodeType type;
        NodeData data;
    };

    struct StoredEdge {
        NodeId from;
        NodeId to;
        EdgeData data;
    };

    std::vector<StoredNode> nodes;
    std::vector<StoredEdge> edges;

    // Adjacency: node_id -> list of outgoing/incoming edge IDs
    std::vector<std::vector<EdgeId>> outgoing;
    std::vector<std::vector<EdgeId>> incoming;

    // Type index caches — rebuilt on demand or kept incrementally
    mutable std::vector<std::vector<NodeId>> type_index_cache;
    mutable bool type_index_dirty = true;
};

// ---------------------------------------------------------------------------
// Rule-of-five
// ---------------------------------------------------------------------------

ConnectivityGraph::ConnectivityGraph()
    : m_impl(std::make_unique<Impl>()) {
    // Pre-size adjacency lists for type cache
    m_impl->type_index_cache.resize(3); // Device(0), Net(1), Pin(2)
}

ConnectivityGraph::~ConnectivityGraph() = default;

ConnectivityGraph::ConnectivityGraph(ConnectivityGraph&&) noexcept = default;
ConnectivityGraph& ConnectivityGraph::operator=(ConnectivityGraph&&) noexcept = default;

// ---------------------------------------------------------------------------
// Node insertion
// ---------------------------------------------------------------------------

NodeId ConnectivityGraph::add_device(const DeviceNode& data) {
    NodeId id = m_impl->nodes.size();
    m_impl->nodes.push_back({NodeType::Device, data});
    m_impl->outgoing.emplace_back();
    m_impl->incoming.emplace_back();
    m_impl->type_index_dirty = true;
    return id;
}

NodeId ConnectivityGraph::add_net(const NetNode& data) {
    NodeId id = m_impl->nodes.size();
    m_impl->nodes.push_back({NodeType::Net, data});
    m_impl->outgoing.emplace_back();
    m_impl->incoming.emplace_back();
    m_impl->type_index_dirty = true;
    return id;
}

NodeId ConnectivityGraph::add_pin(const PinNode& data) {
    NodeId id = m_impl->nodes.size();
    m_impl->nodes.push_back({NodeType::Pin, data});
    m_impl->outgoing.emplace_back();
    m_impl->incoming.emplace_back();
    m_impl->type_index_dirty = true;
    return id;
}

// ---------------------------------------------------------------------------
// Edge insertion
// ---------------------------------------------------------------------------

EdgeId ConnectivityGraph::add_edge(NodeId from, NodeId to, const EdgeData& data) {
    if (from >= m_impl->nodes.size() || to >= m_impl->nodes.size()) {
        throw std::invalid_argument(
            "add_edge: from/to node ID out of range");
    }
    EdgeId id = m_impl->edges.size();
    m_impl->edges.push_back({from, to, data});
    m_impl->outgoing[from].push_back(id);
    m_impl->incoming[to].push_back(id);
    return id;
}

// ---------------------------------------------------------------------------
// Basic queries
// ---------------------------------------------------------------------------

bool ConnectivityGraph::empty() const noexcept {
    return m_impl->nodes.empty();
}

std::size_t ConnectivityGraph::node_count() const noexcept {
    return m_impl->nodes.size();
}

std::size_t ConnectivityGraph::edge_count() const noexcept {
    return m_impl->edges.size();
}

NodeType ConnectivityGraph::node_type(NodeId id) const {
    if (id >= m_impl->nodes.size()) {
        throw std::invalid_argument("node_type: invalid NodeId");
    }
    return m_impl->nodes[id].type;
}

const NodeData& ConnectivityGraph::node_data(NodeId id) const {
    if (id >= m_impl->nodes.size()) {
        throw std::invalid_argument("node_data: invalid NodeId");
    }
    return m_impl->nodes[id].data;
}

const EdgeData& ConnectivityGraph::edge_data(EdgeId id) const {
    if (id >= m_impl->edges.size()) {
        throw std::invalid_argument("edge_data: invalid EdgeId");
    }
    return m_impl->edges[id].data;
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

const std::vector<EdgeId>& ConnectivityGraph::outgoing_edges(NodeId id) const {
    if (id >= m_impl->nodes.size()) {
        throw std::invalid_argument("outgoing_edges: invalid NodeId");
    }
    return m_impl->outgoing[id];
}

const std::vector<EdgeId>& ConnectivityGraph::incoming_edges(NodeId id) const {
    if (id >= m_impl->nodes.size()) {
        throw std::invalid_argument("incoming_edges: invalid NodeId");
    }
    return m_impl->incoming[id];
}

NodeId ConnectivityGraph::edge_from(EdgeId id) const {
    if (id >= m_impl->edges.size()) {
        throw std::invalid_argument("edge_from: invalid EdgeId");
    }
    return m_impl->edges[id].from;
}

NodeId ConnectivityGraph::edge_to(EdgeId id) const {
    if (id >= m_impl->edges.size()) {
        throw std::invalid_argument("edge_to: invalid EdgeId");
    }
    return m_impl->edges[id].to;
}

// ---------------------------------------------------------------------------
// Type-indexed queries
// ---------------------------------------------------------------------------

static std::size_t type_to_idx(NodeType t) {
    return static_cast<std::size_t>(t);
}

std::vector<NodeId> ConnectivityGraph::nodes_of_type(NodeType type) const {
    if (m_impl->type_index_dirty) {
        for (auto& bucket : m_impl->type_index_cache) {
            bucket.clear();
        }
        for (NodeId i = 0; i < m_impl->nodes.size(); ++i) {
            m_impl->type_index_cache[type_to_idx(m_impl->nodes[i].type)].push_back(i);
        }
        m_impl->type_index_dirty = false;
    }
    return m_impl->type_index_cache[type_to_idx(type)];
}

std::optional<NodeId> ConnectivityGraph::find_device(const std::string& name) const {
    for (NodeId i = 0; i < m_impl->nodes.size(); ++i) {
        if (m_impl->nodes[i].type != NodeType::Device) continue;
        const auto& dev = std::get<DeviceNode>(m_impl->nodes[i].data);
        if (dev.name == name) return i;
    }
    return std::nullopt;
}

std::optional<NodeId> ConnectivityGraph::find_net(const std::string& name) const {
    for (NodeId i = 0; i < m_impl->nodes.size(); ++i) {
        if (m_impl->nodes[i].type != NodeType::Net) continue;
        const auto& net = std::get<NetNode>(m_impl->nodes[i].data);
        if (net.name == name) return i;
    }
    return std::nullopt;
}

std::optional<NodeId> ConnectivityGraph::find_pin(const std::string& name) const {
    for (NodeId i = 0; i < m_impl->nodes.size(); ++i) {
        if (m_impl->nodes[i].type != NodeType::Pin) continue;
        const auto& pin = std::get<PinNode>(m_impl->nodes[i].data);
        if (pin.name == name) return i;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

void ConnectivityGraph::bfs(NodeId start, INodeVisitor& visitor) const {
    if (start >= m_impl->nodes.size()) return;

    std::vector<bool> seen(m_impl->nodes.size(), false);
    std::deque<NodeId> queue;
    queue.push_back(start);
    seen[start] = true;

    while (!queue.empty()) {
        NodeId cur = queue.front();
        queue.pop_front();

        const auto& node = m_impl->nodes[cur];
        visitor.visit(cur, node.type, node.data);

        for (EdgeId eid : m_impl->outgoing[cur]) {
            NodeId next = m_impl->edges[eid].to;
            if (!seen[next]) {
                seen[next] = true;
                queue.push_back(next);
            }
        }
    }
}

void ConnectivityGraph::dfs(NodeId start, INodeVisitor& visitor) const {
    if (start >= m_impl->nodes.size()) return;

    std::vector<bool> seen(m_impl->nodes.size(), false);

    // Iterative DFS to avoid stack overflow on huge graphs
    struct Frame { NodeId id; bool post; };
    std::vector<Frame> stack;
    stack.reserve(64);
    stack.push_back({start, false});
    seen[start] = true;

    while (!stack.empty()) {
        Frame frame = stack.back();
        stack.pop_back();

        const auto& node = m_impl->nodes[frame.id];

        if (!frame.post) {
            visitor.pre_visit(frame.id, node.type, node.data);
            stack.push_back({frame.id, true}); // will be post-visit later

            for (EdgeId eid : m_impl->outgoing[frame.id]) {
                NodeId next = m_impl->edges[eid].to;
                if (!seen[next]) {
                    seen[next] = true;
                    stack.push_back({next, false});
                }
            }
        } else {
            visitor.post_visit(frame.id, node.type, node.data);
        }
    }
}

// ---------------------------------------------------------------------------
// Topological ordering
// ---------------------------------------------------------------------------

std::vector<NodeId> ConnectivityGraph::topological_order() const {
    const std::size_t N = m_impl->nodes.size();
    if (N == 0) return {};

    std::vector<std::size_t> in_degree(N, 0);
    for (const auto& edge : m_impl->edges) {
        ++in_degree[edge.to];
    }

    std::queue<NodeId> q;
    for (NodeId i = 0; i < N; ++i) {
        if (in_degree[i] == 0) q.push(i);
    }

    std::vector<NodeId> result;
    result.reserve(N);
    while (!q.empty()) {
        NodeId cur = q.front(); q.pop();
        result.push_back(cur);

        for (EdgeId eid : m_impl->outgoing[cur]) {
            NodeId to = m_impl->edges[eid].to;
            if (--in_degree[to] == 0) {
                q.push(to);
            }
        }
    }

    // If not all nodes are processed we have a cycle.
    if (result.size() != N) {
        // Return partial order and let the caller decide.
        // For now we just return what we have.
    }
    return result;
}

// ---------------------------------------------------------------------------
// Batch construction from LayoutIR
// ---------------------------------------------------------------------------

ConnectivityGraph ConnectivityGraph::from_layout_ir(
    const aegis::parsing::LayoutIR& ir,
    std::vector<std::string>& out_unresolved)
{
    ConnectivityGraph graph;
    out_unresolved.clear();

    // --- Phase 1: create Net nodes (index by name) -------------------------
    std::unordered_map<std::string, NodeId> net_by_name;
    for (const auto& net : ir.nets) {
        NodeId nid = graph.add_net({net.name, net.properties});
        net_by_name[net.name] = nid;
    }

    // --- Phase 2: create Device nodes -------------------------------------
    std::unordered_map<std::string, NodeId> device_by_name;
    for (const auto& dev : ir.devices) {
        DeviceNode data;
        data.name      = dev.name;
        data.device_type = dev.type;
        data.properties = dev.properties;
        NodeId nid = graph.add_device(std::move(data));
        device_by_name[dev.name] = nid;
    }

    // --- Phase 3: device pins ------------------------------------------------
    for (const auto& dev : ir.devices) {
        NodeId device_id = device_by_name.at(dev.name);
        for (const auto& [pin_name, net_name] : dev.pins) {
            // Ensure the referenced net exists
            auto net_it = net_by_name.find(net_name);
            if (net_it == net_by_name.end()) {
                out_unresolved.push_back(dev.name + "." + pin_name + " -> " + net_name);
                net_it = net_by_name.emplace(net_name,
                    graph.add_net({net_name, {}})).first;
            }
            NodeId net_id = net_it->second;

            // Create pin node
            PinNode pin_data;
            pin_data.name = dev.name + "." + pin_name;
            pin_data.direction = "BIDIR";
            NodeId pin_id = graph.add_pin(pin_data);

            // Device -> Pin (terminal_name = pin_name)
            graph.add_edge(device_id, pin_id,
                {EdgeType::DeviceToPin, pin_name});

            // Pin -> Net
            graph.add_edge(pin_id, net_id,
                {EdgeType::NetToPin, ""});
        }
    }

    // --- Phase 4: external ports ---------------------------------------------
    for (const auto& port : ir.ports) {
        // Look up or create the net
        auto net_it = net_by_name.find(port.net_name);
        if (net_it == net_by_name.end()) {
            out_unresolved.push_back("PORT " + port.name + " -> " + port.net_name);
            net_it = net_by_name.emplace(port.net_name,
                graph.add_net({port.net_name, {}})).first;
        }
        NodeId net_id = net_it->second;

        PinNode pin_data;
        pin_data.name      = port.name;
        pin_data.direction = port.direction;
        if (port.layer)  pin_data.layer = port.layer.value();
        if (port.location) {
            pin_data.x = port.location->x;
            pin_data.y = port.location->y;
        }
        NodeId pin_id = graph.add_pin(pin_data);

        graph.add_edge(pin_id, net_id, {EdgeType::NetToPin, ""});
    }

    return graph;
}

} // namespace aegis::graph
