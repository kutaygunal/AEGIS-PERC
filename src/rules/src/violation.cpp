#include "aegis/rules/violation.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace aegis::rules {

// ---------------------------------------------------------------------------
// Severity helpers
// ---------------------------------------------------------------------------

Severity severity_from_string(const std::string& s) {
    std::string lowered;
    lowered.reserve(s.size());
    for (char c : s) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lowered == "info")    return Severity::Info;
    if (lowered == "warning") return Severity::Warning;
    if (lowered == "error")   return Severity::Error;
    if (lowered == "fatal")   return Severity::Fatal;
    throw std::invalid_argument("Unknown severity string: '" + s + "'");
}

std::string severity_to_string(Severity s) {
    switch (s) {
        case Severity::Info:    return "info";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
        case Severity::Fatal:   return "fatal";
    }
    return "unknown";
}

static int severity_rank(Severity s) {
    switch (s) {
        case Severity::Fatal:   return 0;
        case Severity::Error:   return 1;
        case Severity::Warning: return 2;
        case Severity::Info:    return 3;
    }
    return 99;
}

// ---------------------------------------------------------------------------
// ViolationLocation JSON (ADL)
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& j, const ViolationLocation& loc) {
    j = nlohmann::json::object();
    if (loc.layer)     j["layer"]      = *loc.layer;
    if (loc.net_name)  j["net_name"]   = *loc.net_name;
    if (loc.pin_name)  j["pin_name"]   = *loc.pin_name;
    if (loc.device_name) j["device_name"] = *loc.device_name;
    if (loc.point) {
        j["point"] = nlohmann::json{{"x", loc.point->x}, {"y", loc.point->y}};
    }
}

void from_json(const nlohmann::json& j, ViolationLocation& loc) {
    if (j.contains("layer") && !j.at("layer").is_null())
        loc.layer = j.at("layer").get<std::string>();
    if (j.contains("net_name") && !j.at("net_name").is_null())
        loc.net_name = j.at("net_name").get<std::string>();
    if (j.contains("pin_name") && !j.at("pin_name").is_null())
        loc.pin_name = j.at("pin_name").get<std::string>();
    if (j.contains("device_name") && !j.at("device_name").is_null())
        loc.device_name = j.at("device_name").get<std::string>();
    if (j.contains("point") && j.at("point").is_object()) {
        const auto& p = j.at("point");
        loc.point = aegis::graph::Point{p.value("x", 0.0), p.value("y", 0.0)};
    }
}

// ---------------------------------------------------------------------------
// Violation JSON (ADL)
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& j, const Violation& v) {
    j = {
        {"id", v.id},
        {"rule_id", v.rule_id},
        {"severity", severity_to_string(v.severity)},
        {"message", v.message},
        {"location", v.location},
        {"metadata", v.metadata}
    };
}

void from_json(const nlohmann::json& j, Violation& v) {
    v.id       = j.value("id", std::string{});
    v.rule_id  = j.value("rule_id", std::string{});
    v.severity = severity_from_string(j.value("severity", "error"));
    v.message  = j.value("message", std::string{});
    if (j.contains("location") && j.at("location").is_object()) {
        aegis::rules::from_json(j.at("location"), v.location);
    } else {
        v.location = ViolationLocation{};
    }
    if (j.contains("metadata") && j.at("metadata").is_object()) {
        aegis::graph::from_json(j.at("metadata"), v.metadata);
    } else {
        v.metadata = aegis::graph::PropertyMap{};
    }
}

// ---------------------------------------------------------------------------
// ViolationCollection
// ---------------------------------------------------------------------------

void ViolationCollection::add(Violation v) {
    m_violations.push_back(std::move(v));
}

void ViolationCollection::append(const ViolationCollection& other) {
    m_violations.insert(m_violations.end(), other.begin(), other.end());
}

void ViolationCollection::append(std::vector<Violation> violations) {
    m_violations.insert(m_violations.end(),
                        std::make_move_iterator(violations.begin()),
                        std::make_move_iterator(violations.end()));
}

const Violation& ViolationCollection::operator[](std::size_t i) const {
    if (i >= m_violations.size()) {
        throw std::out_of_range("ViolationCollection index out of range");
    }
    return m_violations[i];
}

const std::vector<Violation>& ViolationCollection::violations() const noexcept {
    return m_violations;
}

ViolationCollection ViolationCollection::filter_by_severity(Severity s) const {
    ViolationCollection result;
    for (const auto& v : m_violations) {
        if (v.severity == s) result.add(v);
    }
    return result;
}

ViolationCollection ViolationCollection::filter_by_rule(
    const std::string& rule_id) const {
    ViolationCollection result;
    for (const auto& v : m_violations) {
        if (v.rule_id == rule_id) result.add(v);
    }
    return result;
}

ViolationCollection ViolationCollection::filter_by_predicate(
    std::function<bool(const Violation&)> pred) const {
    ViolationCollection result;
    for (const auto& v : m_violations) {
        if (pred(v)) result.add(v);
    }
    return result;
}

std::map<std::string, ViolationCollection> ViolationCollection::group_by_rule() const {
    std::map<std::string, ViolationCollection> groups;
    for (const auto& v : m_violations) {
        groups[v.rule_id].add(v);
    }
    return groups;
}

std::map<Severity, ViolationCollection> ViolationCollection::group_by_severity() const {
    std::map<Severity, ViolationCollection> groups;
    for (const auto& v : m_violations) {
        groups[v.severity].add(v);
    }
    return groups;
}

ViolationCollection ViolationCollection::paginate(
    std::size_t offset, std::size_t limit) const {
    ViolationCollection result;
    if (offset >= m_violations.size() || limit == 0) return result;
    std::size_t end = std::min(offset + limit, m_violations.size());
    for (std::size_t i = offset; i < end; ++i) {
        result.add(m_violations[i]);
    }
    return result;
}

void ViolationCollection::sort_by_severity() {
    std::sort(m_violations.begin(), m_violations.end(),
              [](const Violation& a, const Violation& b) {
                  return severity_rank(a.severity) < severity_rank(b.severity);
              });
}

void ViolationCollection::sort_by_rule_id() {
    std::sort(m_violations.begin(), m_violations.end(),
              [](const Violation& a, const Violation& b) {
                  return a.rule_id < b.rule_id;
              });
}

void ViolationCollection::sort_by_message() {
    std::sort(m_violations.begin(), m_violations.end(),
              [](const Violation& a, const Violation& b) {
                  return a.message < b.message;
              });
}

nlohmann::json ViolationCollection::to_json() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& v : m_violations) {
        j.push_back(v);
    }
    return j;
}

ViolationCollection ViolationCollection::from_json(const nlohmann::json& j) {
    ViolationCollection result;
    if (!j.is_array()) {
        throw std::invalid_argument("ViolationCollection JSON must be an array");
    }
    for (const auto& item : j) {
        result.add(item.get<Violation>());
    }
    return result;
}

std::string ViolationCollection::to_json_string(int indent) const {
    return to_json().dump(indent);
}

ViolationCollection ViolationCollection::from_json_string(const std::string& s) {
    return from_json(nlohmann::json::parse(s));
}

} // namespace aegis::rules
