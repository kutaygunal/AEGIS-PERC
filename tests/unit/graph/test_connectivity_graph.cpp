#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/parser_interface.hpp"

using namespace aegis::graph;
using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static DeviceNode make_dev(const std::string& name, const std::string& type) {
    return DeviceNode{name, type, {}};
}

static NetNode make_net(const std::string& name) {
    return NetNode{name, {}};
}

static PinNode make_pin(const std::string& name, const std::string& dir) {
    PinNode p; p.name = name; p.direction = dir;
    return p;
}

// ---------------------------------------------------------------------------
// Recording visitor
// ---------------------------------------------------------------------------

struct RecordingVisitor : public INodeVisitor {
    std::vector<NodeId> visit_order;
    std::vector<NodeId> pre_order;
    std::vector<NodeId> post_order;

    void visit(NodeId id, NodeType, const NodeData&) override {
        visit_order.push_back(id);
    }
    void pre_visit(NodeId id, NodeType, const NodeData&) override {
        pre_order.push_back(id);
    }
    void post_visit(NodeId id, NodeType, const NodeData&) override {
        post_order.push_back(id);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("ConnectivityGraph constructs empty",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    REQUIRE(graph.empty());
    REQUIRE(graph.node_count() == 0);
    REQUIRE(graph.edge_count() == 0);
}

TEST_CASE("ConnectivityGraph adds typed nodes",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;

    NodeId d0 = graph.add_device(make_dev("M1", "NMOS"));
    NodeId n0 = graph.add_net(make_net("nA"));
    NodeId p0 = graph.add_pin(make_pin("pgate", "INPUT"));

    REQUIRE(graph.node_count() == 3);
    REQUIRE(!graph.empty());

    REQUIRE(graph.node_type(d0) == NodeType::Device);
    REQUIRE(graph.node_type(n0) == NodeType::Net);
    REQUIRE(graph.node_type(p0) == NodeType::Pin);

    REQUIRE(graph.node_data(d0).index() == 0); // DeviceNode variant
    REQUIRE(graph.node_data(n0).index() == 1); // NetNode variant
    REQUIRE(graph.node_data(p0).index() == 2); // PinNode variant
}

TEST_CASE("ConnectivityGraph stores node data correctly",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;

    DeviceNode d{"M3", "PMOS", {{"w", "1.0um"}}};
    NetNode     n{"nclk", {{"width", "0.7"}}};
    PinNode     p{"pgate", "INPUT", "M1", 2.0, 3.0};

    NodeId d_id = graph.add_device(d);
    NodeId n_id = graph.add_net(n);
    NodeId p_id = graph.add_pin(p);

    const auto& dev = std::get<DeviceNode>(graph.node_data(d_id));
    REQUIRE(dev.name == "M3");
    REQUIRE(dev.device_type == "PMOS");
    REQUIRE(dev.properties.at("w") == "1.0um");

    const auto& net = std::get<NetNode>(graph.node_data(n_id));
    REQUIRE(net.name == "nclk");
    REQUIRE(net.properties.at("width") == "0.7");

    const auto& pin = std::get<PinNode>(graph.node_data(p_id));
    REQUIRE(pin.name == "pgate");
    REQUIRE(pin.direction == "INPUT");
    REQUIRE(pin.layer == "M1");
    REQUIRE(pin.x == 2.0);
    REQUIRE(pin.y == 3.0);
}

TEST_CASE("ConnectivityGraph adds and tracks edges",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;

    NodeId d0 = graph.add_device(make_dev("M1", "NMOS"));
    NodeId p0 = graph.add_pin(make_pin("gate", "BIDIR"));
    NodeId n0 = graph.add_net(make_net("nA"));

    EdgeId e0 = graph.add_edge(d0, p0, {EdgeType::DeviceToPin, "gate"});
    EdgeId e1 = graph.add_edge(p0, n0, {EdgeType::NetToPin, ""});

    REQUIRE(graph.edge_count() == 2);
    REQUIRE(e0 == 0);
    REQUIRE(e1 == 1);

    REQUIRE(graph.edge_from(e0) == d0);
    REQUIRE(graph.edge_to(e0) == p0);
    REQUIRE(graph.edge_data(e0).type == EdgeType::DeviceToPin);
    REQUIRE(graph.edge_data(e0).terminal_name == "gate");

    REQUIRE(graph.edge_from(e1) == p0);
    REQUIRE(graph.edge_to(e1) == n0);
    REQUIRE(graph.edge_data(e1).type == EdgeType::NetToPin);
}

TEST_CASE("ConnectivityGraph adjacency lists are consistent",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;

    NodeId a = graph.add_device(make_dev("A", "NMOS"));
    NodeId b = graph.add_net(make_net("B"));
    NodeId c = graph.add_pin(make_pin("C", "OUTPUT"));

    graph.add_edge(a, b, {EdgeType::DeviceToPin, ""});
    graph.add_edge(b, c, {EdgeType::NetToPin, ""});
    graph.add_edge(a, c, {EdgeType::DeviceToPin, "drain"});

    REQUIRE(graph.outgoing_edges(a).size() == 2);
    REQUIRE(graph.incoming_edges(a).size() == 0);

    REQUIRE(graph.outgoing_edges(b).size() == 1);
    REQUIRE(graph.incoming_edges(b).size() == 1);

    REQUIRE(graph.outgoing_edges(c).size() == 0);
    REQUIRE(graph.incoming_edges(c).size() == 2);
}

TEST_CASE("ConnectivityGraph nodes_of_type returns correct subset",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;

    for (int i = 0; i < 3; ++i)
        graph.add_device(make_dev("D" + std::to_string(i), "NMOS"));
    for (int i = 0; i < 5; ++i)
        graph.add_net(make_net("N" + std::to_string(i)));
    for (int i = 0; i < 2; ++i)
        graph.add_pin(make_pin("P" + std::to_string(i), "INOUT"));

    auto devs = graph.nodes_of_type(NodeType::Device);
    auto nets = graph.nodes_of_type(NodeType::Net);
    auto pins = graph.nodes_of_type(NodeType::Pin);

    REQUIRE(devs.size() == 3);
    REQUIRE(nets.size() == 5);
    REQUIRE(pins.size() == 2);

    // Check stable IDs (insertion order)
    REQUIRE(devs[0] == 0);
    REQUIRE(nets[0] == 3);
    REQUIRE(pins[0] == 8);
}

TEST_CASE("ConnectivityGraph find_by_name works",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    graph.add_device(make_dev("M1", "NMOS"));
    graph.add_net(make_net("nA"));
    graph.add_pin(make_pin("A", "INPUT"));

    REQUIRE(graph.find_device("M1").has_value());
    REQUIRE(graph.find_device("M1").value() == 0);
    REQUIRE(!graph.find_device("nonexistent").has_value());

    REQUIRE(graph.find_net("nA").has_value());
    REQUIRE(graph.find_net("nA").value() == 1);

    REQUIRE(graph.find_pin("A").has_value());
    REQUIRE(graph.find_pin("A").value() == 2);
}

TEST_CASE("ConnectivityGraph BFS visits reachable nodes",
          "[graph][GraphModel][fast]")
{
    // A -> B -> C
    // A -> D
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("A", "NMOS"));
    NodeId b = graph.add_net(make_net("B"));
    NodeId c = graph.add_pin(make_pin("C", "OUTPUT"));
    NodeId d = graph.add_net(make_net("D"));

    graph.add_edge(a, b, {EdgeType::DeviceToPin, ""});
    graph.add_edge(b, c, {EdgeType::NetToPin, ""});
    graph.add_edge(a, d, {EdgeType::DeviceToPin, ""});

    RecordingVisitor vis;
    graph.bfs(a, vis);

    // BFS from A visits A, then B+D, then C
    REQUIRE(vis.visit_order.size() == 4);
    REQUIRE(vis.visit_order[0] == a);
    // Second layer: B and D (order depends on insertion order of edges)
    REQUIRE(vis.visit_order[1] == b);
    REQUIRE(vis.visit_order[2] == d);
    REQUIRE(vis.visit_order[3] == c);
}

TEST_CASE("ConnectivityGraph DFS with pre/post visit",
          "[graph][GraphModel][fast]")
{
    // A -> B -> C
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("A", "NMOS"));
    NodeId b = graph.add_net(make_net("B"));
    NodeId c = graph.add_pin(make_pin("C", "OUTPUT"));

