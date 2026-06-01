#include <catch2/catch_test_macros.hpp>

#include "aegis/reporting/signoff_exports.hpp"

#include <nlohmann/json.hpp>

using aegis::reporting::BaselineFile;
using aegis::reporting::BaselineRecord;
using aegis::reporting::RegressionDiffResult;
using aegis::reporting::RunMetadata;
using aegis::rules::Severity;

TEST_CASE("Reporting exports baseline JSON with run_metadata and stable structure",
          "[Reporting][S1-007][fast]")
{
    BaselineFile baseline;
    baseline.records = {BaselineRecord{"k1", "R1", Severity::Error}};

    RunMetadata meta;
    meta.created_at_utc = "2026-05-22T00:00:00Z";
    meta.tool_version = "test";
    meta.project_name = "P";
    meta.rule_pack_id = "rules-1";

    const auto out1 = aegis::reporting::export_baseline_json(baseline, meta);
    const auto out2 = aegis::reporting::export_baseline_json(baseline, meta);
    CHECK(out1 == out2);

    const auto j = nlohmann::json::parse(out1);
    CHECK(j.at("run_metadata").at("tool_version") == "test");
    CHECK(j.at("baseline_schema_version") == 1);
    CHECK(j.at("records").is_array());
    CHECK(j.at("records").size() == 1);
    CHECK(j.at("records")[0].at("identity_key") == "k1");
}

TEST_CASE("Reporting exports regression diff JSON with summary and stable structure",
          "[Reporting][S1-007][fast]")
{
    RegressionDiffResult diff;
    diff.summary.new_count = 1;
    diff.summary.new_by_rule["R1"] = 1;
    diff.newly_introduced.push_back(BaselineRecord{"k1", "R1", Severity::Error});

    RunMetadata meta;
    meta.created_at_utc = "2026-05-22T00:00:00Z";
    meta.tool_version = "test";

    const auto out = aegis::reporting::export_regression_diff_json(diff, meta);
    const auto j = nlohmann::json::parse(out);
    CHECK(j.at("run_metadata").at("tool_version") == "test");
    CHECK(j.at("summary").at("new_count") == 1);
    CHECK(j.at("new").is_array());
    CHECK(j.at("new").size() == 1);
    CHECK(j.at("new")[0].at("identity_key") == "k1");
}

