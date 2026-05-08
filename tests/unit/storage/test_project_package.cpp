#include <catch2/catch_test_macros.hpp>
#include "aegis/storage/project_package.hpp"

using namespace aegis::storage;

TEST_CASE("ProjectPackage loads manifest JSON and builds normalized references", "[storage][p6-002][ProjectPackage]")
{
    const std::string manifest = R"({
        "manifest_version": 1,
        "project": {
            "name": "FalconCPU",
            "description": "CPU block",
            "customer": "ExampleSemi",
            "design_stage": "place_route"
        },
        "artifacts": {
            "technology": [
                {"id": "tech-1", "path": "layout/technology.lef", "role": "lef"}
            ],
            "layout": [
                {"id": "layout-1", "path": "layout/top.def", "role": "def"}
            ],
            "netlist": [
                {"id": "netlist-1", "path": "netlist/top.v", "role": "verilog"}
            ],
            "rules": [
                {"id": "rules-1", "path": "rules/aegis_rules.yaml", "role": "aegis_rule_pack"}
            ],
            "power": [
                {"id": "power-1", "path": "power/power_domains.csv", "role": "power_domains_csv", "optional": true}
            ],
            "current": [
                {"id": "current-1", "path": "reports/current_report.csv", "role": "current_csv", "optional": true}
            ],
            "waivers": [
                {"id": "waiver-1", "path": "reports/waivers.csv", "role": "waiver_csv", "optional": true}
            ],
            "external_reports": [
                {"id": "report-1", "path": "reports/erc_report.rpt", "role": "imported_report", "optional": true}
            ]
        },
        "validation": {
            "status": "warning"
        },
        "diagnostics": [
            {"severity": "warning", "code": "MULTI_NETLIST_CANDIDATE", "message": "More than one netlist found", "artifact_id": "netlist-1"}
        ]
    })";

    const ProjectPackage pkg = ProjectPackage::from_manifest_json(manifest);

    REQUIRE(pkg.manifest_version() == 1);
    REQUIRE(pkg.project().name == "FalconCPU");
    REQUIRE(pkg.project().customer == "ExampleSemi");
    REQUIRE(pkg.validation_status() == ValidationStatus::Warning);
    REQUIRE(pkg.artifacts().size() == 8);

    REQUIRE(pkg.normalized().technology_artifact_ids == std::vector<std::string>{"tech-1"});
    REQUIRE(pkg.normalized().layout_artifact_ids == std::vector<std::string>{"layout-1"});
    REQUIRE(pkg.normalized().netlist_artifact_ids == std::vector<std::string>{"netlist-1"});
    REQUIRE(pkg.normalized().rule_artifact_ids == std::vector<std::string>{"rules-1"});
    REQUIRE(pkg.normalized().power_artifact_ids == std::vector<std::string>{"power-1"});
    REQUIRE(pkg.normalized().current_artifact_ids == std::vector<std::string>{"current-1"});
    REQUIRE(pkg.normalized().waiver_artifact_ids == std::vector<std::string>{"waiver-1"});
    REQUIRE(pkg.normalized().external_report_artifact_ids == std::vector<std::string>{"report-1"});

    REQUIRE(pkg.diagnostics().size() == 1);
    REQUIRE(pkg.diagnostics().front().code == "MULTI_NETLIST_CANDIDATE");
    REQUIRE(pkg.find_artifact_by_id("power-1") != nullptr);
    REQUIRE(pkg.find_artifact_by_id("missing") == nullptr);
}

TEST_CASE("ProjectPackage serialization round-trip is deterministic for manifest-backed data", "[storage][p6-002][ProjectPackage]")
{
    ProjectPackage pkg;
    pkg.set_manifest_version(1);
    pkg.project().name = "FalconCPU";
    pkg.project().description = "CPU block reliability verification package";
    pkg.project().customer = "ExampleSemi";
    pkg.project().design_stage = "place_route";
    pkg.set_validation_status(ValidationStatus::Valid);

    pkg.artifacts().push_back({"tech-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    pkg.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    pkg.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    pkg.artifacts().push_back({"rules-1", "rules/aegis_rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    pkg.artifacts().push_back({"power-1", "power/power_domains.csv", ArtifactRole::PowerDomainsCsv, ArtifactCategory::Power, true, "manifest"});
    pkg.diagnostics().push_back({DiagnosticSeverity::Info, "IMPORT_READY", "Package is ready for validation", std::nullopt});
    pkg.rebuild_normalized_view();

    const std::string json1 = pkg.to_manifest_json(2);
    const ProjectPackage reparsed = ProjectPackage::from_manifest_json(json1);
    const std::string json2 = reparsed.to_manifest_json(2);

    REQUIRE(json1 == json2);
    REQUIRE(reparsed.project().name == "FalconCPU");
    REQUIRE(reparsed.validation_status() == ValidationStatus::Valid);
    REQUIRE(reparsed.normalized().power_artifact_ids == std::vector<std::string>{"power-1"});
}
