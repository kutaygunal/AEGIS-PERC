#include <catch2/catch_test_macros.hpp>

#include "aegis/rules/violation.hpp"

#include <string>

using namespace aegis::rules;
using aegis::graph::PropertyMap;

// ---------------------------------------------------------------------------
// Severity
// ---------------------------------------------------------------------------

TEST_CASE("severity round-trips through string",
          "[ViolationModel][Severity][fast]")
{
    CHECK(severity_to_string(Severity::Info) == "info");
    CHECK(severity_to_string(Severity::Warning) == "warning");
    CHECK(severity_to_string(Severity::Error) == "error");
    CHECK(severity_to_string(Severity::Fatal) == "fatal");

    CHECK(severity_from_string("info") == Severity::Info);
    CHECK(severity_from_string("warning") == Severity::Warning);
    CHECK(severity_from_string("error") == Severity::Error);
    CHECK(severity_from_string("fatal") == Severity::Fatal);

    CHECK(severity_from_string("INFO") == Severity::Info);
    CHECK(severity_from_string("WARNING") == Severity::Warning);
    CHECK(severity_from_string("ERROR") == Severity::Error);
    CHECK(severity_from_string("FATAL") == Severity::Fatal);
}

TEST_CASE("severity_from_string rejects unknown",
          "[ViolationModel][Severity][fast]")
{
    CHECK_THROWS_AS(severity_from_string("bogus"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Violation construction
// ---------------------------------------------------------------------------

TEST_CASE("Violation default constructs with Error severity",
          "[ViolationModel][Violation][fast]")
{
    Violation v;
    CHECK(v.severity == Severity::Error);
    CHECK(v.rule_id.empty());
    CHECK(v.message.empty());
}

TEST_CASE("Violation three-arg constructor",
          "[ViolationModel][Violation][fast]")
{
    Violation v{"RULE-01", Severity::Warning, "something is off"};
    CHECK(v.rule_id == "RULE-01");
    CHECK(v.severity == Severity::Warning);
    CHECK(v.message == "something is off");
    CHECK(v.id.empty());
    CHECK_FALSE(v.location.net_name.has_value());
}

TEST_CASE("Violation four-arg flat location constructor",
          "[ViolationModel][Violation][fast]")
{
    Violation v{"RULE-01", Severity::Error, "bad net", "n1"};
    CHECK(v.rule_id == "RULE-01");
    CHECK(v.location.net_name.has_value());
    CHECK(v.location.net_name.value() == "n1");
}

TEST_CASE("Violation full constructor with structured location",
          "[ViolationModel][Violation][fast]")
{
    ViolationLocation loc;
    loc.layer     = "M1";
    loc.net_name  = "vdd";
    loc.pin_name  = "vdd_port";
    loc.device_name = "M1";

    Violation v{"RULE-01", Severity::Fatal, "critical", loc};
    CHECK(v.location.layer.has_value());
    CHECK(v.location.layer.value() == "M1");
    CHECK(v.location.pin_name.value() == "vdd_port");
    CHECK(v.location.device_name.value() == "M1");
}

TEST_CASE("Violation equality compares all fields",
          "[ViolationModel][Violation][fast]")
{
    Violation a{"R1", Severity::Error, "msg", "n1"};
    Violation b{"R1", Severity::Error, "msg", "n1"};
    Violation c{"R2", Severity::Error, "msg", "n1"};
    Violation d{"R1", Severity::Warning, "msg", "n1"};

    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK_FALSE(a == d);
}

TEST_CASE("Violation make_violation convenience factories",
          "[ViolationModel][Violation][fast]")
{
    auto a = make_violation("R1", Severity::Error, "msg");
    CHECK(a.rule_id == "R1");
    CHECK_FALSE(a.location.net_name.has_value());

    auto b = make_violation("R1", Severity::Error, "msg", "n1");
    CHECK(b.location.net_name.value() == "n1");

    ViolationLocation loc;
    loc.layer = "M1";
    auto c = make_violation("R1", Severity::Error, "msg", loc);
    CHECK(c.location.layer.value() == "M1");
}

// ---------------------------------------------------------------------------
// Violation metadata (PropertyMap)
// ---------------------------------------------------------------------------

TEST_CASE("Violation can carry metadata",
          "[ViolationModel][Metadata][fast]")
{
    Violation v{"R1", Severity::Error, "msg"};
    v.metadata = v.metadata.with("net_count", 42);
    CHECK(v.metadata.get<int>("net_count").value() == 42);
}

// ---------------------------------------------------------------------------
// Violation JSON
// ---------------------------------------------------------------------------

TEST_CASE("Violation serializes to JSON",
          "[ViolationModel][JSON][fast]")
{
    Violation v{"ELEC-001", Severity::Error, "floating net", "n1"};
    v.metadata = v.metadata.with("line", 42);

    nlohmann::json j = v;
    CHECK(j["rule_id"] == "ELEC-001");
    CHECK(j["severity"] == "error");
    CHECK(j["message"] == "floating net");
    CHECK(j["location"]["net_name"] == "n1");
    CHECK(j["metadata"]["line"] == 42);
}

TEST_CASE("Violation deserializes from JSON",
          "[ViolationModel][JSON][fast]")
{
    nlohmann::json j = {
        {"id", ""},
        {"rule_id", "ELEC-002"},
        {"severity", "warning"},
        {"message", "open pin"},
        {"location", {
            {"net_name", nullptr},
            {"pin_name", "M1.gate"},
            {"device_name", nullptr}
        }},
        {"metadata", nullptr}
    };

    auto v = j.get<Violation>();
    CHECK(v.rule_id == "ELEC-002");
    CHECK(v.severity == Severity::Warning);
    CHECK(v.message == "open pin");
    REQUIRE(v.location.pin_name.has_value());
    CHECK(v.location.pin_name.value() == "M1.gate");
    CHECK_FALSE(v.location.net_name.has_value());
}

TEST_CASE("Violation JSON round-trip preserves data",
          "[ViolationModel][JSON][fast]")
{
    ViolationLocation loc;
    loc.layer = "M2";
    loc.net_name = "out";

    Violation original{"R1", Severity::Fatal, "boom", loc};
    original.metadata = original.metadata.with("x", 1.5);

    nlohmann::json j = original;
    auto restored = j.get<Violation>();

    CHECK(restored == original);
}

// ---------------------------------------------------------------------------
// ViolationCollection basics
// ---------------------------------------------------------------------------

TEST_CASE("ViolationCollection constructs empty",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    CHECK(coll.empty());
    CHECK(coll.size() == 0);
}

TEST_CASE("ViolationCollection add and size",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error, "msg"});
    coll.add({"R2", Severity::Warning, "msg2"});
    CHECK(coll.size() == 2);
    CHECK_FALSE(coll.empty());
}

TEST_CASE("ViolationCollection operator[] access",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error, "first"});
    coll.add({"R2", Severity::Warning, "second"});
    CHECK(coll[0].rule_id == "R1");
    CHECK(coll[1].rule_id == "R2");
    CHECK_THROWS_AS(coll[99], std::out_of_range);
}

