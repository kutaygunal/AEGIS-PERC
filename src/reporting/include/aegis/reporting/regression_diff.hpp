#pragma once

#include "aegis/reporting/baseline.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::reporting {

enum class DiffDiagnosticSeverity { Info, Warning, Error };

struct DiffDiagnostic {
    DiffDiagnosticSeverity severity = DiffDiagnosticSeverity::Info;
    std::string code;
    std::string message;
};

struct SeverityChange {
    std::string identity_key;
    std::string rule_id;
    aegis::rules::Severity from = aegis::rules::Severity::Error;
    aegis::rules::Severity to = aegis::rules::Severity::Error;

    bool operator==(const SeverityChange& o) const noexcept = default;
};

struct RegressionDiffSummary {
    std::size_t baseline_total = 0;
    std::size_t current_total = 0;
    std::size_t new_count = 0;
    std::size_t removed_count = 0;
    std::size_t unchanged_count = 0;
    std::size_t changed_severity_count = 0;

    // Aggregations (deterministic keys).
    std::map<std::string, std::size_t> new_by_rule;
    std::map<std::string, std::size_t> removed_by_rule;
    std::map<std::string, std::size_t> changed_by_rule;

    std::map<std::string, std::size_t> new_by_severity;     // severity_to_string
    std::map<std::string, std::size_t> removed_by_severity;
    std::map<std::string, std::size_t> changed_to_by_severity;

    bool operator==(const RegressionDiffSummary& o) const noexcept = default;
};

struct RegressionDiffResult {
    RegressionDiffSummary summary;
    std::vector<BaselineRecord> newly_introduced; // present in current, absent in baseline
    std::vector<BaselineRecord> removed;          // present in baseline, absent in current
    std::vector<BaselineRecord> unchanged;        // same severity
    std::vector<SeverityChange> changed_severity; // same identity_key, different severity
    std::vector<DiffDiagnostic> diagnostics;

    bool operator==(const RegressionDiffResult& o) const noexcept = default;
};

// Diff helpers
[[nodiscard]] RegressionDiffResult diff_baselines(const BaselineFile& baseline,
                                                  const BaselineFile& current);

[[nodiscard]] RegressionDiffResult diff_baseline_against_violations(
    const BaselineFile& baseline,
    const std::vector<aegis::rules::Violation>& current_violations,
    std::optional<std::string> project_name = std::nullopt);

} // namespace aegis::reporting

