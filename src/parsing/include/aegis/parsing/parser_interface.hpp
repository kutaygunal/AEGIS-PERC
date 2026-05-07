#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::parsing {

// ---------------------------------------------------------------------------
// Parsed entity types emitted by any layout/netlist parser
// ---------------------------------------------------------------------------

struct ParsedPoint {
    double x = 0.0;
    double y = 0.0;
};

struct ParsedCell {
    std::string name;
    std::map<std::string, std::string> properties;
};

struct ParsedNet {
    std::string name;
    std::vector<std::string> pin_names;
    std::map<std::string, std::string> properties;
};

struct ParsedPin {
    std::string name;
    std::string net_name;
    std::string direction; // "INPUT", "OUTPUT", "INOUT"
};

struct ParsedGeometry {
    std::string layer;
    std::string shape_type; // e.g. "RECTANGLE", "POLYGON"
    std::vector<ParsedPoint> points;
};

// ---------------------------------------------------------------------------
// Progress reported during parsing
// ---------------------------------------------------------------------------

struct Progress {
    std::size_t current = 0;
    std::size_t total   = 0;
    std::string message;
};

// ---------------------------------------------------------------------------
// Cancellation token — checked by parsers at safe yield points
// ---------------------------------------------------------------------------

class ICancellationToken {
public:
    virtual ~ICancellationToken()          = default;
    virtual bool is_cancelled() const = 0;
};

class CancellationToken : public ICancellationToken {
public:
    void cancel() noexcept { m_cancelled.store(true, std::memory_order_relaxed); }
    bool is_cancelled() const noexcept override {
        return m_cancelled.load(std::memory_order_relaxed);
    }
private:
    std::atomic<bool> m_cancelled{false};
};

class NullCancellationToken : public ICancellationToken {
public:
    bool is_cancelled() const noexcept override { return false; }
};

// ---------------------------------------------------------------------------
// Callback interface — consumers implement this to receive parse events
// ---------------------------------------------------------------------------

class IParserCallbacks {
public:
    virtual ~IParserCallbacks() = default;

    virtual void on_begin(const std::filesystem::path& path) = 0;
    virtual void on_progress(const Progress& progress)      = 0;
    virtual void on_cell(const ParsedCell& cell)            = 0;
    virtual void on_net(const ParsedNet& net)               = 0;
    virtual void on_pin(const ParsedPin& pin)               = 0;
    virtual void on_geometry(const ParsedGeometry& geometry)= 0;
    virtual void on_error(const std::string& message, std::optional<int> line) = 0;
    virtual void on_end(bool success)                       = 0;
};

// ---------------------------------------------------------------------------
// Abstract parser interface
// ---------------------------------------------------------------------------

class IParser {
public:
    virtual ~IParser() = default;

    /// Human-readable format name (e.g. "DEF", "SPICE", "JSON").
    virtual std::string format_name() const = 0;

    /**
     * Parse a file and emit events via \p callbacks.
     *
     * The parser must check \p token.is_cancelled() at safe yield points
     * and return `false` if cancellation is requested.  Even on failure,
     * `on_end(false)` must be called before returning.
     *
     * @return true if the file was parsed to completion, false on error
     *         or cancellation.
     */
    virtual bool parse(const std::filesystem::path& path,
                       IParserCallbacks& callbacks,
                       const ICancellationToken& token) = 0;
};

} // namespace aegis::parsing
