#include <catch2/catch_test_macros.hpp>
#include "aegis/graph/graph_model.hpp"

using namespace aegis::graph;

TEST_CASE("GraphModel constructs and counts nodes and edges", "[graph][p1-010][fast][ModuleBoundary]")
{
    GraphModel model;
    REQUIRE(model.node_count() == 0);
    REQUIRE(model.edge_count() == 0);

    NodeId n0 = model.add_node();
    NodeId n1 = model.add_node();
    REQUIRE(model.node_count() == 2);

    EdgeId e0 = model.add_edge(n0, n1);
    REQUIRE(model.edge_count() == 1);
    REQUIRE(e0 == 0);

    model.add_edge(n1, n0);
    REQUIRE(model.edge_count() == 2);
}
