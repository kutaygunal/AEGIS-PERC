#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "aegis/graph/data_model.hpp"

#include <unordered_set>

using namespace aegis::graph;

// ---------------------------------------------------------------------------
// PropertyMap
// ---------------------------------------------------------------------------

TEST_CASE("PropertyMap is empty by default", "[graph][DataModel][fast]")
{
    PropertyMap pmap;
    REQUIRE(pmap.empty());
    REQUIRE(pmap.size() == 0);
    REQUIRE(!pmap.has("key"));
    REQUIRE(!pmap.raw("key").has_value());
}

TEST_CASE("PropertyMap stores string values", "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap().with("name", std::string{"alice"});
    REQUIRE(!pmap.empty());
    REQUIRE(pmap.size() == 1);
    REQUIRE(pmap.has("name"));

    auto raw = pmap.raw("name");
    REQUIRE(raw.has_value());
    REQUIRE(std::holds_alternative<std::string>(*raw));
    REQUIRE(std::get<std::string>(*raw) == "alice");
}

TEST_CASE("PropertyMap stores int values", "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap().with("age", 42);
    auto raw = pmap.raw("age");
    REQUIRE(raw.has_value());
    REQUIRE(std::holds_alternative<int>(*raw));
    REQUIRE(std::get<int>(*raw) == 42);
}

TEST_CASE("PropertyMap stores double values", "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap().with("ratio", 3.14);
    auto raw = pmap.raw("ratio");
    REQUIRE(raw.has_value());
    REQUIRE(std::holds_alternative<double>(*raw));
    REQUIRE_THAT(std::get<double>(*raw), Catch::Matchers::WithinAbs(3.14, 0.001));
}

TEST_CASE("PropertyMap stores bool values", "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap().with("active", true);
    auto raw = pmap.raw("active");
    REQUIRE(raw.has_value());
    REQUIRE(std::holds_alternative<bool>(*raw));
    REQUIRE(std::get<bool>(*raw) == true);
}

TEST_CASE("PropertyMap get<string> coerces all scalar types",
          "[graph][DataModel][fast]")
{
    REQUIRE(PropertyMap().with("s", std::string{"hi"}).get<std::string>("s") == "hi");
    REQUIRE(PropertyMap().with("i", 42)   .get<std::string>("i") == "42");
    REQUIRE(PropertyMap().with("d", 3.0).get<std::string>("d") == "3.000000");
    REQUIRE(PropertyMap().with("b", true) .get<std::string>("b") == "true");
    REQUIRE(PropertyMap().with("b", false).get<std::string>("b") == "false");
}

TEST_CASE("PropertyMap get<double> coerces from string, int, bool",
          "[graph][DataModel][fast]")
{
    REQUIRE(PropertyMap().with("d", 2.5)   .get<double>("d") == 2.5);
    REQUIRE(PropertyMap().with("i", 7)      .get<double>("i") == 7.0);
    REQUIRE(PropertyMap().with("b", true)   .get<double>("b") == 1.0);
    REQUIRE(PropertyMap().with("b", false)  .get<double>("b") == 0.0);
    REQUIRE(PropertyMap().with("s", std::string{"3.14"}).get<double>("s") == 3.14);

    // Invalid string -> nullopt
    REQUIRE(!PropertyMap().with("bad", std::string{"abc"}).get<double>("bad").has_value());
}

TEST_CASE("PropertyMap get<int> coerces from string, double, bool",
          "[graph][DataModel][fast]")
{
    REQUIRE(PropertyMap().with("i", 10)   .get<int>("i") == 10);
    REQUIRE(PropertyMap().with("d", 4.9)    .get<int>("d") == 4); // truncation
    REQUIRE(PropertyMap().with("b", true)  .get<int>("b") == 1);
    REQUIRE(PropertyMap().with("s", std::string{"99"}).get<int>("s") == 99);

    REQUIRE(!PropertyMap().with("bad", std::string{"x"}).get<int>("bad").has_value());
}

TEST_CASE("PropertyMap get<bool> coerces from recognized string values",
          "[graph][DataModel][fast]")
{
    // Positive string patterns
    REQUIRE( PropertyMap().with("s", std::string{"true"}).get<bool>("s") );
    REQUIRE( PropertyMap().with("s", std::string{"yes"}) .get<bool>("s") );
    REQUIRE( PropertyMap().with("s", std::string{"1"})   .get<bool>("s") );
    REQUIRE( PropertyMap().with("s", std::string{"on"})  .get<bool>("s") );
    // Direct bool storage verified by "PropertyMap stores bool values"
}