TEST_CASE("ViolationCollection clear",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error, "msg"});
    coll.clear();
    CHECK(coll.empty());
}

TEST_CASE("ViolationCollection append collection",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection a;
    a.add({"R1", Severity::Error, "a"});

    ViolationCollection b;
    b.add({"R2", Severity::Warning, "b"});

    a.append(b);
    CHECK(a.size() == 2);
}

TEST_CASE("ViolationCollection append vector",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection a;
    a.add({"R1", Severity::Error, "a"});
    a.append(std::vector<Violation>{
        {"R2", Severity::Warning, "b"},
        {"R3", Severity::Info, "c"}});
    CHECK(a.size() == 3);
}

// ---------------------------------------------------------------------------
// ViolationCollection filtering
// ---------------------------------------------------------------------------

TEST_CASE("filter_by_severity",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error,   "err1"});
    coll.add({"R2", Severity::Warning, "warn"});
    coll.add({"R3", Severity::Error,   "err2"});
    coll.add({"R4", Severity::Info,    "info"});

    auto errors = coll.filter_by_severity(Severity::Error);
    CHECK(errors.size() == 2);
    for (const auto& v : errors) {
        CHECK(v.severity == Severity::Error);
    }

    auto infos = coll.filter_by_severity(Severity::Info);
    CHECK(infos.size() == 1);
    CHECK(infos[0].rule_id == "R4");

    auto fatals = coll.filter_by_severity(Severity::Fatal);
    CHECK(fatals.empty());
}

