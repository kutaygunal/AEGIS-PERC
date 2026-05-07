#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/domain_tagging.hpp"

using namespace aegis::graph;

// ---------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------

TEST_CASE("DomainTagger empty on empty graph", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    DomainTagger tagger;
    tagger.propagate(graph);
    CHECK(tagger.conflicts().empty());
}

TEST_CASE("DomainTagging seeds and retrieves", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    DomainTagger tagger;
    tagger.seed(net, {"VDD", "power"});
    auto tag = tagger.tag_for(net);
    REQUIRE(tag.has_value());
    CHECK(tag->domain == "VDD");
    CHECK(tag->signal == "power");
}

TEST_CASE("DomainTagging seed conflict", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});

    DomainTagger tagger;
    tagger.seed(net, {"VDD", "power"});
    tagger.seed(net, {"GND", "ground"});

    auto tag = tagger.tag_for(net);
    REQUIRE(tag.has_value());
    CHECK(tag->domain == "VDD"); // original kept

    REQUIRE(tagger.conflicts().size() == 1);
    CHECK(tagger.conflicts()[0].node_id == net);
    CHECK(tagger.conflicts()[0].existing_domain == "VDD");
    CHECK(tagger.conflicts()[0].conflicting_domain == "GND");
}

// ---------------------------------------------------------------------------
// Propagation
// ---------------------------------------------------------------------------

TEST_CASE("DomainTagging propagates net to device", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"VDD", {}});
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto pin = graph.add_pin(PinNode{"M1.source", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "source"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    DomainTagger tagger;
    tagger.seed(net, {"VDD", "power"});
    tagger.propagate(graph);

    auto dev_tag = tagger.tag_for(dev);
    REQUIRE(dev_tag.has_value());
    CHECK(dev_tag->domain == "VDD");
    CHECK(dev_tag->signal == "power");
    CHECK(tagger.conflicts().empty());
}

TEST_CASE("DomainTagging propagates device to net", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"n1", {}});
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto pin = graph.add_pin(PinNode{"M1.source", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin, {EdgeType::DeviceToPin, "source"});
    graph.add_edge(pin, net, {EdgeType::NetToPin, ""});

    DomainTagger tagger;
    tagger.seed(dev, {"VDD", "power"});
    tagger.propagate(graph);

    auto net_tag = tagger.tag_for(net);
    REQUIRE(net_tag.has_value());
    CHECK(net_tag->domain == "VDD");
    CHECK(tagger.conflicts().empty());
}

TEST_CASE("DomainTagging propagates through pin chain", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"CLK", {}});
    auto dev = graph.add_device(DeviceNode{"BUF1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"BUF1.A", "INPUT", std::nullopt, std::nullopt, std::nullopt});
    auto pin2 = graph.add_pin(PinNode{"BUF1.Y", "OUTPUT", std::nullopt, std::nullopt, std::nullopt});
    auto out_net = graph.add_net(NetNode{"out", {}});

    graph.add_edge(dev, pin1, {EdgeType::DeviceToPin, "A"});
    graph.add_edge(pin1, net, {EdgeType::NetToPin, ""});
    graph.add_edge(dev, pin2, {EdgeType::DeviceToPin, "Y"});
    graph.add_edge(pin2, out_net, {EdgeType::NetToPin, ""});

    DomainTagger tagger;
    tagger.seed(net, {"CLK", "clock"});
    tagger.propagate(graph);

    CHECK(tagger.tag_for(dev)->domain == "CLK");
    CHECK(tagger.tag_for(pin1)->domain == "CLK");
    CHECK(tagger.tag_for(pin2)->domain == "CLK");
    CHECK(tagger.tag_for(out_net)->domain == "CLK");
    CHECK(tagger.conflicts().empty());
}

// ---------------------------------------------------------------------------
// Conflict detection
// ---------------------------------------------------------------------------

TEST_CASE("DomainTagging detects conflict", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net1 = graph.add_net(NetNode{"VDD", {}});
    auto net2 = graph.add_net(NetNode{"GND", {}});
    auto dev = graph.add_device(DeviceNode{"M1", "NMOS", {}});
    auto pin1 = graph.add_pin(PinNode{"M1.source", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    auto pin2 = graph.add_pin(PinNode{"M1.drain", "BIDIR", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(dev, pin1, {EdgeType::DeviceToPin, "source"});
    graph.add_edge(pin1, net1, {EdgeType::NetToPin, ""});
    graph.add_edge(dev, pin2, {EdgeType::DeviceToPin, "drain"});
    graph.add_edge(pin2, net2, {EdgeType::NetToPin, ""});

    DomainTagger tagger;
    tagger.seed(net1, {"VDD", "power"});
    tagger.seed(net2, {"GND", "ground"});
    tagger.propagate(graph);

    // Both the device and the second pin are contested.
    REQUIRE(tagger.conflicts().size() == 2);
    auto dev_conflict = std::find_if(
        tagger.conflicts().begin(), tagger.conflicts().end(),
        [dev](const auto& c) { return c.node_id == dev; });
    REQUIRE(dev_conflict != tagger.conflicts().end());
    CHECK(dev_conflict->existing_domain == "VDD");
    CHECK(dev_conflict->conflicting_domain == "GND");
}

TEST_CASE("DomainTagging clear resets state", "[DomainTagging][fast]")
{
    ConnectivityGraph graph;
    auto net = graph.add_net(NetNode{"VDD", {}});

    DomainTagger tagger;
    tagger.seed(net, {"VDD", "power"});
    tagger.clear();

    CHECK_FALSE(tagger.tag_for(net).has_value());
    CHECK(tagger.conflicts().empty());
}
