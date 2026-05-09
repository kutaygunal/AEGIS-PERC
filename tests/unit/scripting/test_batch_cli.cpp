#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_batch_cli_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

fs::path cli_path()
{
    fs::path path = fs::path(AEGIS_BINARY_DIR) / "bin";
#ifdef _WIN32
    path /= "Release";
    path /= "aegis-perc-cli.exe";
#else
    path /= "aegis-perc-cli";
#endif
    return path;
}

std::string quote(const fs::path& path)
{
    return std::string("\"") + path.string() + "\"";
}

std::string shell_wrap(std::string command)
{
#ifdef _WIN32
    return std::string("cmd /c \"") + command + "\"";
#else
    return command;
#endif
}

} // namespace

TEST_CASE("aegis-perc-cli import writes a manifest from explicit file arguments", "[scripting][p6-008][BatchCLI]")
{
    const fs::path root = make_temp_dir();
    const fs::path out_manifest = root / "out" / "manifest.json";

    write_file(root / "tech.lef", "VERSION 5.8 ;\nLAYER M1 ;\n");
    write_file(root / "top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules.yaml", "rules:\n  - id: FLOATING_NET\n    type: floating_net\n    severity: high\n");

    const std::string command = shell_wrap(
        quote(cli_path()) +
        " import --project FalconCPU" +
        " --lef " + quote(root / "tech.lef") +
        " --def " + quote(root / "top.def") +
        " --netlist " + quote(root / "top.v") +
        " --rules " + quote(root / "rules.yaml") +
        " --output " + quote(out_manifest));

    const int exit_code = std::system(command.c_str());
    REQUIRE(exit_code == 0);
    REQUIRE(fs::exists(out_manifest));

    const std::string manifest = read_file(out_manifest);
    REQUIRE(manifest.find("\"name\": \"FalconCPU\"") != std::string::npos);
    REQUIRE(manifest.find("\"role\": \"lef\"") != std::string::npos);
    REQUIRE(manifest.find("\"validation_status\": \"valid\"") != std::string::npos);

    remove_tree(root);
}

TEST_CASE("aegis-perc-cli report runs from a manifest and exports violation JSON", "[scripting][p6-008][BatchCLI]")
{
    const fs::path root = make_temp_dir();
    const fs::path out_report = root / "out" / "report.json";

    write_file(root / "layout/tech.lef", "VERSION 5.8 ;\nLAYER M1 ;\n");
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

    write_file(root / "manifest.json",
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
               "  }\n"
               "}\n");

    const std::string command = shell_wrap(
        quote(cli_path()) +
        " report --manifest " + quote(root / "manifest.json") +
        " --output " + quote(out_report));

    const int exit_code = std::system(command.c_str());
    REQUIRE(exit_code == 3);
    REQUIRE(fs::exists(out_report));

    const std::string report = read_file(out_report);
    REQUIRE(report.find("\"violation_count\": 1") != std::string::npos);
    REQUIRE(report.find("\"rule_id\": \"EM_CURRENT_LIMIT\"") != std::string::npos);
    REQUIRE(report.find("\"source_package\"") != std::string::npos);
    REQUIRE(report.find("\"name\": \"FalconCPU\"") != std::string::npos);

    remove_tree(root);
}

TEST_CASE("aegis-perc-cli run uses parsed Verilog connectivity for gate-level netlists", "[scripting][P9][P9-003][BatchCLI]")
{
    const fs::path root = make_temp_dir();
    const fs::path out_report = root / "out" / "report.json";

    write_file(root / "layout/tech.lef", "VERSION 5.8 ;\nLAYER M1 ;\n");
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

    write_file(root / "manifest.json",
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
               "    \"current\": [],\n"
               "    \"waivers\": [],\n"
               "    \"external_reports\": []\n"
               "  }\n"
               "}\n");

    const std::string command = shell_wrap(
        quote(cli_path()) +
        " run --manifest " + quote(root / "manifest.json") +
        " --output " + quote(out_report));

    const int exit_code = std::system(command.c_str());
    REQUIRE(exit_code == 3);
    REQUIRE(fs::exists(out_report));

    const std::string report = read_file(out_report);
    REQUIRE(report.find("\"rule_id\": \"FLOATING_NET\"") != std::string::npos);
    REQUIRE(report.find("dangling") != std::string::npos);

    remove_tree(root);
}
