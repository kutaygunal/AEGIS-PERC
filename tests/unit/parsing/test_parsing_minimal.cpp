#include <catch2/catch_test_macros.hpp>
#include "aegis/parsing/parser_interface.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Recording callbacks — captures every event for contract verification
// ---------------------------------------------------------------------------

struct RecordingCallbacks : public IParserCallbacks {
    std::filesystem::path begin_path;
    std::vector<std::string>           event_log;
    std::vector<Progress>              progresses;
    std::vector<ParsedCell>            cells;
    std::vector<ParsedNet>             nets;
    std::vector<ParsedPin>             pins;
    std::vector<ParsedGeometry>        geometries;
    std::vector<std::pair<std::string, std::optional<int>>> errors;
    std::optional<bool>                end_success;

    void on_begin(const std::filesystem::path& path) override {
        begin_path = path;
        event_log.push_back("begin");
    }
    void on_progress(const Progress& p) override {
        progresses.push_back(p);
        event_log.push_back("progress:" + std::to_string(p.current) + "/" + std::to_string(p.total));
    }
    void on_cell(const ParsedCell& c) override {
        cells.push_back(c);
        event_log.push_back("cell:" + c.name);
    }
    void on_net(const ParsedNet& n) override {
        nets.push_back(n);
        event_log.push_back("net:" + n.name);
    }
    void on_pin(const ParsedPin& p) override {
        pins.push_back(p);
        event_log.push_back("pin:" + p.name);
    }
    void on_geometry(const ParsedGeometry& g) override {
        geometries.push_back(g);
        event_log.push_back("geometry:" + g.layer);
    }
    void on_error(const std::string& msg, std::optional<int> line) override {
        errors.emplace_back(msg, line);
        event_log.push_back("error:" + msg);
    }
    void on_end(bool success) override {
        end_success = success;
        event_log.push_back("end:" + std::string(success ? "true" : "false"));
    }
};

// ---------------------------------------------------------------------------
// Mock parser — simulates a multi-step parse with all event types
// ---------------------------------------------------------------------------

class MockParser : public IParser {
public:
    std::string format_name() const override { return "MockLayout"; }

