#include "aegis/reporting/regression_diff.hpp"

#include <algorithm>
#include <unordered_map>

namespace aegis::reporting {
namespace {

int severity_rank(aegis::rules::Severity s)
{
    // Lower is "more severe".
    using S = aegis::rules::Severity;
    switch (s) {
    case S::Fatal: return 0;
    case S::Error: return 1;
    case S::Warning: return 2;
    case S::Info: return 3;
    }
    return 99;
}

bool record_less(const BaselineRecord& a, const BaselineRecord& b)
{
    if (a.identity_key != b.identity_key) return a.identity_key < b.identity_key;
    if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
    return severity_rank(a.severity) < severity_rank(b.severity);
}

BaselineRecord pick_canonical(const BaselineRecord& a, const BaselineRecord& b)
{
    // Deterministic: keep the "worse" severity; tie-break by rule_id then full ordering.
    if (severity_rank(a.severity) != severity_rank(b.severity)) {
        return (severity_rank(a.severity) < severity_rank(b.severity)) ? a : b;
    }
    if (a.rule_id != b.rule_id) {
        return (a.rule_id < b.rule_id) ? a : b;
    }
    return record_less(a, b) ? a : b;
}

std::unordered_map<std::string, BaselineRecord> index_by_identity(const BaselineFile& file,
                                                                  std::vector<DiffDiagnostic>& diags,
                                                                  std::string_view side_label)
{
    std::unordered_map<std::string, BaselineRecord> idx;
    idx.reserve(file.records.size());

    for (const auto& r : file.records) {
        if (r.identity_key.empty()) {
            diags.push_back(DiffDiagnostic{DiffDiagnosticSeverity::Warning,
                                           "DIFF_MISSING_IDENTITY_KEY",
                                           std::string(side_label) + " baseline record missing identity_key"});
            continue;
        }
        auto it = idx.find(r.identity_key);
        if (it == idx.end()) {
            idx.emplace(r.identity_key, r);
            continue;
        }
        // Duplicate identity in file: choose canonical and record diagnostic.
        diags.push_back(DiffDiagnostic{DiffDiagnosticSeverity::Warning,
                                       "DIFF_DUPLICATE_IDENTITY_KEY",
                                       std::string(side_label) + " baseline contains duplicate identity_key '" + r.identity_key + "'; using canonical record"});
        it->second = pick_canonical(it->second, r);
    }
    return idx;
}

void bump(std::map<std::string, std::size_t>& map, const std::string& key)
{
    ++map[key];
}

} // namespace

RegressionDiffResult diff_baselines(const BaselineFile& baseline, const BaselineFile& current)
{
    RegressionDiffResult result;
    result.summary.baseline_total = baseline.records.size();
    result.summary.current_total = current.records.size();

    auto baseline_idx = index_by_identity(baseline, result.diagnostics, "baseline");
    auto current_idx = index_by_identity(current, result.diagnostics, "current");

    // Build sorted identity lists for deterministic iteration.
    std::vector<std::string> all_keys;
    all_keys.reserve(baseline_idx.size() + current_idx.size());
    for (const auto& [k, _] : baseline_idx) all_keys.push_back(k);
    for (const auto& [k, _] : current_idx) all_keys.push_back(k);
    std::sort(all_keys.begin(), all_keys.end());
    all_keys.erase(std::unique(all_keys.begin(), all_keys.end()), all_keys.end());

    for (const auto& key : all_keys) {
        const auto it_base = baseline_idx.find(key);
        const auto it_curr = current_idx.find(key);

        if (it_base == baseline_idx.end() && it_curr != current_idx.end()) {
            result.newly_introduced.push_back(it_curr->second);
            ++result.summary.new_count;
            bump(result.summary.new_by_rule, it_curr->second.rule_id);
            bump(result.summary.new_by_severity, aegis::rules::severity_to_string(it_curr->second.severity));
            continue;
        }
        if (it_base != baseline_idx.end() && it_curr == current_idx.end()) {
            result.removed.push_back(it_base->second);
            ++result.summary.removed_count;
            bump(result.summary.removed_by_rule, it_base->second.rule_id);
            bump(result.summary.removed_by_severity, aegis::rules::severity_to_string(it_base->second.severity));
            continue;
        }
        if (it_base == baseline_idx.end() || it_curr == current_idx.end()) {
            continue;
        }

        const auto& b = it_base->second;
        const auto& c = it_curr->second;
        if (b.severity == c.severity) {
            result.unchanged.push_back(c);
            ++result.summary.unchanged_count;
        } else {
            SeverityChange change;
            change.identity_key = key;
            change.rule_id = !c.rule_id.empty() ? c.rule_id : b.rule_id;
            change.from = b.severity;
            change.to = c.severity;
            result.changed_severity.push_back(std::move(change));
            ++result.summary.changed_severity_count;
            bump(result.summary.changed_by_rule, !c.rule_id.empty() ? c.rule_id : b.rule_id);
            bump(result.summary.changed_to_by_severity, aegis::rules::severity_to_string(c.severity));
        }
    }

    // Deterministic ordering of outputs.
    std::sort(result.newly_introduced.begin(), result.newly_introduced.end(), record_less);
    std::sort(result.removed.begin(), result.removed.end(), record_less);
    std::sort(result.unchanged.begin(), result.unchanged.end(), record_less);
    std::sort(result.changed_severity.begin(), result.changed_severity.end(),
              [](const SeverityChange& a, const SeverityChange& b) {
                  if (a.identity_key != b.identity_key) return a.identity_key < b.identity_key;
                  if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
                  if (a.from != b.from) return severity_rank(a.from) < severity_rank(b.from);
                  return severity_rank(a.to) < severity_rank(b.to);
              });

    return result;
}

RegressionDiffResult diff_baseline_against_violations(const BaselineFile& baseline,
                                                      const std::vector<aegis::rules::Violation>& current_violations,
                                                      std::optional<std::string> project_name)
{
    const auto current = build_baseline(current_violations, std::move(project_name));
    return diff_baselines(baseline, current);
}

} // namespace aegis::reporting