TEST_CASE("filter_by_rule",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"ELEC-001", Severity::Error, "msg"});
    coll.add({"ELEC-002", Severity::Error, "msg"});
    coll.add({"ELEC-001", Severity::Warning, "msg"});

    auto filtered = coll.filter_by_rule("ELEC-001");
    CHECK(filtered.size() == 2);
    for (const auto& v : filtered) {
        CHECK(v.rule_id == "ELEC-001");
    }
}

TEST_CASE("filter_by_predicate",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error,   "alpha"});
    coll.add({"R2", Severity::Warning, "beta"});
    coll.add({"R3", Severity::Error,   "alpha"});

    auto filtered = coll.filter_by_predicate(
        [](const Violation& v) { return v.message == "alpha"; });
    CHECK(filtered.size() == 2);
}

// ---------------------------------------------------------------------------
// ViolationCollection grouping
// ---------------------------------------------------------------------------

TEST_CASE("group_by_rule",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"A", Severity::Error,   "a1"});
    coll.add({"B", Severity::Warning, "b1"});
    coll.add({"A", Severity::Error,   "a2"});

    auto groups = coll.group_by_rule();
    CHECK(groups.size() == 2);
    CHECK(groups.at("A").size() == 2);
    CHECK(groups.at("B").size() == 1);
}

TEST_CASE("group_by_severity",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error,   "e"});
    coll.add({"R2", Severity::Warning, "w"});
    coll.add({"R3", Severity::Error,   "e2"});
    coll.add({"R4", Severity::Info,    "i"});

    auto groups = coll.group_by_severity();
    CHECK(groups.at(Severity::Error).size() == 2);
    CHECK(groups.at(Severity::Warning).size() == 1);
    CHECK(groups.at(Severity::Info).size() == 1);
    CHECK(groups.count(Severity::Fatal) == 0);
}

// ---------------------------------------------------------------------------
// ViolationCollection pagination
// ---------------------------------------------------------------------------

TEST_CASE("paginate basic",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    for (int i = 0; i < 10; ++i) {
        coll.add({"R", Severity::Error, std::to_string(i)});
    }

    auto page1 = coll.paginate(0, 3);
    CHECK(page1.size() == 3);
    CHECK(page1[0].message == "0");
    CHECK(page1[2].message == "2");

    auto page2 = coll.paginate(3, 3);
    CHECK(page2.size() == 3);
    CHECK(page2[0].message == "3");

    auto page3 = coll.paginate(9, 5);
    CHECK(page3.size() == 1);
    CHECK(page3[0].message == "9");
}

TEST_CASE("paginate offset out of bounds returns empty",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R", Severity::Error, "only"});
    auto page = coll.paginate(99, 5);
    CHECK(page.empty());
}

TEST_CASE("paginate with limit zero returns empty",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R", Severity::Error, "only"});
    auto page = coll.paginate(0, 0);
    CHECK(page.empty());
}

// ---------------------------------------------------------------------------
// ViolationCollection sorting
// ---------------------------------------------------------------------------

