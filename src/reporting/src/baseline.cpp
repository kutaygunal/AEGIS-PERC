#include "aegis/reporting/baseline.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace aegis::reporting {
namespace {

using json = nlohmann::json;

} // namespace

void to_json(json& j, const BaselineRecord& r)
{
    j = json::object();
    j["identity_key"] = r.identity_key;
    j["rule_id"] = r.rule_id;
    j["severity"] = aegis::rules::severity_to_string(r.severity);

    if (r.layer) j["layer"] = *r.layer;
    if (r.net_name) j["net_name"] = *r.net_name;
    if (r.pin_name) j["pin_name"] = *r.pin_name;
    if (r.device_name) j["device_name"] = *r.device_name;
    if (r.point) {
        json p = json::object();
        p["x"] = r.point->x;
        p["y"] = r.point->y;
        j["point"] = std::move(p);
    }
}

void from_json(const json& j, BaselineRecord& r)
{
    r.identity_key = j.value("identity_key", std::string{});
    r.rule_id = j.value("rule_id", std::string{});
    r.severity = aegis::rules::severity_from_string(j.value("severity", "error"));

    if (j.contains("layer") && j.at("layer").is_string()) r.layer = j.at("layer").get<std::string>();
    if (j.contains("net_name") && j.at("net_name").is_string()) r.net_name = j.at("net_name").get<std::string>();
    if (j.contains("pin_name") && j.at("pin_name").is_string()) r.pin_name = j.at("pin_name").get<std::string>();
    if (j.contains("device_name") && j.at("device_name").is_string()) r.device_name = j.at("device_name").get<std::string>();
    if (j.contains("point") && j.at("point").is_object()) {
        const auto& p = j.at("point");
        r.point = aegis::graph::Point{p.value("x", 0.0), p.value("y", 0.0)};
    }
}

void to_json(json& j, const BaselineFile& b)
{
    j = json::object();
    j["baseline_schema_version"] = b.schema_version;
    j["records"] = b.records;
    if (b.created_at_utc) j["created_at_utc"] = *b.created_at_utc;
    if (b.project_name) j["project_name"] = *b.project_name;
}

void from_json(const json& j, BaselineFile& b)
{
    const int version = j.value("baseline_schema_version", 0);
    if (version != BaselineFile::baseline_schema_version) {
        throw std::invalid_argument("Unsupported baseline_schema_version: " + std::to_string(version));
    }
    b.schema_version = version;
    if (j.contains("created_at_utc") && j.at("created_at_utc").is_string()) {
        b.created_at_utc = j.at("created_at_utc").get<std::string>();
    }
    if (j.contains("project_name") && j.at("project_name").is_string()) {
        b.project_name = j.at("project_name").get<std::string>();
    }
    if (!j.contains("records") || !j.at("records").is_array()) {
        throw std::invalid_argument("Baseline JSON missing 'records' array");
    }
    b.records = j.at("records").get<std::vector<BaselineRecord>>();
}

BaselineFile build_baseline(const std::vector<aegis::rules::Violation>& violations,
                            std::optional<std::string> project_name)
{
    BaselineFile baseline;
    baseline.project_name = std::move(project_name);
    baseline.records.reserve(violations.size());

    for (const auto& v : violations) {
        BaselineRecord r;
        r.identity_key = v.identity_key();
        r.rule_id = v.rule_id;
        r.severity = v.severity;
        r.layer = v.location.layer;
        r.net_name = v.location.net_name;
        r.pin_name = v.location.pin_name;
        r.device_name = v.location.device_name;
        r.point = v.location.point;
        baseline.records.push_back(std::move(r));
    }

    std::sort(baseline.records.begin(), baseline.records.end(),
              [](const BaselineRecord& a, const BaselineRecord& b) {
                  if (a.identity_key != b.identity_key) return a.identity_key < b.identity_key;
                  if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
                  return aegis::rules::severity_to_string(a.severity) < aegis::rules::severity_to_string(b.severity);
              });

    return baseline;
}

void write_baseline_json_file(const std::filesystem::path& path, const BaselineFile& baseline)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Unable to open baseline output file: " + path.string());
    }
    json j = baseline;
    out << j.dump(2);
}

BaselineFile read_baseline_json_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open baseline input file: " + path.string());
    }
    const json j = json::parse(in, nullptr, true, true);
    return j.get<BaselineFile>();
}

} // namespace aegis::reporting
