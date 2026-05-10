#include <catch2/catch_test_macros.hpp>

#include "aegis/storage/imported_design_session.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << content;
}

} // namespace

using namespace aegis::storage;

TEST_CASE("ImportedDesignSession builds deterministic imported workspace state from shipped customer package",
          "[storage][P13][P13-001][ImportedDesignSession]")
{
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.project().description = "Curated import-package sample derived from openframe_simple_example/simple_design";
    package.project().customer = "OpenFrame Example";
    package.project().design_stage = "place_route";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.artifacts().push_back({"rules-1", "rules/aegis_rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    package.artifacts().push_back({"power-1", "power/power_domains.csv", ArtifactRole::PowerDomainsCsv, ArtifactCategory::Power, true, "manifest"});
    package.artifacts().push_back({"current-1", "reports/current_report.csv", ArtifactRole::CurrentCsv, ArtifactCategory::Current, true, "manifest"});
    package.artifacts().push_back({"waiver-1", "reports/waivers.csv", ArtifactRole::WaiverCsv, ArtifactCategory::Waivers, true, "manifest"});
    package.artifacts().push_back({"report-1", "reports/erc_report.rpt", ArtifactRole::ImportedReport, ArtifactCategory::ExternalReports, true, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    ImportedDesignSession session = builder.build(package, root);

    REQUIRE(session.package().project().name == "OpenFrameSimpleDesign");
    REQUIRE(session.base_path() == root);
    REQUIRE(session.technology_libraries().size() == 1);
    REQUIRE(session.rule_artifact_ids() == std::vector<std::string>{"rules-1"});
    REQUIRE(session.rule_artifact_paths().size() == 1);
    REQUIRE(session.object_count(ImportedDesignObjectKind::Layer) > 0);
    REQUIRE(session.object_count(ImportedDesignObjectKind::Port) > 0);
    REQUIRE(session.object_count(ImportedDesignObjectKind::Net) > 0);
    REQUIRE(session.object_count(ImportedDesignObjectKind::Device) > 0);
    REQUIRE(session.graph().node_count() > 0);
    REQUIRE_FALSE(session.has_errors());

    bool saw_net = false;
    for (const auto& object : session.objects()) {
        if (object.kind == ImportedDesignObjectKind::Net) {
            saw_net = true;
            const bool has_expected_artifact = object.provenance.artifact_id == "netlist-1"
                || object.provenance.artifact_id == "layout-1";
            REQUIRE(has_expected_artifact);
            REQUIRE_FALSE(object.stable_id.empty());
            REQUIRE(session.find_object_by_stable_id(object.stable_id) != nullptr);
            break;
        }
    }
    REQUIRE(saw_net);
}

TEST_CASE("ImportedDesignSession preserves partial-ready state when optional enrichments fail",
          "[storage][P13][P13-001][ImportedDesignSession]")
{
    const fs::path root = fs::temp_directory_path() / "aegis_perc_p13_001_invalid_current";
    std::error_code ec;
    fs::remove_all(root, ec);

    write_text(root / "layout/technology.lef",
               "VERSION 5.8 ;\n"
               "LAYER M1\n"
               "  TYPE ROUTING ;\n"
               "  WIDTH 0.10 ;\n"
               "  PITCH 0.20 ;\n"
               "  DIRECTION HORIZONTAL ;\n"
               "END M1\n"
               "END LIBRARY\n");
    write_text(root / "layout/top.def",
               "VERSION 5.8 ;\n"
               "DESIGN top ;\n"
               "DIEAREA ( 0 0 ) ( 100 100 ) ;\n"
               "PINS 1 ;\n"
               "- A + NET A + DIRECTION INPUT + PLACED ( 10 10 ) N ;\n"
               "END PINS\n"
               "NETS 1 ;\n"
               "- A ( PIN A ) ;\n"
               "END NETS\n"
               "END DESIGN\n");
    write_text(root / "netlist/top.v", "module top(input A, output Y); assign Y = A; endmodule\n");
    write_text(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");
    write_text(root / "reports/current_report.csv",
               "net_name,current_mA,voltage_domain,layer\n"
               "A,not_a_number,VDD,M1\n");

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "InvalidCurrentImport";
    package.artifacts().push_back({"tech-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.artifacts().push_back({"rules-1", "rules/aegis_rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    package.artifacts().push_back({"current-1", "reports/current_report.csv", ArtifactRole::CurrentCsv, ArtifactCategory::Current, true, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    ImportedDesignSession session = builder.build(package, root);

    REQUIRE(session.graph().node_count() > 0);
    REQUIRE(session.object_count(ImportedDesignObjectKind::Net) > 0);
    REQUIRE(session.status() == SessionBuildStatus::Error);
    REQUIRE_FALSE(session.current_activity().has_value());

    bool saw_current_error = false;
    for (const auto& diagnostic : session.diagnostics()) {
        if (diagnostic.stage == SessionBuildStage::CurrentEnrichment && diagnostic.severity == SessionDiagnosticSeverity::Error) {
            saw_current_error = true;
            REQUIRE(diagnostic.artifact_id == std::optional<std::string>{"current-1"});
        }
    }
    REQUIRE(saw_current_error);

    fs::remove_all(root, ec);
}
