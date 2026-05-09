#include <catch2/catch_test_macros.hpp>
#include "aegis/storage/import_validation.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace aegis::storage;

namespace {

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_import_validation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

void remove_tree(const fs::path& root)
{
    std::error_code ec;
    fs::remove_all(root, ec);
}

} // namespace

TEST_CASE("ImportPreflightValidator detects file roles from extension and content markers", "[storage][p6-003][ImportValidation]")
{
    const fs::path root = make_temp_dir();
    fs::create_directories(root);

    const fs::path rules = root / "rules.yaml";
    const fs::path power = root / "power.csv";
    const fs::path current = root / "current.csv";

    write_file(rules, "rules:\n  - id: FLOATING_NET\n");
    write_file(power, "instance,domain,voltage\nCPU_CORE,CORE_0V8,0.8\n");
    write_file(current, "net_name,current_mA,voltage_domain,layer\nVDD_CPU,52.4,0.8V,M4\n");

    ImportPreflightValidator validator;

    const auto rules_detection = validator.detect_file_role(rules);
    REQUIRE(rules_detection.best() != nullptr);
    REQUIRE(rules_detection.best()->role == ArtifactRole::AegisRulePack);

    const auto power_detection = validator.detect_file_role(power);
    REQUIRE(power_detection.best() != nullptr);
    REQUIRE(power_detection.best()->role == ArtifactRole::PowerDomainsCsv);

    const auto current_detection = validator.detect_file_role(current);
    REQUIRE(current_detection.best() != nullptr);
    REQUIRE(current_detection.best()->role == ArtifactRole::CurrentCsv);

    remove_tree(root);
}

TEST_CASE("ImportPreflightValidator scans customer folder and reports mixed known and unknown files", "[storage][p6-003][ImportValidation]")
{
    const fs::path root = make_temp_dir();

    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");
    write_file(root / "power/power_domains.csv", "instance,domain,voltage\nCPU_CORE,CORE_0V8,0.8\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nVDD_CPU,52.4,0.8V,M4\n");
    write_file(root / "misc/notes.bin", "opaque\x00data");

    ImportPreflightValidator validator;
    const ProjectPackage package = validator.scan_project_folder(root, "FalconCPU");

    REQUIRE(package.project().name == "FalconCPU");
    REQUIRE(package.validation_status() == ValidationStatus::Warning);
    REQUIRE(package.normalized().technology_artifact_ids.size() == 1);
    REQUIRE(package.normalized().lef_libraries.size() == 1);
    REQUIRE(package.normalized().lef_libraries.front().artifact_id == package.normalized().technology_artifact_ids.front());
    REQUIRE(package.normalized().lef_libraries.front().source_path == fs::path("layout/technology.lef"));
    REQUIRE(package.normalized().lef_libraries.front().layer_count == 1);
    REQUIRE(package.normalized().layout_artifact_ids.size() == 1);
    REQUIRE(package.normalized().netlist_artifact_ids.size() == 1);
    REQUIRE(package.normalized().rule_artifact_ids.size() == 1);
    REQUIRE(package.normalized().power_artifact_ids.size() == 1);
    REQUIRE(package.normalized().current_artifact_ids.size() == 1);

    bool saw_unknown_warning = false;
    for (const auto& diagnostic : package.diagnostics()) {
        if (diagnostic.code == "UNKNOWN_ARTIFACT") {
            saw_unknown_warning = true;
        }
    }
    REQUIRE(saw_unknown_warning);

    const std::string manifest = package.to_manifest_json(2);
    const ProjectPackage reloaded = ProjectPackage::from_manifest_json(manifest);
    REQUIRE(reloaded.find_lef_technology_by_artifact_id(package.normalized().technology_artifact_ids.front()) != nullptr);
    REQUIRE(reloaded.find_lef_technology_by_artifact_id(package.normalized().technology_artifact_ids.front())->layer_count == 1);

    remove_tree(root);
}

TEST_CASE("ImportPreflightValidator scan reports malformed Verilog netlists with artifact diagnostics", "[storage][P9][P9-003][ImportValidation]")
{
    const fs::path root = make_temp_dir();

    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y);\n  BUFX1 u0 (.A(A), .Y(Y);\nendmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");

    ImportPreflightValidator validator;
    const ProjectPackage package = validator.scan_project_folder(root, "BrokenVerilog");

    bool saw_parse_error = false;
    bool saw_lef_summary = !package.normalized().lef_libraries.empty();
    for (const auto& diagnostic : package.diagnostics()) {
        saw_parse_error = saw_parse_error || diagnostic.code == "VERILOG_PARSE_FAILED";
    }
    REQUIRE(saw_lef_summary);
    REQUIRE(saw_parse_error);
    REQUIRE(package.validation_status() == ValidationStatus::Invalid);

    remove_tree(root);
}

TEST_CASE("ImportPreflightValidator scan reports malformed SPICE/CDL netlists with artifact diagnostics", "[storage][P9][P9-004][ImportValidation]")
{
    const fs::path root = make_temp_dir();

    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.sp", ".SUBCKT top a b\n.FOO bad\n.ENDS top\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");

    ImportPreflightValidator validator;
    const ProjectPackage package = validator.scan_project_folder(root, "BrokenSpice");

    bool saw_parse_error = false;
    bool saw_lef_summary = !package.normalized().lef_libraries.empty();
    for (const auto& diagnostic : package.diagnostics()) {
        saw_parse_error = saw_parse_error || diagnostic.code == "SPICE_PARSE_FAILED";
    }
    REQUIRE(saw_lef_summary);
    REQUIRE(saw_parse_error);
    REQUIRE(package.validation_status() == ValidationStatus::Invalid);

    remove_tree(root);
}

TEST_CASE("ImportPreflightValidator validate reports missing required artifacts and duplicate path conflicts", "[storage][p6-003][ImportValidation]")
{
    ProjectPackage package;
    package.project().name = "BrokenProject";
    package.artifacts().push_back({"dup-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"dup-2", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.rebuild_normalized_view();

    ImportPreflightValidator validator;
    const auto diagnostics = validator.validate(package);

    bool missing_technology = false;
    bool missing_netlist = false;
    bool missing_rules = false;
    bool duplicate_path = false;
    for (const auto& diagnostic : diagnostics) {
        missing_technology = missing_technology || diagnostic.code == "MISSING_REQUIRED_TECHNOLOGY";
        missing_netlist = missing_netlist || diagnostic.code == "MISSING_REQUIRED_NETLIST";
        missing_rules = missing_rules || diagnostic.code == "MISSING_REQUIRED_RULES";
        duplicate_path = duplicate_path || diagnostic.code == "DUPLICATE_ARTIFACT_PATH";
    }

    REQUIRE(missing_technology);
    REQUIRE(missing_netlist);
    REQUIRE(missing_rules);
    REQUIRE(duplicate_path);
    REQUIRE(validator.derive_status(diagnostics) == ValidationStatus::Invalid);
}
