#include <catch2/catch_test_macros.hpp>

#include "aegis/orchestration/job_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace aegis::orchestration;
using namespace aegis::storage;

namespace {

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_job_pipeline_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ProjectPackage make_manifest_package(const fs::path& root)
{
    write_file(root / "layout/tech.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/rules.yaml",
               "rules:\n"
               "  - id: EM_CURRENT_LIMIT\n"
               "    type: em_current_limit\n"
               "    severity: medium\n"
               "    parameters:\n"
               "      M4_max_mA: 40\n");
    write_file(root / "reports/current.csv",
               "net_name,current_mA,voltage_domain,layer\n"
               "VDD_CPU,52.4,CORE_0V8,M4\n");

    const std::string manifest =
        "{\n"
        "  \"manifest_version\": 1,\n"
        "  \"project\": {\n"
        "    \"name\": \"FalconCPU\"\n"
        "  },\n"
        "  \"artifacts\": {\n"
        "    \"technology\": [{\"id\": \"tech-1\", \"path\": \"layout/tech.lef\", \"role\": \"lef\"}],\n"
        "    \"layout\": [{\"id\": \"layout-1\", \"path\": \"layout/top.def\", \"role\": \"def\"}],\n"
        "    \"netlist\": [{\"id\": \"netlist-1\", \"path\": \"netlist/top.v\", \"role\": \"verilog\"}],\n"
        "    \"rules\": [{\"id\": \"rules-1\", \"path\": \"rules/rules.yaml\", \"role\": \"aegis_rule_pack\"}],\n"
        "    \"power\": [],\n"
        "    \"current\": [{\"id\": \"current-1\", \"path\": \"reports/current.csv\", \"role\": \"current_csv\", \"optional\": true}],\n"
        "    \"waivers\": [],\n"
        "    \"external_reports\": []\n"
        "  },\n"
        "  \"validation\": {\n"
        "    \"status\": \"valid\"\n"
        "  }\n"
        "}\n";

    return ProjectPackage::from_manifest_json(manifest);
}

bool saw_stage(const std::vector<JobProgressSnapshot>& snapshots, JobStage stage)
{
    return std::any_of(snapshots.begin(), snapshots.end(), [&](const auto& snapshot) {
        return snapshot.stage == stage;
    });
}

} // namespace

TEST_CASE("LocalJobPipeline executes imported projects through staged progress and exports JSON/HTML", "[orchestration][p6-009][JobPipeline]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);
    std::vector<JobProgressSnapshot> snapshots;

    LocalJobPipeline pipeline;
    JobRequest request;
    request.package = package;
    request.base_path = root;
    request.output_dir = root / "out";
    request.progress_callback = [&](const JobProgressSnapshot& snapshot) {
        snapshots.push_back(snapshot);
    };

    const JobId job_id = pipeline.submit(std::move(request));
    REQUIRE(pipeline.wait(job_id, std::chrono::seconds(5)));

    const auto result = pipeline.result(job_id);
    REQUIRE(result.has_value());
    REQUIRE(result->state == JobState::Completed);
    REQUIRE(result->violations.size() == 1);
    REQUIRE(result->json_report_path.has_value());
    REQUIRE(result->html_report_path.has_value());
    REQUIRE(fs::exists(*result->json_report_path));
    REQUIRE(fs::exists(*result->html_report_path));

    const auto final_snapshot = pipeline.snapshot(job_id);
    REQUIRE(final_snapshot.has_value());
    REQUIRE(final_snapshot->state == JobState::Completed);
    REQUIRE(final_snapshot->completed_stages == final_snapshot->total_stages);

    REQUIRE(saw_stage(snapshots, JobStage::Parse));
    REQUIRE(saw_stage(snapshots, JobStage::Normalize));
    REQUIRE(saw_stage(snapshots, JobStage::GraphBuild));
    REQUIRE(saw_stage(snapshots, JobStage::RuleExecution));
    REQUIRE(saw_stage(snapshots, JobStage::ReportExport));

    const std::string json_report = read_file(*result->json_report_path);
    REQUIRE(json_report.find("\"violation_count\": 1") != std::string::npos);
    REQUIRE(json_report.find("\"rule_id\": \"EM_CURRENT_LIMIT\"") != std::string::npos);

    const std::string html_report = read_file(*result->html_report_path);
    REQUIRE(html_report.find("FalconCPU") != std::string::npos);
    REQUIRE(html_report.find("Violation count") != std::string::npos);

    remove_tree(root);
}

TEST_CASE("LocalJobPipeline uses parsed Verilog connectivity during imported execution", "[orchestration][P9][P9-003][JobPipeline]")
{
    const fs::path root = make_temp_dir();
    write_file(root / "layout/tech.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v",
               "module top(input A, output Y);\n"
               "  wire dangling;\n"
               "  BUFX1 u0 (.A(A), .Y(Y));\n"
               "endmodule\n");
    write_file(root / "rules/rules.yaml",
               "rules:\n"
               "  - id: FLOATING_NET\n"
               "    type: floating_net\n"
               "    severity: high\n");

    ProjectPackage package;
    package.project().name = "VerilogConnectivity";
    package.artifacts().push_back({"tech-1", "layout/tech.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.artifacts().push_back({"rules-1", "rules/rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    package.rebuild_normalized_view();
    package.set_validation_status(ValidationStatus::Valid);

    LocalJobPipeline pipeline;
    JobRequest request;
    request.package = package;
    request.base_path = root;
    request.output_dir = root / "out";

    const JobId job_id = pipeline.submit(std::move(request));
    REQUIRE(pipeline.wait(job_id, std::chrono::seconds(5)));

    const auto result = pipeline.result(job_id);
    REQUIRE(result.has_value());
    REQUIRE(result->state == JobState::Completed);
    REQUIRE(result->violations.size() == 1);
    REQUIRE(result->violations.front().rule_id == "FLOATING_NET");
    REQUIRE(result->violations.front().message.find("dangling") != std::string::npos);

    remove_tree(root);
}

TEST_CASE("LocalJobPipeline cancellation stops local jobs cleanly and avoids partial result exports", "[orchestration][p6-009][JobPipeline]")
{
    const fs::path root = make_temp_dir();
    const ProjectPackage package = make_manifest_package(root);

    LocalJobPipeline pipeline;
    JobRequest request;
    request.package = package;
    request.base_path = root;
    request.output_dir = root / "cancelled_out";
    request.options.artificial_stage_delay = std::chrono::milliseconds(120);

    const JobId job_id = pipeline.submit(std::move(request));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    REQUIRE(pipeline.request_cancel(job_id));
    REQUIRE(pipeline.wait(job_id, std::chrono::seconds(5)));

    const auto result = pipeline.result(job_id);
    REQUIRE(result.has_value());
    REQUIRE(result->state == JobState::Cancelled);
    REQUIRE(!result->json_report_path.has_value());
    REQUIRE(!result->html_report_path.has_value());

    const auto snapshot = pipeline.snapshot(job_id);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->state == JobState::Cancelled);
    REQUIRE(snapshot->cancel_requested);

    REQUIRE(!fs::exists(root / "cancelled_out" / "report.json"));
    REQUIRE(!fs::exists(root / "cancelled_out" / "report.html"));

    remove_tree(root);
}
