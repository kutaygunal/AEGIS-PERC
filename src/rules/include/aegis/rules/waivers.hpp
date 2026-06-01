#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aegis::rules {

struct Violation;

enum class WaiverDiagnosticSeverity { Info, Warning, Error };

struct WaiverDiagnostic {
    WaiverDiagnosticSeverity severity = WaiverDiagnosticSeverity::Info;
    std::string code;
    std::string message;
    std::optional<std::size_t> source_line;
    std::optional<std::size_t> source_row;
};

struct WaiverEntry {
    // Primary matching keys (v1): prefer identity_key when present.
    std::optional<std::string> identity_key;
    std::optional<std::string> rule_id;

    // Optional audit fields (not enforced by core logic yet).
    std::optional<std::string> comment;
    std::optional<std::string> owner;
    std::optional<std::string> expires_on; // ISO-like string (kept as string for deterministic parsing)
};

struct WaiverParseResult {
    std::vector<WaiverEntry> waivers;
    std::vector<WaiverDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

struct WaiverApplicationSummary {
    std::size_t total_violations = 0;
    std::size_t waived_violations = 0;
    std::size_t active_violations = 0;
};

// Applies waivers by marking violations with metadata:
// - waived: bool
// - waiver_match: "identity_key" | "rule_id"
// - waiver_comment / waiver_owner / waiver_expires_on when present
//
// The input list is modified in-place so downstream export paths keep an audit trail.
[[nodiscard]] WaiverApplicationSummary apply_waivers_in_place(
    std::vector<Violation>& violations,
    const std::vector<WaiverEntry>& waivers);

// Format-specific parsers.
[[nodiscard]] WaiverParseResult parse_waivers_csv_file(const std::filesystem::path& path);
[[nodiscard]] WaiverParseResult parse_waivers_json_file(const std::filesystem::path& path);
[[nodiscard]] WaiverParseResult parse_waivers_yaml_file(const std::filesystem::path& path);

} // namespace aegis::rules
