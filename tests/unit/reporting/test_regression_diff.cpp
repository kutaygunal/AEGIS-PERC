#include <catch2/catch_test_macros.hpp>

#include "aegis/reporting/regression_diff.hpp"

using aegis::reporting::BaselineFile;
using aegis::reporting::BaselineRecord;
using aegis::reporting::diff_baselines;
using aegis::rules::Severity;

TEST_CASE("RegressionDiff computes new removed unchanged and changed-severity sets deterministically",
          "[RegressionDiff][S1-005][fast]")
{
    BaselineFile base;
    base.records = {
        BaselineRecord{"k1", "R1", Severity::Error},
        BaselineRecord{"k2", "R1", Severity::Warning},
        BaselineRecord{"k3", "R2", Severity::Info}
    };

    BaselineFile cur;
    cur.records = {
        BaselineRecord{"k1", "R1", Severity::Error},     // unchanged
        BaselineRecord{"k2", "R1", Severity::Error},     // changed severity
        BaselineRecord{"k4", "R3", Severity::Warning}    // new
        // k3 removed
    };

    const auto diff = diff_baselines(base, cur);
    CHECK(diff.summary.baseline_total == 3);
    CHECK(diff.summary.current_total == 3);
    CHECK(diff.summary.new_count == 1);
    CHECK(diff.summary.removed_count == 1);
    CHECK(diff.summary.unchanged_count == 1);
    CHECK(diff.summary.changed_severity_count == 1);

    REQUIRE(diff.newly_introduced.size() == 1);
    CHECK(diff.newly_introduced[0].identity_key == "k4");

    REQUIRE(diff.removed.size() == 1);
    CHECK(diff.removed[0].identity_key == "k3");

    REQUIRE(diff.unchanged.size() == 1);
    CHECK(diff.unchanged[0].identity_key == "k1");

    REQUIRE(diff.changed_severity.size() == 1);
    CHECK(diff.changed_severity[0].identity_key == "k2");
    CHECK(diff.changed_severity[0].from == Severity::Warning);
    CHECK(diff.changed_severity[0].to == Severity::Error);

    CHECK(diff.summary.new_by_rule.at("R3") == 1);
    CHECK(diff.summary.removed_by_rule.at("R2") == 1);
    CHECK(diff.summary.changed_by_rule.at("R1") == 1);
    CHECK(diff.summary.changed_to_by_severity.at("error") == 1);
}

TEST_CASE("RegressionDiff handles duplicate identities by choosing canonical record and emitting diagnostics",
          "[RegressionDiff][S1-005][fast]")
{
    BaselineFile base;
    base.records = {
        BaselineRecord{"dup", "R1", Severity::Warning},
        BaselineRecord{"dup", "R1", Severity::Error} // duplicate, more severe
    };

    BaselineFile cur;
    cur.records = {
        BaselineRecord{"dup", "R1", Severity::Error}
    };

    const auto diff = diff_baselines(base, cur);
    CHECK(diff.summary.unchanged_count == 1);
    CHECK_FALSE(diff.diagnostics.empty());
}

TEST_CASE("RegressionDiff ignores records missing identity_key with warning diagnostics",
          "[RegressionDiff][S1-005][fast]")
{
    BaselineFile base;
    base.records = {
        BaselineRecord{"", "R1", Severity::Error},
        BaselineRecord{"k1", "R1", Severity::Error}
    };
    BaselineFile cur;
    cur.records = {BaselineRecord{"k1", "R1", Severity::Error}};

    const auto diff = diff_baselines(base, cur);
    CHECK(diff.summary.unchanged_count == 1);
    CHECK_FALSE(diff.diagnostics.empty());
}

