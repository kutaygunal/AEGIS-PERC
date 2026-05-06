#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace aegis::graph {

using NodeId = std::size_t;
using EdgeId = std::size_t;

class GraphModel {
public:
    GraphModel();
    ~GraphModel();

    GraphModel(const GraphModel&) = delete;
    GraphModel& operator=(const GraphModel&) = delete;
    GraphModel(GraphModel&&) noexcept;
    GraphModel& operator=(GraphModel&&) noexcept;

    NodeId add_node();
    EdgeId add_edge(NodeId from, NodeId to);

    std::size_t node_count() const;
    std::size_t edge_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::graph
