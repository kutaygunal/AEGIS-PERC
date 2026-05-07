#pragma once

#include "aegis/graph/data_model.hpp"   // PropertyMap, Point

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::rules {

// ---------------------------------------------------------------------------
// Severity — type-safe classification
// ---------------------------------------------------------------------------

enum class Severity { Info, Warning, Error, Fatal };

[[nodiscard]] Severity severity_from_string(const std::string& s);
[[nodiscard]] std::string severity_to_string(Severity s);

// ---------------------------------------------------------------------------
// Structured location
// ---------------------------------------------------------------------------

struct ViolationLocation {
    std::optional<std::string> layer;
    std::optional<std::string> net_name;
    std::optional<std::string> pin_name;
    std::optional<std::string> device_name;
    std::optional<aegis::graph::Point>   point;

    bool operator==(const ViolationLocation& o) const noexcept = default;
};

// ADL JSON serialization
void to_json(nlohmann::json& j, const ViolationLocation& loc);
void from_json(const nlohmann::json& j, ViolationLocation& loc);

// ---------------------------------------------------------------------------
// Violation — expanded record
// ---------------------------------------------------------------------------

struct Violation {
    std::string id;            // stable identifier for linking / clustering
    std::string rule_id;
    Severity    severity = Severity::Error;
    std::string message;
    ViolationLocation location;
    aegis::graph::PropertyMap metadata;

    Violation() = default;

    // Convenience: message only (no location)
    Violation(std::string rid, Severity sev, std::string msg)
        : rule_id(std::move(rid)), severity(sev), message(std::move(msg)) {}

    // Convenience: flat location name → populates net_name
    Violation(std::string rid, Severity sev, std::string msg,
              std::string loc_name)
        : rule_id(std::move(rid)), severity(sev), message(std::move(msg)) {
        location.net_name = std::move(loc_name);
    }

    // Full constructor
    Violation(std::string rid, Severity sev, std::string msg,
              ViolationLocation loc)
        : rule_id(std::move(rid)), severity(sev), message(std::move(msg)),
          location(std::move(loc)) {}

    bool operator==(const Violation& o) const noexcept = default;
    bool operator!=(const Violation& o) const noexcept = default;
};

// ADL JSON serialization
void to_json(nlohmann::json& j, const Violation& v);
void from_json(const nlohmann::json& j, Violation& v);

// ---------------------------------------------------------------------------
// ViolationCollection — filtering, grouping, pagination, JSON
// ---------------------------------------------------------------------------

class ViolationCollection {
public:
    ViolationCollection() = default;
    explicit ViolationCollection(std::vector<Violation> violations)
        : m_violations(std::move(violations)) {}

    // -------------------------------------------------------------------
    // Basic queries
    // -------------------------------------------------------------------
    [[nodiscard]] std::size_t size() const noexcept { return m_violations.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_violations.empty(); }
    void clear() { m_violations.clear(); }

    void add(Violation v);
    void append(const ViolationCollection& other);
    void append(std::vector<Violation> violations);

    [[nodiscard]] const Violation& operator[](std::size_t i) const;
    [[nodiscard]] const std::vector<Violation>& violations() const noexcept;

    [[nodiscard]] auto begin() const { return m_violations.begin(); }
    [[nodiscard]] auto end()   const { return m_violations.end(); }

    // -------------------------------------------------------------------
    // Filtering
    // -------------------------------------------------------------------
    [[nodiscard]] ViolationCollection filter_by_severity(Severity s) const;
    [[nodiscard]] ViolationCollection filter_by_rule(const std::string& rule_id) const;
    [[nodiscard]] ViolationCollection filter_by_predicate(
        std::function<bool(const Violation&)> pred) const;

    // -------------------------------------------------------------------
    // Grouping
    // -------------------------------------------------------------------
    [[nodiscard]] std::map<std::string, ViolationCollection> group_by_rule() const;
    [[nodiscard]] std::map<Severity, ViolationCollection> group_by_severity() const;

    // -------------------------------------------------------------------
    // Pagination
    // -------------------------------------------------------------------
    [[nodiscard]] ViolationCollection paginate(
        std::size_t offset, std::size_t limit) const;

    // -------------------------------------------------------------------
    // Sorting (in-place)
    // -------------------------------------------------------------------
    void sort_by_severity();   // Fatal > Error > Warning > Info
    void sort_by_rule_id();
    void sort_by_message();

    // -------------------------------------------------------------------
    // JSON
    // -------------------------------------------------------------------
    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static ViolationCollection from_json(const nlohmann::json& j);

    [[nodiscard]] std::string to_json_string(int indent = 4) const;
    [[nodiscard]] static ViolationCollection from_json_string(const std::string& s);

    // -------------------------------------------------------------------
    // Equality
    // -------------------------------------------------------------------
    [[nodiscard]] bool operator==(const ViolationCollection& o) const = default;

private:
    std::vector<Violation> m_violations;
};

// Convenience factory (for backwards-compatibility style calls)
inline Violation make_violation(std::string rule_id, Severity severity,
                                std::string message) {
    return Violation(std::move(rule_id), severity, std::move(message));
}

inline Violation make_violation(std::string rule_id, Severity severity,
                                std::string message, std::string location_name) {
    return Violation(std::move(rule_id), severity, std::move(message),
                     std::move(location_name));
}

inline Violation make_violation(std::string rule_id, Severity severity,
                                std::string message, ViolationLocation location) {
    return Violation(std::move(rule_id), severity, std::move(message),
                     std::move(location));
}

} // namespace aegis::rules