TEST_CASE("sort_by_severity orders Fatal > Error > Warning > Info",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Info,    "i"});
    coll.add({"R2", Severity::Fatal,   "f"});
    coll.add({"R3", Severity::Warning, "w"});
    coll.add({"R4", Severity::Error,   "e"});

    coll.sort_by_severity();
    CHECK(coll[0].severity == Severity::Fatal);
    CHECK(coll[1].severity == Severity::Error);
    CHECK(coll[2].severity == Severity::Warning);
    CHECK(coll[3].severity == Severity::Info);
}

TEST_CASE("sort_by_rule_id is alphabetical",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"Z", Severity::Error, "z"});
    coll.add({"A", Severity::Error, "a"});
    coll.add({"M", Severity::Error, "m"});

    coll.sort_by_rule_id();
    CHECK(coll[0].rule_id == "A");
    CHECK(coll[1].rule_id == "M");
    CHECK(coll[2].rule_id == "Z");
}

TEST_CASE("sort_by_message is alphabetical",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll;
    coll.add({"R", Severity::Error, "zebra"});
    coll.add({"R", Severity::Error, "apple"});

    coll.sort_by_message();
    CHECK(coll[0].message == "apple");
    CHECK(coll[1].message == "zebra");
}

// ---------------------------------------------------------------------------
// ViolationCollection JSON
// ---------------------------------------------------------------------------

TEST_CASE("ViolationCollection serializes to JSON array",
          "[ViolationModel][JSON][fast]")
{
    ViolationCollection coll;
    coll.add({"R1", Severity::Error,   "msg1", "n1"});
    coll.add({"R2", Severity::Warning, "msg2", "n2"});

    nlohmann::json j = coll.to_json();
    CHECK(j.is_array());
    CHECK(j.size() == 2);
    CHECK(j[0]["rule_id"] == "R1");
    CHECK(j[1]["severity"] == "warning");
}

TEST_CASE("ViolationCollection deserializes from JSON array",
          "[ViolationModel][JSON][fast]")
{
    nlohmann::json j = nlohmann::json::array();
    j.push_back({
        {"rule_id", "R1"},
        {"severity", "error"},
        {"message", "bad"},
        {"location", {}}
    });
    j.push_back({
        {"rule_id", "R2"},
        {"severity", "info"},
        {"message", "ok"},
        {"location", {}}
    });

    auto coll = ViolationCollection::from_json(j);
    CHECK(coll.size() == 2);
    CHECK(coll[0].rule_id == "R1");
    CHECK(coll[1].severity == Severity::Info);
}

TEST_CASE("ViolationCollection rejects non-array JSON",
          "[ViolationModel][JSON][fast]")
{
    nlohmann::json j = {{"not", "an array"}};
    CHECK_THROWS_AS(ViolationCollection::from_json(j), std::invalid_argument);
}

TEST_CASE("ViolationCollection round-trip via JSON string",
          "[ViolationModel][JSON][fast]")
{
    ViolationLocation loc;
    loc.net_name = "vdd";
    loc.layer = "M1";

    ViolationCollection original;
    original.add({"R1", Severity::Error,   "msg1", loc});
    original.add({"R2", Severity::Warning, "msg2", "n2"});

    auto json_str = original.to_json_string();
    auto restored = ViolationCollection::from_json_string(json_str);

    CHECK(restored.size() == 2);
    CHECK(restored[0] == original[0]);
    CHECK(restored[1] == original[1]);
}

TEST_CASE("ViolationCollection equality",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection a;
    a.add({"R1", Severity::Error, "msg"});

    ViolationCollection b;
    b.add({"R1", Severity::Error, "msg"});

    ViolationCollection c;
    c.add({"R1", Severity::Warning, "msg"});

    CHECK(a == b);
    CHECK_FALSE(a == c);
}

TEST_CASE("ViolationCollection construct from vector",
          "[ViolationModel][Collection][fast]")
{
    ViolationCollection coll(std::vector<Violation>{
        {"R1", Severity::Error, "msg1"},
        {"R2", Severity::Error, "msg2"}
    });
    CHECK(coll.size() == 2);
    CHECK(coll[0].rule_id == "R1");
}
