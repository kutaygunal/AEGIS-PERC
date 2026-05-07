#include "aegis/graph/domain_tagging.hpp"

#include <queue>

namespace aegis::graph {

void DomainTagger::seed(NodeId node, DomainTag tag) {
    auto it = m_tags.find(node);
    if (it == m_tags.end()) {
        m_tags.emplace(node, std::move(tag));
    } else if (it->second.domain != tag.domain || it->second.signal != tag.signal) {
        m_conflicts.push_back(DomainConflict{
            node,
            it->second.domain,
            tag.domain,
            "Node " + std::to_string(node) + " already tagged with domain '" +
                it->second.domain + "' but conflicting domain '" + tag.domain +
                "' was seeded."
        });
    }
}

void DomainTagger::propagate(const ConnectivityGraph& graph) {
    if (m_tags.empty()) return;

    std::queue<NodeId> q;
    for (const auto& [id, _] : m_tags) {
        q.push(id);
    }

    while (!q.empty()) {
        NodeId current = q.front(); q.pop();
        const auto& current_tag = m_tags.at(current);

        auto visit_neighbor = [&](NodeId neighbor) {
            auto it = m_tags.find(neighbor);
            if (it == m_tags.end()) {
                m_tags.emplace(neighbor, current_tag);
                q.push(neighbor);
            } else if (it->second.domain != current_tag.domain ||
                       it->second.signal != current_tag.signal) {
                m_conflicts.push_back(DomainConflict{
                    neighbor,
                    it->second.domain,
                    current_tag.domain,
                    "Node " + std::to_string(neighbor) + " tagged with domain '" +
                        it->second.domain + "' conflicts with propagated domain '" +
                        current_tag.domain + "'."
                });
            }
        };

        for (EdgeId eid : graph.outgoing_edges(current)) {
            visit_neighbor(graph.edge_to(eid));
        }
        for (EdgeId eid : graph.incoming_edges(current)) {
            visit_neighbor(graph.edge_from(eid));
        }
    }
}

void DomainTagger::clear() {
    m_tags.clear();
    m_conflicts.clear();
}

std::optional<DomainTag> DomainTagger::tag_for(NodeId node) const {
    auto it = m_tags.find(node);
    if (it != m_tags.end()) return it->second;
    return std::nullopt;
}

const std::vector<DomainConflict>& DomainTagger::conflicts() const {
    return m_conflicts;
}

} // namespace aegis::graph
