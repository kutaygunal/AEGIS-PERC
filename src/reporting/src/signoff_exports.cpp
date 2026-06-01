#include "aegis/reporting/signoff_exports.hpp"

#include <nlohmann/json.hpp>

namespace aegis::reporting {
namespace {

using json = nlohmann::json;

json metadata_json(const RunMetadata& meta)
{
    json j = json::object();
    j["created_at_utc"] = meta.created_at_utc.value_or("unknown");
    j["tool_version"] = meta.tool_version.value_or("unknown");
    if (meta.project_name.has_value()) {
        j["project_name"] = meta.project_name.value();
    }
    if (meta.rule_pack_id.has_value()) {
        j["rule_pack_id"] = meta.rule_pack_id.value();
    }
    return j;
}

json baseline_record_json(const BaselineRecord& r)
{
    json j = json::object();
    j["identity_key"] = r.identity_key;
    j["rule_id"] = r.rule_id;
    j["severity"] = aegis::rules::severity_to_string(r.severity);
    if (r.layer) j["layer"] = *r.layer;
    if (r.net_name) j["net_name"] = *r.net_name;
    if (r.pin_name) j["pin_name"] = *r.pin_name;
    if (r.device_name) j["device_name"] = *r.device_name;
    if (r.point) j["point"] = {{"x", r.point->x}, {"y", r.point->y}};
    return j;
}

json baseline_json(const BaselineFile& baseline, const RunMetadata& meta)
{
    json j = json::object();
    j["run_metadata"] = metadata_json(meta);
    j["baseline_schema_version"] = baseline.schema_version;
    if (baseline.project_name.has_value()) {
        j["project_name"] = baseline.project_name.value();
    }
    json records = json::array();
    for (const auto& r : baseline.records) {
        records.push_back(baseline_record_json(r));
    }
    j["records"] = std::move(records);
    return j;
}

json severity_change_json(const SeverityChange& c)
{
    return json{
        {"identity_key", c.identity_key},
        {"rule_id", c.rule_id},
        {"from", aegis::rules::severity_to_string(c.from)},
        {"to", aegis::rules::severity_to_string(c.to)}
    };
}

json diff_json(const RegressionDiffResult& diff, const RunMetadata& meta)
{
    json j = json::object();
    j["run_metadata"] = metadata_json(meta);
    j["summary"] = {
        {"baseline_total", diff.summary.baseline_total},
        {"current_total", diff.summary.current_total},
        {"new_count", diff.summary.new_count},
        {"removed_count", diff.summary.removed_count},
        {"unchanged_count", diff.summary.unchanged_count},
        {"changed_severity_count", diff.summary.changed_severity_count},
        {"new_by_rule", diff.summary.new_by_rule},
        {"removed_by_rule", diff.summary.removed_by_rule},
        {"changed_by_rule", diff.summary.changed_by_rule},
        {"new_by_severity", diff.summary.new_by_severity},
        {"removed_by_severity", diff.summary.removed_by_severity},
        {"changed_to_by_severity", diff.summary.changed_to_by_severity}
    };

    auto records_to_json = [](const std::vector<BaselineRecord>& records) {
        json arr = json::array();
        for (const auto& r : records) {
            json rec = json::object();
            rec["identity_key"] = r.identity_key;
            rec["rule_id"] = r.rule_id;
            rec["severity"] = aegis::rules::severity_to_string(r.severity);
            arr.push_back(std::move(rec));
        }
        return arr;
    };

    j["new"] = records_to_json(diff.newly_introduced);
    j["removed"] = records_to_json(diff.removed);
    j["unchanged"] = records_to_json(diff.unchanged);

    json changed = json::array();
    for (const auto& c : diff.changed_severity) {
        changed.push_back(severity_change_json(c));
    }
    j["changed_severity"] = std::move(changed);

    json diags = json::array();
    for (const auto& d : diff.diagnostics) {
        diags.push_back({{"code", d.code}, {"message", d.message}});
    }
    j["diagnostics"] = std::move(diags);

    return j;
}

} // namespace

std::string export_baseline_json(const BaselineFile& baseline, const RunMetadata& meta)
{
    return baseline_json(baseline, meta).dump(2);
}

std::string export_regression_diff_json(const RegressionDiffResult& diff, const RunMetadata& meta)
{
    return diff_json(diff, meta).dump(2);
}

} // namespace aegis::reporting

