#include <catch2/catch_test_macros.hpp>

#include "aegis/rules/violation.hpp"
#include "aegis/rules/waivers.hpp"
#include "aegis/storage/imported_design_session.hpp"

#include <filesystem>

using namespace aegis::rules;
using namespace aegis::storage;
namespace fs = std::filesystem;

TEST_CASE("WaiverApplication identity_key waiver marks matching violations as waived",
          "[WaiverApplication][S1-003][fast]")
{
    ViolationLocation loc;
    loc.net_name = "n1";
    Violation v{"R1", Severity::Error, "msg", loc};

    WaiverEntry w;
    w.identity_key = v.identity_key();
    w.comment = "known issue";

    std::vector<Violation> violations{v};
    const auto summary = apply_waivers_in_place(violations, std::vector<WaiverEntry>{w});

    REQUIRE(summary.total_violations == 1);
    REQUIRE(summary.waived_violations == 1);
    REQUIRE(summary.active_violations == 0);
    CHECK(violations[0].metadata.get<bool>("waived").value_or(false));
    CHECK(violations[0].metadata.get<std::string>("waiver_comment").value_or("") == "known issue");
}

TEST_CASE("WaiverApplication rule_id-only waiver marks all rule findings as waived",
          "[WaiverApplication][S1-003][fast]")
{
    std::vector<Violation> violations;
    violations.push_back(Violation{"R1", Severity::Error, "a", "n1"});
    violations.push_back(Violation{"R1", Severity::Warning, "b", "n2"});
    violations.push_back(Violation{"R2", Severity::Error, "c", "n3"});

    WaiverEntry w;
    w.rule_id = "R1";

    const auto summary = apply_waivers_in_place(violations, std::vector<WaiverEntry>{w});
    REQUIRE(summary.total_violations == 3);
    REQUIRE(summary.waived_violations == 2);
    REQUIRE(summary.active_violations == 1);
    CHECK(violations[0].metadata.get<bool>("waived").value_or(false));
    CHECK(violations[1].metadata.get<bool>("waived").value_or(false));
    CHECK_FALSE(violations[2].metadata.get<bool>("waived").value_or(false));
}

TEST_CASE("WaiverApplication works with imported-design-derived identifiers",
          "[WaiverApplication][S1-003][fast]")
{
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);
    REQUIRE_FALSE(session.objects().empty());

    const auto& obj = session.objects().front();
    const std::string name = obj.display_name.empty() ? obj.name : obj.display_name;

    ViolationLocation loc;
    loc.net_name = name;
    Violation v{"IMPORTED-TEST", Severity::Error, "msg", loc};

    WaiverEntry w;
    w.identity_key = v.identity_key();
    w.owner = "integration";

    std::vector<Violation> violations{v};
    const auto summary = apply_waivers_in_place(violations, std::vector<WaiverEntry>{w});
    REQUIRE(summary.waived_violations == 1);
    CHECK(violations[0].metadata.get<std::string>("waiver_owner").value_or("") == "integration");
}