TEST_CASE("PropertyMap without returns copy missing key",
          "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap()
        .with("a", 1)
        .with("b", 2)
        .with("c", 3);

    auto reduced = pmap.without("b");
    REQUIRE(reduced.has("a"));
    REQUIRE(!reduced.has("b"));
    REQUIRE(reduced.has("c"));
    REQUIRE(pmap.has("b")); // original unchanged
}

TEST_CASE("PropertyMap preserves original on with()",
          "[graph][DataModel][fast]")
{
    auto base = PropertyMap().with("x", 1);
    auto derived = base.with("y", 2);

    REQUIRE(base.size() == 1);
    REQUIRE(derived.size() == 2);
    REQUIRE(base.has("x"));
    REQUIRE(!base.has("y"));
    REQUIRE(derived.has("x"));
    REQUIRE(derived.has("y"));
}

TEST_CASE("PropertyMap from_string_map imports all as strings",
          "[graph][DataModel][fast]")
{
    std::map<std::string, std::string> src = {
        {"w", "0.5um"},
        {"l", "0.18um"},
        {"type", "NMOS"},
    };
    auto pmap = PropertyMap::from_string_map(src);
    REQUIRE(pmap.size() == 3);
    REQUIRE(pmap.get<std::string>("w") == "0.5um");
    REQUIRE(pmap.get<std::string>("l") == "0.18um");
    REQUIRE(pmap.get<std::string>("type") == "NMOS");

    // String values can be coerced to double
    REQUIRE(pmap.get<double>("w").has_value());
    REQUIRE_THAT(pmap.get<double>("w").value(), Catch::Matchers::WithinAbs(0.5, 0.001));
}

TEST_CASE("PropertyMap equality compares exact entries",
          "[graph][DataModel][fast]")
{
    auto a = PropertyMap().with("x", 1).with("y", 2);
    auto b = PropertyMap().with("x", 1).with("y", 2);
    auto c = PropertyMap().with("x", std::string{"1"}).with("y", 2);

    REQUIRE(a == b);
    REQUIRE(a != c); // int 1 != string "1"
    REQUIRE(b != c);
}

TEST_CASE("PropertyMap hash is deterministic and consistent with equality",
          "[graph][DataModel][fast]")
{
    auto a = PropertyMap().with("x", 1).with("y", std::string{"hello"});
    auto b = PropertyMap().with("x", 1).with("y", std::string{"hello"});
    auto c = PropertyMap().with("x", 2).with("y", std::string{"hello"});

    REQUIRE(hash_value(a) == hash_value(b));
    REQUIRE(hash_value(a) != hash_value(c));
}

TEST_CASE("PropertyMap range iteration works",
          "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap()
        .with("z", 1)
        .with("a", 2);

    std::size_t count = 0;
    for (const auto& [k, v] : pmap) {
        ++count;
        REQUIRE((k == "a" || k == "z"));
    }
    REQUIRE(count == 2);
}

TEST_CASE("PropertyMap missing key returns nullopt",
          "[graph][DataModel][fast]")
{
    auto pmap = PropertyMap().with("only", 1);
    REQUIRE(!pmap.raw("missing").has_value());
    REQUIRE(!pmap.get<int>("missing").has_value());
    REQUIRE(!pmap.get<double>("missing").has_value());
    REQUIRE(!pmap.get<bool>("missing").has_value());
    REQUIRE(!pmap.get<std::string>("missing").has_value());
}

// ---------------------------------------------------------------------------
// Direction
// ---------------------------------------------------------------------------

TEST_CASE("Direction round-trips through string",
          "[graph][DataModel][fast]")
{
    REQUIRE(direction_to_string(Direction::Input)  == "INPUT");
    REQUIRE(direction_to_string(Direction::Output) == "OUTPUT");
    REQUIRE(direction_to_string(Direction::InOut)  == "INOUT");
    REQUIRE(direction_to_string(Direction::BiDir)  == "BIDIR");

    REQUIRE(direction_from_string("INPUT")  == Direction::Input);
    REQUIRE(direction_from_string("OUTPUT") == Direction::Output);
    REQUIRE(direction_from_string("INOUT")  == Direction::InOut);
    REQUIRE(direction_from_string("UNKNOWN") == Direction::BiDir); // fallback
    REQUIRE(direction_from_string("input")  == Direction::Input); // case-insensitive
}

// ---------------------------------------------------------------------------
// PinModel
// ---------------------------------------------------------------------------