    bool parse(const std::filesystem::path& path,
               IParserCallbacks& cb,
               const ICancellationToken& token) override
    {
        cb.on_begin(path);

        cb.on_progress({0, 5, "start"});
        if (token.is_cancelled()) {
            cb.on_error("cancelled before first cell", std::nullopt);
            cb.on_end(false);
            return false;
        }

        cb.on_cell({"INV1", {{"type", "inverter"}}});
        cb.on_progress({1, 5, "cell"});
        if (token.is_cancelled()) {
            cb.on_error("cancelled after cell", std::nullopt);
            cb.on_end(false);
            return false;
        }

        cb.on_net({"n1", {"A", "Y"}, {{"width", "0.5"}}});
        cb.on_progress({2, 5, "net"});
        if (token.is_cancelled()) {
            cb.on_error("cancelled after net", std::nullopt);
            cb.on_end(false);
            return false;
        }

        cb.on_pin({"A", "n1", "INPUT"});
        cb.on_progress({3, 5, "pin"});
        if (token.is_cancelled()) {
            cb.on_error("cancelled after pin", std::nullopt);
            cb.on_end(false);
            return false;
        }

        cb.on_geometry({"M1", "RECTANGLE", {{0.0, 0.0}, {10.0, 10.0}}});
        cb.on_progress({4, 5, "geometry"});
        if (token.is_cancelled()) {
            cb.on_error("cancelled after geometry", std::nullopt);
            cb.on_end(false);
            return false;
        }

        cb.on_progress({5, 5, "done"});
        cb.on_end(true);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Cancelling token — becomes true after N calls to is_cancelled()
// ---------------------------------------------------------------------------

class CancellingToken : public ICancellationToken {
public:
    explicit CancellingToken(int after_calls)
        : m_after(after_calls) {}

    bool is_cancelled() const override {
        return m_calls.fetch_add(1, std::memory_order_relaxed) >= m_after;
    }
private:
    mutable std::atomic<int> m_calls{0};
    int m_after;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Mock parser implements IParser contract",
          "[parsing][ParserInterface][fast][ModuleBoundary]")
{
    MockParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;

    REQUIRE(parser.format_name() == "MockLayout");

    const auto result = parser.parse("/tmp/test.def", cb, token);
    REQUIRE(result);

    REQUIRE(cb.begin_path == std::filesystem::path{"/tmp/test.def"});
    REQUIRE(cb.end_success.value_or(false));

    REQUIRE(cb.cells.size() == 1);
    REQUIRE(cb.cells[0].name == "INV1");
    REQUIRE(cb.cells[0].properties.at("type") == "inverter");

    REQUIRE(cb.nets.size() == 1);
    REQUIRE(cb.nets[0].name == "n1");
    REQUIRE(cb.nets[0].pin_names == std::vector<std::string>{"A", "Y"});

    REQUIRE(cb.pins.size() == 1);
    REQUIRE(cb.pins[0].name == "A");
    REQUIRE(cb.pins[0].net_name == "n1");
    REQUIRE(cb.pins[0].direction == "INPUT");

    REQUIRE(cb.geometries.size() == 1);
    REQUIRE(cb.geometries[0].layer == "M1");
    REQUIRE(cb.geometries[0].shape_type == "RECTANGLE");
    REQUIRE(cb.geometries[0].points.size() == 2);
    REQUIRE(cb.geometries[0].points[0].x == 0.0);
    REQUIRE(cb.geometries[0].points[1].y == 10.0);

    REQUIRE(cb.progresses.size() == 6);
    REQUIRE(cb.progresses[0].current == 0);
    REQUIRE(cb.progresses.back().current == 5);
    REQUIRE(cb.progresses.back().total   == 5);
}

TEST_CASE("NullCancellationToken never cancels",
          "[parsing][ParserInterface][fast]")
{
    NullCancellationToken token;
    REQUIRE(!token.is_cancelled());
    REQUIRE(!token.is_cancelled());
}

TEST_CASE("CancellationToken can be cancelled",
          "[parsing][ParserInterface][fast]")
{
    CancellationToken token;
    REQUIRE(!token.is_cancelled());
    token.cancel();
    REQUIRE(token.is_cancelled());
}

TEST_CASE("Parser respects cancellation after first progress",
          "[parsing][ParserInterface][fast]")
{
    MockParser parser;
    RecordingCallbacks cb;
    CancellingToken token(0); // cancel immediately

    const auto result = parser.parse("/tmp/test.def", cb, token);
    REQUIRE(!result);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());
    REQUIRE(cb.cells.empty());
    REQUIRE(!cb.errors.empty());
}

TEST_CASE("Parser respects cancellation mid-parse",
          "[parsing][ParserInterface][fast]")
{
    MockParser parser;
    RecordingCallbacks cb;
    CancellingToken token(2); // cancel at 3rd check (~before pin emission)

    const auto result = parser.parse("/tmp/test.def", cb, token);
    REQUIRE(!result);
    REQUIRE(cb.end_success.has_value());
    REQUIRE(!cb.end_success.value());

    // Should have emitted begin + cell + net, but not pin/geometry
    REQUIRE(cb.cells.size() == 1);
    REQUIRE(cb.nets.size()  == 1);
    REQUIRE(cb.pins.empty());
    REQUIRE(cb.geometries.empty());
}

TEST_CASE("IParser is format-agnostic -- format_name varies",
          "[parsing][ParserInterface][fast]")
{
    // Verify the interface contract itself: format_name() is pure virtual.
    struct DefParser : public IParser {
        std::string format_name() const override { return "DEF"; }
        bool parse(const std::filesystem::path&,
                   IParserCallbacks&,
                   const ICancellationToken&) override { return true; }
    };
    struct SpiceParser : public IParser {
        std::string format_name() const override { return "SPICE"; }
        bool parse(const std::filesystem::path&,
                   IParserCallbacks&,
                   const ICancellationToken&) override { return true; }
    };

    DefParser    def;
    SpiceParser  spice;
    REQUIRE(def.format_name()    == "DEF");
    REQUIRE(spice.format_name()  == "SPICE");
}
