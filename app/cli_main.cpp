#include "aegis/scripting/cli_workflow.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using aegis::scripting::CliExitCode;
using aegis::scripting::CliProjectInput;
using aegis::scripting::CliWorkflow;
using aegis::scripting::CliWorkflowResult;

void write_output(const std::filesystem::path& path, const std::string& content)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Unable to open output file: " + path.string());
    }
    out << content;
}

void print_usage()
{
    std::cerr
        << "Usage:\n"
        << "  aegis-perc-cli import --project NAME --lef FILE [--lef FILE ...] --def FILE --netlist FILE --rules FILE [--power FILE] [--current FILE] [--output FILE]\n"
        << "  aegis-perc-cli import --manifest FILE [--project NAME] [--output FILE]\n"
        << "  aegis-perc-cli run    (--manifest FILE | explicit file args) [--output FILE]\n"
        << "  aegis-perc-cli report (--manifest FILE | explicit file args) [--output FILE]\n";
}

CliProjectInput parse_project_input(int argc, char** argv, std::filesystem::path& output_path)
{
    CliProjectInput input;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + flag);
            }
            return argv[++i];
        };

        if (arg == "--manifest") {
            input.manifest_path = require_value(arg);
        } else if (arg == "--project") {
            input.project_name = require_value(arg);
        } else if (arg == "--lef") {
            input.lef_paths.emplace_back(require_value(arg));
        } else if (arg == "--def") {
            input.def_path = require_value(arg);
        } else if (arg == "--netlist") {
            input.netlist_path = require_value(arg);
        } else if (arg == "--rules") {
            input.rules_path = require_value(arg);
        } else if (arg == "--power") {
            input.power_csv_path = require_value(arg);
        } else if (arg == "--current") {
            input.current_csv_path = require_value(arg);
        } else if (arg == "--waiver") {
            input.waiver_paths.emplace_back(require_value(arg));
        } else if (arg == "--external-report") {
            input.external_report_paths.emplace_back(require_value(arg));
        } else if (arg == "--output") {
            output_path = require_value(arg);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    return input;
}

int finish(const CliWorkflowResult& result, const std::filesystem::path& output_path)
{
    if (!result.error_message.empty()) {
        std::cerr << result.error_message << '\n';
    }

    if (!result.output_json.empty()) {
        if (!output_path.empty()) {
            write_output(output_path, result.output_json);
        } else {
            std::cout << result.output_json << '\n';
        }
    }

    return aegis::scripting::cli_exit_code_value(result.exit_code);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        print_usage();
        return aegis::scripting::cli_exit_code_value(CliExitCode::UsageError);
    }

    try {
        const std::string command = argv[1];
        std::filesystem::path output_path;
        const CliProjectInput input = parse_project_input(argc, argv, output_path);
        CliWorkflow workflow;

        if (command == "import") {
            return finish(workflow.import_project(input), output_path);
        }
        if (command == "run") {
            return finish(workflow.run_project(input), output_path);
        }
        if (command == "report") {
            return finish(workflow.report_project(input), output_path);
        }

        print_usage();
        return aegis::scripting::cli_exit_code_value(CliExitCode::UsageError);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        print_usage();
        return aegis::scripting::cli_exit_code_value(CliExitCode::UsageError);
    }
}