TEST_CASE("PinModel equality and inequality", "[graph][DataModel][fast]")
{
    PinModel a{.name = "pgate", .direction = Direction::Input,
               .net_name = "nA", .layer = "M1",
               .location = Point{1.0, 2.0},
               .properties = PropertyMap().with("cap", 0.1)};

    PinModel b = a;
    REQUIRE(a == b);

    b.direction = Direction::Output;
    REQUIRE(a != b);
}

TEST_CASE("PinModel can be hashed and used in unordered_set",
          "[graph][DataModel][fast]")
{
    PinModel a{"A", Direction::Input, "nA"};
    PinModel b{"B", Direction::Output, "nB"};

    std::unordered_set<std::size_t> hashes;
    hashes.insert(hash_value(a));
    hashes.insert(hash_value(b));
    REQUIRE(hashes.size() == 2);

    PinModel a2 = a;
    REQUIRE(hash_value(a) == hash_value(a2));
}

// ---------------------------------------------------------------------------
// NetModel
// ---------------------------------------------------------------------------

TEST_CASE("NetModel stores name, pins, properties",
          "[graph][DataModel][fast]")
{
    NetModel net;
    net.name = "vdd";
    net.pin_names = {"p1", "p2", "p3"};
    net.properties = PropertyMap().with("width", 0.5).with("spacing", 0.3);

    REQUIRE(net.name == "vdd");
    REQUIRE(net.pin_names.size() == 3);
    REQUIRE(net.properties.size() == 2);
    REQUIRE(net.properties.get<double>("width") == 0.5);
}

TEST_CASE("NetModel equality", "[graph][DataModel][fast]")
{
    NetModel a{"nA", {"A", "B"}, PropertyMap()};
    NetModel b{"nA", {"A", "B"}, PropertyMap()};
    NetModel c{"nA", {"A"}, PropertyMap()};

    REQUIRE(a == b);
    REQUIRE(a != c);
}

// ---------------------------------------------------------------------------
// DeviceModel
// ---------------------------------------------------------------------------

TEST_CASE("DeviceModel stores name, type, pins, properties",
          "[graph][DataModel][fast]")
{
    DeviceModel dev;
    dev.name = "M1";
    dev.device_type = "NMOS";
    dev.pins = {{"gate", "nA"}, {"source", "GND"}, {"drain", "n_int"}};
    dev.properties = PropertyMap().with("w", 0.5).with("l", 0.18);

    REQUIRE(dev.name == "M1");
    REQUIRE(dev.device_type == "NMOS");
    REQUIRE(dev.pins.size() == 3);
    REQUIRE(dev.pins.at("gate") == "nA");

    REQUIRE(dev.properties.get<double>("w").has_value());
    REQUIRE(dev.width().has_value());
    REQUIRE_THAT(dev.width().value(), Catch::Matchers::WithinAbs(0.5, 0.001));
}

TEST_CASE("DeviceModel width() and length() convenience accessors",
          "[graph][DataModel][fast]")
{
    DeviceModel dev;
    dev.properties = PropertyMap()
        .with("w", 0.5)
        .with("l", 0.18)
        .with("model", std::string{"tsmc65"});

    REQUIRE(dev.width().value() == 0.5);
    REQUIRE(dev.length().value() == 0.18);
    REQUIRE(dev.model().value() == "tsmc65");
}

TEST_CASE("DeviceModel width() returns nullopt when absent",
          "[graph][DataModel][fast]")
{
    DeviceModel dev;
    REQUIRE(!dev.width().has_value());
    REQUIRE(!dev.length().has_value());
    REQUIRE(!dev.model().has_value());
}

TEST_CASE("DeviceModel with_width returns new copy",
          "[graph][DataModel][fast]")
{
    DeviceModel base;
    base.name = "M1";
    base.properties = PropertyMap().with("l", 0.18);

    auto updated = base.with_width(0.5);

    // Original unchanged
    REQUIRE(!base.width().has_value());
    REQUIRE(base.properties.size() == 1);

    // New copy has width
    REQUIRE(updated.width().has_value());
    REQUIRE(updated.width().value() == 0.5);
    REQUIRE(updated.length().has_value());
    REQUIRE(updated.length().value() == 0.18);
}

TEST_CASE("DeviceModel with_model returns new copy",
          "[graph][DataModel][fast]")
{
    DeviceModel base;
    base.properties = PropertyMap().with("w", 0.5);

    auto m = base.with_model("65nm");
    REQUIRE(m.model().value() == "65nm");
    REQUIRE(m.width().value() == 0.5);

    // base unchanged
    REQUIRE(!base.model().has_value());
}

