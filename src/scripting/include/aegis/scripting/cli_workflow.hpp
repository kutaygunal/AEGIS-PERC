#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/storage/project_package.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::scripting {

enum class CliExitCode {
    SuccessNoViolations = 0,
    ImportFailure = 2,
    SuccessWithViolations = 3,
    ExecutionError = 4,
    UsageError = 64
};

struct CliProjectInput {
    std::optional<std::filesystem::path> manifest_path;
    std::string project_name;
    std::vector<std::filesystem::path> lef_paths;
    std::optional<std::filesystem::path> def_path;
    std::optional<std::filesystem::path> netlist_path;
    std::optional<std::filesystem::path> rules_path;
    std::optional<std::filesystem::path> power_csv_path;
    std::optional<std::filesystem::path> current_csv_path;
    std::vector<std::filesystem::path> waiver_paths;
    std::vector<std::filesystem::path> external_report_paths;
};

struct CliWorkflowResult {
    CliExitCode exit_code = CliExitCode::UsageError;
    aegis::storage::ProjectPackage package;
    std::vector<aegis::rules::Violation> violations;
    std::string output_json;
    std::string error_message;
};

class CliWorkflow {
public:
    CliWorkflow() = default;

    CliWorkflowResult import_project(const CliProjectInput& input) const;
    CliWorkflowResult run_project(const CliProjectInput& input) const;
    CliWorkflowResult report_project(const CliProjectInput& input) const;

private:
    aegis::storage::ProjectPackage build_package(const CliProjectInput& input) const;
};

std::string cli_exit_code_name(CliExitCode code);
int cli_exit_code_value(CliExitCode code) noexcept;

} // namespace aegis::scripting
