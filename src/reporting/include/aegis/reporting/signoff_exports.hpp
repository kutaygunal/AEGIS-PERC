#pragma once

#include "aegis/reporting/baseline.hpp"
#include "aegis/reporting/regression_diff.hpp"

#include <optional>
#include <string>

namespace aegis::reporting {

struct RunMetadata {
    // Informational only; exporters will include keys even when values are missing/unknown.
    std::optional<std::string> created_at_utc;
    std::optional<std::string> tool_version;
    std::optional<std::string> project_name;
    std::optional<std::string> rule_pack_id;
};

// Stable JSON exports used by CLI/UI/orchestration.
// Field ordering is deterministic (nlohmann::json default object ordering).
[[nodiscard]] std::string export_baseline_json(const BaselineFile& baseline, const RunMetadata& meta);
[[nodiscard]] std::string export_regression_diff_json(const RegressionDiffResult& diff, const RunMetadata& meta);

} // namespace aegis::reporting

