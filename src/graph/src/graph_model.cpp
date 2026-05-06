#include "aegis/graph/graph_model.hpp"

namespace aegis::graph {

struct GraphModel::Impl {
    std::vector<NodeId> nodes;
    struct Edge { NodeId from; NodeId to; };
    std::vector<Edge> edges;
};

GraphModel::GraphModel() : m_impl(std::make_unique<Impl>()) {}
GraphModel::~GraphModel() = default;
GraphModel::GraphModel(GraphModel&&) noexcept = default;
GraphModel& GraphModel::operator=(GraphModel&&) noexcept = default;

NodeId GraphModel::add_node() {
    NodeId id = m_impl->nodes.size();
    m_impl->nodes.push_back(id);
    return id;
}

EdgeId GraphModel::add_edge(NodeId from, NodeId to) {
    EdgeId id = m_impl->edges.size();
    m_impl->edges.push_back({from, to});
    return id;
}

std::size_t GraphModel::node_count() const {
    return m_impl->nodes.size();
}

std::size_t GraphModel::edge_count() const {
    return m_impl->edges.size();
}

} // namespace aegis::graph
