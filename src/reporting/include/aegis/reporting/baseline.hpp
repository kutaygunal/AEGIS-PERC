#pragma once

#include "aegis/rules/violation.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::reporting {

struct BaselineRecord {
    std::string identity_key;
    std::string rule_id;
    aegis::rules::Severity severity = aegis::rules::Severity::Error;

    // Optional human-facing context (not used for matching/diffing).
    std::optional<std::string> layer;
    std::optional<std::string> net_name;
    std::optional<std::string> pin_name;
    std::optional<std::string> device_name;
    std::optional<aegis::graph::Point> point;

    bool operator==(const BaselineRecord& o) const noexcept = default;
};

struct BaselineFile {
    static constexpr int baseline_schema_version = 1;

    int schema_version = baseline_schema_version;
    std::optional<std::string> created_at_utc; // informational only
    std::optional<std::string> project_name;
    std::vector<BaselineRecord> records;

    bool operator==(const BaselineFile& o) const noexcept = default;
};

// Build a baseline snapshot from violations (deterministic order).
[[nodiscard]] BaselineFile build_baseline(const std::vector<aegis::rules::Violation>& violations,
                                          std::optional<std::string> project_name = std::nullopt);

// Serialize/deserialize. Mismatched schema versions throw std::invalid_argument.
void write_baseline_json_file(const std::filesystem::path& path, const BaselineFile& baseline);
[[nodiscard]] BaselineFile read_baseline_json_file(const std::filesystem::path& path);

} // namespace aegis::reporting