TEST_CASE("DeviceModel equality", "[graph][DataModel][fast]")
{
    DeviceModel a{"M1", "NMOS", {{"g", "nA"}}, PropertyMap().with("w", 0.5)};
    DeviceModel b{"M1", "NMOS", {{"g", "nA"}}, PropertyMap().with("w", 0.5)};
    DeviceModel c{"M1", "NMOS", {{"g", "nA"}}, PropertyMap().with("w", 0.4)};

    REQUIRE(a == b);
    REQUIRE(a != c);
}

TEST_CASE("DeviceModel hashing", "[graph][DataModel][fast]")
{
    DeviceModel a{"M1", "NMOS", {}, PropertyMap().with("w", 0.5)};
    DeviceModel b{"M2", "PMOS", {}, PropertyMap().with("w", 1.0)};

    REQUIRE(hash_value(a) == hash_value(a));
    REQUIRE(hash_value(a) != hash_value(b));
}

// ---------------------------------------------------------------------------
// PortModel
// ---------------------------------------------------------------------------

TEST_CASE("PortModel stores external port attributes",
          "[graph][DataModel][fast]")
{
    PortModel port{
        .name = "CLK",
        .direction = Direction::Input,
        .net_name = "clk_net",
        .layer = "M2",
        .location = Point{5.0, 10.0},
        .properties = PropertyMap().with("cap", 0.02)
    };

    REQUIRE(port.name == "CLK");
    REQUIRE(port.direction == Direction::Input);
    REQUIRE(port.net_name == "clk_net");
    REQUIRE(port.layer.value() == "M2");
    REQUIRE(port.location->x == 5.0);
    REQUIRE(port.location->y == 10.0);
    REQUIRE(port.properties.get<double>("cap").value() == 0.02);
}

TEST_CASE("PortModel equality", "[graph][DataModel][fast]")
{
    PortModel a{"A", Direction::Input, "nA"};
    PortModel b{"A", Direction::Input, "nA"};
    PortModel c{"A", Direction::Output, "nA"};

    REQUIRE(a == b);
    REQUIRE(a != c);
}

// ---------------------------------------------------------------------------
// Integration: round-trip through LayoutIR property string map
// ---------------------------------------------------------------------------

TEST_CASE("PropertyMap from_string_map handles device properties",
          "[graph][DataModel][fast]")
{
    std::map<std::string, std::string> src = {
        {"w", "0.5um"},
        {"l", "0.180um"},
        {"model", "tsmc65nm"},
        {"multiplier", "4"},
        {"bulk_tap", "VDD"},
    };
    auto pmap = PropertyMap::from_string_map(src);

    REQUIRE(pmap.size() == 5);
    REQUIRE(pmap.get<std::string>("w") == "0.5um");
    REQUIRE(pmap.get<std::string>("l") == "0.180um");
    REQUIRE(pmap.get<std::string>("model") == "tsmc65nm");

    // multi-step: string -> double extraction
    auto w = pmap.get<double>("w");
    REQUIRE(w.has_value());

    auto mult = pmap.get<int>("multiplier");
    REQUIRE(mult.has_value());
    REQUIRE(mult.value() == 4);
}

// ---------------------------------------------------------------------------
// std::hash specialisations
// ---------------------------------------------------------------------------

TEST_CASE("std::hash for DeviceModel works in unordered_set",
          "[graph][DataModel][fast]")
{
    DeviceModel a{"M1", "NMOS", {}, PropertyMap().with("w", 0.5)};
    DeviceModel b{"M2", "PMOS", {}, PropertyMap().with("w", 1.0)};
    DeviceModel a2{"M1", "NMOS", {}, PropertyMap().with("w", 0.5)};

    std::unordered_set<DeviceModel> set;
    set.insert(a);
    set.insert(b);
    set.insert(a2); // duplicate

    REQUIRE(set.size() == 2);
}

TEST_CASE("PropertyMap used as key in unordered_map",
          "[graph][DataModel][fast]")
{
    std::unordered_map<PropertyMap, int> counts;
    auto a = PropertyMap().with("x", 1);
    auto b = PropertyMap().with("x", 2);

    counts[a] = 10;
    counts[b] = 20;

    REQUIRE(counts.at(a) == 10);
    REQUIRE(counts.at(b) == 20);
    REQUIRE(counts.size() == 2);
}
