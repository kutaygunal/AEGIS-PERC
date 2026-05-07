#pragma once

#include "aegis/graph/connectivity_graph.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::graph {

// ---------------------------------------------------------------------------
// Domain / signal tag
// ---------------------------------------------------------------------------

struct DomainTag {
    std::string domain;   // e.g. "VDD", "GND", "CLK"
    std::string signal;   // e.g. "power", "ground", "clock", "data"
};

// ---------------------------------------------------------------------------
// Conflict record
// ---------------------------------------------------------------------------

struct DomainConflict {
    NodeId      node_id;
    std::string existing_domain;
    std::string conflicting_domain;
    std::string message;
};

// ---------------------------------------------------------------------------
// Optional graph analysis pass
// ---------------------------------------------------------------------------

/**
 * Propagates power/signal domain tags through the connectivity graph.
 *
 * Tags seeded on nets or devices spread across Device->Pin->Net edges
 * in both directions.  If a node would receive two different domain
 * tags, a conflict is recorded and the original tag is kept.
 */
class DomainTagger {
public:
    void seed(NodeId node, DomainTag tag);
    void propagate(const ConnectivityGraph& graph);
    void clear();

    [[nodiscard]] std::optional<DomainTag> tag_for(NodeId node) const;
    [[nodiscard]] const std::vector<DomainConflict>& conflicts() const;

private:
    std::map<NodeId, DomainTag> m_tags;
    std::vector<DomainConflict>   m_conflicts;
};

} // namespace aegis::graph