    graph.add_edge(a, b, {EdgeType::DeviceToPin, ""});
    graph.add_edge(b, c, {EdgeType::NetToPin, ""});

    RecordingVisitor vis;
    graph.dfs(a, vis);

    REQUIRE(vis.pre_order.size() == 3);
    REQUIRE(vis.post_order.size() == 3);
    REQUIRE(vis.pre_order[0] == a);
    REQUIRE(vis.pre_order[1] == b);
    REQUIRE(vis.pre_order[2] == c);

    // Post-order must be reverse of pre-order for a simple chain
    REQUIRE(vis.post_order[0] == c);
    REQUIRE(vis.post_order[1] == b);
    REQUIRE(vis.post_order[2] == a);
}

TEST_CASE("ConnectivityGraph topological_order is correct for DAG",
          "[graph][GraphModel][fast]")
{
    // D1 -> P1 -> N1
    // D2 -> P2 -> N1
    ConnectivityGraph graph;
    NodeId d1 = graph.add_device(make_dev("D1", "NMOS"));
    NodeId p1 = graph.add_pin(make_pin("P1", "BIDIR"));
    NodeId d2 = graph.add_device(make_dev("D2", "NMOS"));
    NodeId p2 = graph.add_pin(make_pin("P2", "BIDIR"));
    NodeId n1 = graph.add_net(make_net("N1"));

    graph.add_edge(d1, p1, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(p1, n1, {EdgeType::NetToPin, ""});
    graph.add_edge(d2, p2, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(p2, n1, {EdgeType::NetToPin, ""});

    auto order = graph.topological_order();
    REQUIRE(order.size() == 5);

    // D1 and D2 must come before N1
    auto idx_d1 = static_cast<std::size_t>(
        std::find(order.begin(), order.end(), d1) - order.begin());
    auto idx_d2 = static_cast<std::size_t>(
        std::find(order.begin(), order.end(), d2) - order.begin());
    auto idx_n1 = static_cast<std::size_t>(
        std::find(order.begin(), order.end(), n1) - order.begin());

    REQUIRE(idx_d1 < idx_n1);
    REQUIRE(idx_d2 < idx_n1);
}

TEST_CASE("ConnectivityGraph from_layout_ir builds NAND2 graph",
          "[graph][GraphModel][fast]")
{
    LayoutIR ir;
    ir.design_name = "nand2";
    ir.nets.push_back(Net{"nA", {"A"}, {}});
    ir.nets.push_back(Net{"nB", {"B"}, {}});
    ir.nets.push_back(Net{"nY", {"Y"}, {}});
    ir.nets.push_back(Net{"GND", {}, {}});
    ir.nets.push_back(Net{"VDD", {}, {}});
    ir.nets.push_back(Net{"n_int", {}, {}});

    Device m1{"M1", "NMOS",
              {{"gate", "nA"}, {"source", "GND"}, {"drain", "n_int"}},
              {{"w", "0.5um"}}};
    Device m2{"M2", "NMOS",
              {{"gate", "nB"}, {"source", "n_int"}, {"drain", "nY"}},
              {{"w", "0.5um"}}};
    Device m3{"M3", "PMOS",
              {{"gate", "nA"}, {"source", "nY"}, {"drain", "VDD"}},
              {{"w", "1.0um"}}};
    Device m4{"M4", "PMOS",
              {{"gate", "nB"}, {"source", "nY"}, {"drain", "VDD"}},
              {{"w", "1.0um"}}};
    ir.devices.push_back(m1);
    ir.devices.push_back(m2);
    ir.devices.push_back(m3);
    ir.devices.push_back(m4);

    ir.ports.push_back(Port{"A",  "INPUT",  "nA",  std::string{"M1"}, Point{2.0, 2.0}});
    ir.ports.push_back(Port{"B",  "INPUT",  "nB",  std::string{"M1"}, Point{5.0, 2.0}});
    ir.ports.push_back(Port{"Y",  "OUTPUT", "nY",  std::string{"M1"}, Point{8.0, 2.0}});

    std::vector<std::string> unresolved;
    auto graph = ConnectivityGraph::from_layout_ir(ir, unresolved);

    REQUIRE(unresolved.empty());

    // 6 nets + 4 devices + 3 ports + (4 devices * 3 pins each = 12 pins) = 25 nodes
    REQUIRE(graph.node_count() == 25);

    // Edges: 12 device-to-pin + 12 pin-to-net + 3 port-to-net = 27
    REQUIRE(graph.edge_count() == 27);

    // Verify device M1 exists
    auto m1_id = graph.find_device("M1");
    REQUIRE(m1_id.has_value());
    REQUIRE(graph.node_type(m1_id.value()) == NodeType::Device);
    const auto& dev = std::get<DeviceNode>(graph.node_data(m1_id.value()));
    REQUIRE(dev.device_type == "NMOS");
    REQUIRE(dev.properties.at("w") == "0.5um");

    // M1 should have 3 outgoing edges (to its pins)
    REQUIRE(graph.outgoing_edges(m1_id.value()).size() == 3);

    // Find net nA
    auto nA = graph.find_net("nA");
    REQUIRE(nA.has_value());

    // Port A should exist as a pin
    auto portA = graph.find_pin("A");
    REQUIRE(portA.has_value());
    const auto& pinA = std::get<PinNode>(graph.node_data(portA.value()));
    REQUIRE(pinA.direction == "INPUT");

    // Type counts
    REQUIRE(graph.nodes_of_type(NodeType::Device).size() == 4);
    REQUIRE(graph.nodes_of_type(NodeType::Net).size() == 6);
    REQUIRE(graph.nodes_of_type(NodeType::Pin).size() == 15); // 12 internal + 3 ports
}

TEST_CASE("ConnectivityGraph from_layout_ir handles unresolved nets",
          "[graph][GraphModel][fast]")
{
    LayoutIR ir;
    ir.design_name = "test";
    // No nets defined, but device references one
    ir.devices.push_back(Device{"M1", "NMOS",
        {{"gate", "missing_net"}}, {}});

    std::vector<std::string> unresolved;
    auto graph = ConnectivityGraph::from_layout_ir(ir, unresolved);

    REQUIRE(!unresolved.empty());
    REQUIRE(unresolved[0].find("missing_net") != std::string::npos);
    REQUIRE(graph.node_count() == 3); // device + unresolved net + pin
}

TEST_CASE("ConnectivityGraph from_layout_ir correct edge types",
          "[graph][GraphModel][fast]")
{
    LayoutIR ir;
    ir.design_name = "simple";
    ir.nets.push_back(Net{"n1", {}, {}});
    ir.devices.push_back(Device{"D1", "NMOS",
        {{"gate", "n1"}}, {}});

    std::vector<std::string> unresolved;
    auto graph = ConnectivityGraph::from_layout_ir(ir, unresolved);

    REQUIRE(unresolved.empty());

    NodeId dev = graph.find_device("D1").value();
    auto out = graph.outgoing_edges(dev);
    REQUIRE(out.size() == 1);
    REQUIRE(graph.edge_data(out[0]).type == EdgeType::DeviceToPin);
    REQUIRE(graph.edge_data(out[0]).terminal_name == "gate");

    NodeId pin = graph.edge_to(out[0]);
    auto pin_out = graph.outgoing_edges(pin);
    REQUIRE(pin_out.size() == 1);
    REQUIRE(graph.edge_data(pin_out[0]).type == EdgeType::NetToPin);
    REQUIRE(graph.edge_data(pin_out[0]).terminal_name == "");
}

TEST_CASE("ConnectivityGraph visitor visit method is called",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    NodeId d = graph.add_device(make_dev("D1", "NMOS"));
    NodeId p = graph.add_pin(make_pin("pgate", "INPUT"));
    NodeId n = graph.add_net(make_net("n1"));
    graph.add_edge(d, p, {EdgeType::DeviceToPin, "gate"});
    graph.add_edge(p, n, {EdgeType::NetToPin, ""});

    RecordingVisitor vis;
    graph.bfs(d, vis);

    REQUIRE(vis.visit_order.size() == 3);
    REQUIRE(vis.visit_order[0] == d);
    REQUIRE(vis.visit_order[1] == p);
    REQUIRE(vis.visit_order[2] == n);
}

TEST_CASE("ConnectivityGraph invalid node access throws",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    REQUIRE_THROWS(graph.node_type(99));
    REQUIRE_THROWS(graph.node_data(99));
    REQUIRE_THROWS(graph.outgoing_edges(99));
    REQUIRE_THROWS(graph.incoming_edges(99));
}

TEST_CASE("ConnectivityGraph invalid edge access throws",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    REQUIRE_THROWS(graph.edge_data(99));
    REQUIRE_THROWS(graph.edge_from(99));
    REQUIRE_THROWS(graph.edge_to(99));
}

TEST_CASE("ConnectivityGraph invalid add_edge throws",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("A", "NMOS"));
    REQUIRE_THROWS(graph.add_edge(a, 99, {EdgeType::DeviceToPin, ""}));
    REQUIRE_THROWS(graph.add_edge(99, a, {EdgeType::DeviceToPin, ""}));
}

TEST_CASE("ConnectivityGraph BFS from isolated node visits only itself",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("A", "NMOS"));
    [[maybe_unused]] NodeId b = graph.add_net(make_net("B"));

    RecordingVisitor vis;
    graph.bfs(a, vis);

    REQUIRE(vis.visit_order.size() == 1);
    REQUIRE(vis.visit_order[0] == a);
}

TEST_CASE("ConnectivityGraph DFS from isolated node visits only itself",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("A", "NMOS"));

    RecordingVisitor vis;
    graph.dfs(a, vis);

    REQUIRE(vis.pre_order.size() == 1);
    REQUIRE(vis.pre_order[0] == a);
    REQUIRE(vis.post_order.size() == 1);
    REQUIRE(vis.post_order[0] == a);
}

TEST_CASE("ConnectivityGraph move semantics preserve data",
          "[graph][GraphModel][fast]")
{
    ConnectivityGraph graph;
    NodeId a = graph.add_device(make_dev("M1", "NMOS"));
    NodeId b = graph.add_net(make_net("nA"));
    graph.add_edge(a, b, {EdgeType::DeviceToPin, ""});

    auto graph2 = std::move(graph);
    REQUIRE(graph2.node_count() == 2);
    REQUIRE(graph2.edge_count() == 1);
    REQUIRE(graph2.find_device("M1").has_value());
}
