#include "aegis/scripting/cli_workflow.hpp"

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/current_activity_application.hpp"
#include "aegis/graph/power_intent_application.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/parsing/lef_parser.hpp"
#include "aegis/parsing/power_intent.hpp"
#include "aegis/parsing/spice_parser.hpp"
#include "aegis/parsing/verilog_parser.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/rules/waivers.hpp"
#include "aegis/storage/import_validation.hpp"

#include "aegis/reporting/baseline.hpp"
#include "aegis/reporting/regression_diff.hpp"
#include "aegis/reporting/signoff_exports.hpp"

#include <map>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace aegis::scripting {
namespace {

using json = nlohmann::json;
using aegis::graph::ConnectivityGraph;
using aegis::graph::EdgeType;
using aegis::graph::NetNode;
using aegis::graph::PinNode;
using aegis::parsing::LayoutIR;
using aegis::parsing::LefParser;
using aegis::parsing::SpiceParser;
using aegis::parsing::VerilogParser;
using aegis::rules::RuleContext;
using aegis::rules::RuleEngine;
using aegis::rules::RulePackLoader;
using aegis::rules::Severity;
using aegis::storage::ArtifactCategory;
using aegis::storage::ArtifactRole;
using aegis::storage::DiagnosticSeverity;
using aegis::storage::ImportDiagnostic;
using aegis::storage::ImportPreflightValidator;
using aegis::storage::ProjectPackage;
using aegis::storage::SourceArtifact;
using aegis::storage::ValidationStatus;

void add_artifact(ProjectPackage& package,
                  const std::filesystem::path& path,
                  ArtifactRole role,
                  bool optional,
                  std::size_t& index)
{
    SourceArtifact artifact;
    artifact.id = "cli-artifact-" + std::to_string(++index);
    artifact.path = path;
    artifact.role = role;
    artifact.category = aegis::storage::category_for_role(role);
    artifact.optional = optional;
    artifact.origin = "cli";
    package.artifacts().push_back(std::move(artifact));
}

void push_diagnostic(ProjectPackage& package,
                     DiagnosticSeverity severity,
                     std::string code,
                     std::string message,
                     std::optional<std::string> artifact_id = std::nullopt)
{
    package.diagnostics().push_back({severity, std::move(code), std::move(message), std::move(artifact_id)});
}

bool file_exists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

void attach_lef_summary(ProjectPackage& package,
                        const SourceArtifact& artifact,
                        const std::filesystem::path& resolved_path)
{
    LefParser parser;
    const auto lef = parser.parse_file(resolved_path);

    aegis::storage::LefTechnologyData summary;
    summary.artifact_id = artifact.id;
    summary.source_path = artifact.path;
    summary.version = lef.version;
    summary.site_count = lef.sites.size();
    summary.layer_count = lef.layers.size();
    summary.via_count = lef.vias.size();
    summary.macro_count = lef.macros.size();
    summary.diagnostic_count = lef.diagnostics.size();
    summary.has_errors = lef.has_errors();
    for (const auto& layer : lef.layers) {
        summary.layers.push_back({layer.name,
                                  layer.type,
                                  layer.width_value,
                                  layer.pitch.has_value() ? std::optional<double>(layer.pitch->x) : std::nullopt,
                                  layer.pitch.has_value() ? std::optional<double>(layer.pitch->y) : std::nullopt,
                                  layer.routing_direction == aegis::parsing::LefRoutingDirection::Unknown
                                      ? std::nullopt
                                      : std::optional<std::string>(aegis::parsing::to_string(layer.routing_direction))});
    }
    for (const auto& macro : lef.macros) {
        summary.macros.push_back({macro.name,
                                  macro.macro_class,
                                  macro.width,
                                  macro.height,
                                  macro.pins.size(),
                                  macro.obstruction.has_value()});
    }
    package.normalized().lef_libraries.push_back(std::move(summary));

    for (const auto& diagnostic : lef.diagnostics) {
        const auto severity = diagnostic.severity == aegis::parsing::LefDiagnostic::Severity::Error
            ? DiagnosticSeverity::Error
            : (diagnostic.severity == aegis::parsing::LefDiagnostic::Severity::Warning
                ? DiagnosticSeverity::Warning
                : DiagnosticSeverity::Info);
        push_diagnostic(package,
                        severity,
                        diagnostic.code,
                        "LEF '" + artifact.path.generic_string() + "': " + diagnostic.message,
                        artifact.id);
    }
}

std::filesystem::path package_base_path(const CliProjectInput& input)
{
    if (input.manifest_path.has_value()) {
        return input.manifest_path->parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path resolve_artifact_path(const CliProjectInput& input,
                                            const SourceArtifact& artifact)
{
    if (artifact.path.is_absolute()) {
        return artifact.path;
    }
    return package_base_path(input) / artifact.path;
}

std::string diagnostic_severity_to_string(DiagnosticSeverity severity)
{
    return aegis::storage::to_string(severity);
}

std::string parser_severity_to_string(aegis::parsing::CurrentActivityDiagnostic::Severity severity)
{
    return severity == aegis::parsing::CurrentActivityDiagnostic::Severity::Error ? "error" : "warning";
}

std::string parser_severity_to_string(aegis::parsing::PowerIntentDiagnostic::Severity severity)
{
    return severity == aegis::parsing::PowerIntentDiagnostic::Severity::Error ? "error" : "warning";
}

json artifact_to_json(const SourceArtifact& artifact)
{
    return json{
        {"id", artifact.id},
        {"path", artifact.path.generic_string()},
        {"role", aegis::storage::to_string(artifact.role)},
        {"category", aegis::storage::to_string(artifact.category)},
        {"optional", artifact.optional},
        {"origin", artifact.origin}
    };
}

json import_diagnostic_to_json(const ImportDiagnostic& diagnostic)
{
    json item{
        {"severity", diagnostic_severity_to_string(diagnostic.severity)},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.artifact_id.has_value()) {
        item["artifact_id"] = *diagnostic.artifact_id;
    }
    return item;
}

json current_diagnostic_to_json(const aegis::parsing::CurrentActivityDiagnostic& diagnostic)
{
    json item{
        {"severity", parser_severity_to_string(diagnostic.severity)},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.source_row.has_value()) {
        item["source_row"] = *diagnostic.source_row;
    }
    if (diagnostic.net_name.has_value()) {
        item["net_name"] = *diagnostic.net_name;
    }
    return item;
}

json power_diagnostic_to_json(const aegis::parsing::PowerIntentDiagnostic& diagnostic)
{
    json item{
        {"severity", parser_severity_to_string(diagnostic.severity)},
        {"code", diagnostic.code},
        {"message", diagnostic.message}
    };
    if (diagnostic.source_row.has_value()) {
        item["source_row"] = *diagnostic.source_row;
    }
    if (diagnostic.instance_name.has_value()) {
        item["instance_name"] = *diagnostic.instance_name;
    }
    return item;
}

LayoutIR append_ir(LayoutIR combined, const LayoutIR& next)
{
    if (combined.design_name.empty()) {
        combined.design_name = next.design_name;
    }
    combined.layers.insert(combined.layers.end(), next.layers.begin(), next.layers.end());
    combined.geometries.insert(combined.geometries.end(), next.geometries.begin(), next.geometries.end());
    combined.nets.insert(combined.nets.end(), next.nets.begin(), next.nets.end());
    combined.devices.insert(combined.devices.end(), next.devices.begin(), next.devices.end());
    combined.ports.insert(combined.ports.end(), next.ports.begin(), next.ports.end());
    combined.annotations.insert(combined.annotations.end(), next.annotations.begin(), next.annotations.end());
    for (const auto& [key, value] : next.metadata) {
        combined.metadata.emplace(key, value);
    }
    return combined;
}

LayoutIR parse_netlist_artifact(const std::filesystem::path& path,
                                ArtifactRole role)
{
    switch (role) {
    case ArtifactRole::Verilog:
    case ArtifactRole::SystemVerilog: {
        VerilogParser parser;
        return parser.parse_to_layout_ir(path);
    }
    case ArtifactRole::Spice:
    case ArtifactRole::Spi:
    case ArtifactRole::Cdl: {
        SpiceParser parser;
        return parser.parse_to_layout_ir(path);
    }
    default:
        throw std::runtime_error("Unsupported netlist role for execution: '" + aegis::storage::to_string(role) + "'");
    }
}

json build_package_json(const ProjectPackage& package)
{
    json artifacts = json::array();
    for (const auto& artifact : package.artifacts()) {
        artifacts.push_back(artifact_to_json(artifact));
    }

    json diagnostics = json::array();
    for (const auto& diagnostic : package.diagnostics()) {
        diagnostics.push_back(import_diagnostic_to_json(diagnostic));
    }

    json lef_libraries = json::array();
    for (const auto& library : package.normalized().lef_libraries) {
        json layers = json::array();
        for (const auto& layer : library.layers) {
            json item;
            item["name"] = layer.name;
            item["type"] = layer.type;
            if (layer.width.has_value()) item["width"] = *layer.width;
            if (layer.pitch_x.has_value()) item["pitch_x"] = *layer.pitch_x;
            if (layer.pitch_y.has_value()) item["pitch_y"] = *layer.pitch_y;
            if (layer.direction.has_value()) item["direction"] = *layer.direction;
            layers.push_back(std::move(item));
        }

        json macros = json::array();
        for (const auto& macro : library.macros) {
            json m;
            m["name"] = macro.name;
            m["macro_class"] = macro.macro_class;
            m["width"] = macro.width;
            m["height"] = macro.height;
            m["pin_count"] = macro.pin_count;
            m["has_obstruction"] = macro.has_obstruction;
            macros.push_back(std::move(m));
        }

        json lib;
        lib["artifact_id"] = library.artifact_id;
        lib["source_path"] = library.source_path.generic_string();
        lib["version"] = library.version;
        lib["site_count"] = library.site_count;
        lib["layer_count"] = library.layer_count;
        lib["via_count"] = library.via_count;
        lib["macro_count"] = library.macro_count;
        lib["diagnostic_count"] = library.diagnostic_count;
        lib["has_errors"] = library.has_errors;
        lib["layers"] = std::move(layers);
        lib["macros"] = std::move(macros);
        lef_libraries.push_back(std::move(lib));
    }

    json result;
    result["project"] = json{
        {"name", package.project().name},
        {"description", package.project().description},
        {"customer", package.project().customer},
        {"design_stage", package.project().design_stage}
    };
    result["manifest_version"] = package.manifest_version();
    result["validation_status"] = aegis::storage::to_string(package.validation_status());
    result["artifacts"] = std::move(artifacts);
    result["normalized"]["lef_libraries"] = std::move(lef_libraries);
    result["diagnostics"] = std::move(diagnostics);
    return result;
}

struct PreparedExecution {
    RuleEngine engine;
    ConnectivityGraph graph;
    json runtime_diagnostics = json::array();
};

PreparedExecution prepare_execution(const ProjectPackage& package, const CliProjectInput& input)
{
    PreparedExecution prepared;

    LayoutIR combined_ir;
    for (const auto& artifact_id : package.normalized().netlist_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (!artifact) {
            continue;
        }
        combined_ir = append_ir(std::move(combined_ir),
                                parse_netlist_artifact(resolve_artifact_path(input, *artifact), artifact->role));
    }
    std::vector<std::string> unresolved;
    prepared.graph = ConnectivityGraph::from_layout_ir(combined_ir, unresolved);
    for (const auto& reference : unresolved) {
        prepared.runtime_diagnostics.push_back({
            {"severity", "warning"},
            {"code", "GRAPH_UNRESOLVED_REFERENCE"},
            {"message", "Resolved missing connectivity reference during graph build: " + reference}
        });
    }

    const auto* rules_artifact = package.normalized().rule_artifact_ids.empty()
        ? nullptr
        : package.find_artifact_by_id(package.normalized().rule_artifact_ids.front());
    if (!rules_artifact) {
        throw std::runtime_error("No rule pack artifact available for execution");
    }

    RulePackLoader loader;
    const auto pack = loader.load_from_file(resolve_artifact_path(input, *rules_artifact));
    for (auto& rule : loader.instantiate_rules(pack)) {
        prepared.engine.register_rule(std::move(rule));
    }

    for (const auto& artifact_id : package.normalized().current_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (!artifact) {
            continue;
        }

        aegis::parsing::CurrentActivityParser parser;
        const auto data = parser.parse_csv_file(resolve_artifact_path(input, *artifact));
        if (data.has_errors()) {
            for (const auto& diagnostic : data.diagnostics) {
                prepared.runtime_diagnostics.push_back(current_diagnostic_to_json(diagnostic));
            }
            throw aegis::rules::RulePackValidationException("Current/activity import contains blocking errors");
        }

        for (const auto& record : data.records) {
            if (!prepared.graph.find_net(record.net_name).has_value()) {
                const auto net_id = prepared.graph.add_net(NetNode{record.net_name, {}});
                const auto pin_id = prepared.graph.add_pin(PinNode{"port_" + record.net_name, "INPUT", std::nullopt, std::nullopt, std::nullopt});
                prepared.graph.add_edge(pin_id, net_id, {EdgeType::NetToPin, ""});
            }
        }

        const auto apply_result = aegis::graph::apply_current_activity(prepared.graph, data);
        for (const auto& diagnostic : apply_result.diagnostics) {
            prepared.runtime_diagnostics.push_back(current_diagnostic_to_json(diagnostic));
        }
    }

    for (const auto& artifact_id : package.normalized().power_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (!artifact) {
            continue;
        }

        aegis::parsing::PowerIntentParser parser;
        const auto data = parser.parse_csv_file(resolve_artifact_path(input, *artifact));
        if (data.has_errors()) {
            for (const auto& diagnostic : data.diagnostics) {
                prepared.runtime_diagnostics.push_back(power_diagnostic_to_json(diagnostic));
            }
            throw aegis::rules::RulePackValidationException("Power-domain import contains blocking errors");
        }

        for (const auto& assignment : data.assignments) {
            if (!prepared.graph.find_device(assignment.instance_name).has_value()) {
                prepared.graph.add_device({assignment.instance_name, "BLOCK", {}});
            }
        }

        const auto apply_result = aegis::graph::apply_power_intent(prepared.graph, data);
        for (const auto& diagnostic : apply_result.diagnostics) {
            prepared.runtime_diagnostics.push_back(power_diagnostic_to_json(diagnostic));
        }
    }

    return prepared;
}

json build_run_report(const ProjectPackage& package,
                      const std::vector<aegis::rules::Violation>& violations,
                      const json& runtime_diagnostics)
{
    std::map<std::string, int> severity_counts;
    for (const auto& violation : violations) {
        ++severity_counts[aegis::rules::severity_to_string(violation.severity)];
    }

    std::size_t waived_count = 0;
    for (const auto& violation : violations) {
        if (violation.metadata.get<bool>("waived").value_or(false)) {
            ++waived_count;
        }
    }

    json artifacts = json::array();
    for (const auto& artifact : package.artifacts()) {
        artifacts.push_back(artifact_to_json(artifact));
    }

    json import_diagnostics = json::array();
    for (const auto& diagnostic : package.diagnostics()) {
        import_diagnostics.push_back(import_diagnostic_to_json(diagnostic));
    }

    json summary_severities = json::object();
    for (const auto& [severity, count] : severity_counts) {
        summary_severities[severity] = count;
    }

    json report{
        {"project", {
            {"name", package.project().name},
            {"description", package.project().description},
            {"customer", package.project().customer},
            {"design_stage", package.project().design_stage}
        }},
        {"source_package", {
            {"manifest_version", package.manifest_version()},
            {"validation_status", aegis::storage::to_string(package.validation_status())},
            {"artifacts", artifacts}
        }},
        {"summary", {
             {"artifact_count", package.artifacts().size()},
             {"violation_count", violations.size()},
             {"active_violation_count", violations.size() - waived_count},
             {"waived_violation_count", waived_count},
             {"severity_counts", summary_severities}
         }},
        {"import_diagnostics", import_diagnostics},
        {"runtime_diagnostics", runtime_diagnostics},
        {"violations", violations}
    };
    return report;
}

} // namespace

CliWorkflowResult CliWorkflow::import_project(const CliProjectInput& input) const
{
    CliWorkflowResult result;
    try {
        result.package = build_package(input);
        result.output_json = build_package_json(result.package).dump(2);
        result.exit_code = result.package.validation_status() == ValidationStatus::Invalid
            ? CliExitCode::ImportFailure
            : CliExitCode::SuccessNoViolations;
    } catch (const std::exception& ex) {
        result.exit_code = CliExitCode::ImportFailure;
        result.error_message = ex.what();
    }
    return result;
}

CliWorkflowResult CliWorkflow::run_project(const CliProjectInput& input) const
{
    CliWorkflowResult result;
    try {
        result.package = build_package(input);
        if (result.package.validation_status() == ValidationStatus::Invalid) {
            result.output_json = build_package_json(result.package).dump(2);
            result.exit_code = CliExitCode::ImportFailure;
            return result;
        }

        PreparedExecution prepared = prepare_execution(result.package, input);
        const RuleContext ctx{prepared.graph, aegis::graph::PropertyMap{}, result.package.project().name};
        result.violations = prepared.engine.run_all(ctx);

        // Apply waivers (if any) and keep an audit trail in violation metadata.
        std::vector<aegis::rules::WaiverEntry> waivers;
        for (const auto& waiver_id : result.package.normalized().waiver_artifact_ids) {
            const auto* artifact = result.package.find_artifact_by_id(waiver_id);
            if (artifact == nullptr) {
                continue;
            }
            const auto resolved = resolve_artifact_path(input, *artifact);

            aegis::rules::WaiverParseResult parsed;
            switch (artifact->role) {
            case ArtifactRole::WaiverCsv:
                parsed = aegis::rules::parse_waivers_csv_file(resolved);
                break;
            case ArtifactRole::WaiverJson:
                parsed = aegis::rules::parse_waivers_json_file(resolved);
                break;
            case ArtifactRole::WaiverYaml:
                parsed = aegis::rules::parse_waivers_yaml_file(resolved);
                break;
            default:
                continue;
            }
            waivers.insert(waivers.end(),
                           std::make_move_iterator(parsed.waivers.begin()),
                           std::make_move_iterator(parsed.waivers.end()));
        }
        if (!waivers.empty()) {
            (void)aegis::rules::apply_waivers_in_place(result.violations, waivers);
        } else {
            for (auto& v : result.violations) {
                v.metadata = v.metadata.with("waived", false);
            }
        }

        result.output_json = build_run_report(result.package, result.violations, prepared.runtime_diagnostics).dump(2);
        result.exit_code = result.violations.empty()
            ? CliExitCode::SuccessNoViolations
            : CliExitCode::SuccessWithViolations;
    } catch (const aegis::rules::RulePackValidationException& ex) {
        result.exit_code = CliExitCode::ImportFailure;
        result.error_message = ex.what();
    } catch (const std::exception& ex) {
        result.exit_code = CliExitCode::ExecutionError;
        result.error_message = ex.what();
    }
    return result;
}

CliWorkflowResult CliWorkflow::report_project(const CliProjectInput& input) const
{
    return run_project(input);
}

ProjectPackage CliWorkflow::build_package(const CliProjectInput& input) const
{
    ImportPreflightValidator validator;
    ProjectPackage package;

    if (input.manifest_path.has_value()) {
        package = ProjectPackage::from_manifest_file(*input.manifest_path);
        if (!input.project_name.empty()) {
            package.project().name = input.project_name;
        }
    } else {
        package.set_manifest_version(1);
        package.project().name = input.project_name;

        std::size_t artifact_index = 0;
        for (const auto& lef : input.lef_paths) {
            add_artifact(package, lef, ArtifactRole::Lef, false, artifact_index);
        }
        if (input.def_path.has_value()) {
            add_artifact(package, *input.def_path, ArtifactRole::Def, false, artifact_index);
        }
        if (input.netlist_path.has_value()) {
            const auto ext = input.netlist_path->extension().string();
            ArtifactRole role = ArtifactRole::Verilog;
            if (ext == ".sv") role = ArtifactRole::SystemVerilog;
            else if (ext == ".spi") role = ArtifactRole::Spi;
            else if (ext == ".cdl") role = ArtifactRole::Cdl;
            else if (ext == ".sp") role = ArtifactRole::Spice;
            add_artifact(package, *input.netlist_path, role, false, artifact_index);
        }
        if (input.rules_path.has_value()) {
            add_artifact(package, *input.rules_path, ArtifactRole::AegisRulePack, false, artifact_index);
        }
        if (input.power_csv_path.has_value()) {
            add_artifact(package, *input.power_csv_path, ArtifactRole::PowerDomainsCsv, true, artifact_index);
        }
        if (input.current_csv_path.has_value()) {
            add_artifact(package, *input.current_csv_path, ArtifactRole::CurrentCsv, true, artifact_index);
        }
        for (const auto& waiver : input.waiver_paths) {
            add_artifact(package, waiver, ArtifactRole::WaiverCsv, true, artifact_index);
        }
        for (const auto& report : input.external_report_paths) {
            add_artifact(package, report, ArtifactRole::ImportedReport, true, artifact_index);
        }

        if (package.project().name.empty()) {
            push_diagnostic(package,
                            DiagnosticSeverity::Error,
                            "PROJECT_NAME_MISSING",
                            "Explicit CLI import requires --project to name the project");
        }
    }

    for (const auto& artifact : package.artifacts()) {
        const auto resolved = resolve_artifact_path(input, artifact);
        if (!file_exists(resolved)) {
            push_diagnostic(package,
                            DiagnosticSeverity::Error,
                            "ARTIFACT_FILE_MISSING",
                            "Artifact file does not exist: '" + resolved.generic_string() + "'",
                            artifact.id);
            continue;
        }
        if (artifact.role == ArtifactRole::Lef && package.find_lef_technology_by_artifact_id(artifact.id) == nullptr) {
            try {
                attach_lef_summary(package, artifact, resolved);
            } catch (const std::exception& ex) {
                push_diagnostic(package,
                                DiagnosticSeverity::Error,
                                "LEF_PARSE_FAILED",
                                "Failed to parse LEF technology/library artifact '" + resolved.generic_string() + "': " + ex.what(),
                                artifact.id);
            }
        }
    }

    package.rebuild_normalized_view();
    const auto validation = validator.validate(package);
    package.diagnostics().insert(package.diagnostics().end(), validation.begin(), validation.end());
    package.set_validation_status(validator.derive_status(package.diagnostics()));
    return package;
}

std::string cli_exit_code_name(CliExitCode code)
{
    switch (code) {
    case CliExitCode::SuccessNoViolations: return "success_no_violations";
    case CliExitCode::ImportFailure: return "import_failure";
    case CliExitCode::SuccessWithViolations: return "success_with_violations";
    case CliExitCode::SuccessWithRegressions: return "success_with_regressions";
    case CliExitCode::ExecutionError: return "execution_error";
    case CliExitCode::UsageError: return "usage_error";
    }
    return "usage_error";
}

int cli_exit_code_value(CliExitCode code) noexcept
{
    return static_cast<int>(code);
}

CliWorkflowResult CliWorkflow::baseline_project(const CliProjectInput& input) const
{
    CliWorkflowResult result;
    try {
        // Reuse run path to build package, execute rules, and apply waivers/audit tagging.
        result = run_project(input);
        if (result.exit_code == CliExitCode::ImportFailure || result.exit_code == CliExitCode::ExecutionError) {
            return result;
        }

        const auto baseline = aegis::reporting::build_baseline(result.violations, result.package.project().name);
        aegis::reporting::RunMetadata meta;
        meta.tool_version = "dev";
        meta.project_name = result.package.project().name;
        if (!result.package.normalized().rule_artifact_ids.empty()) {
            meta.rule_pack_id = result.package.normalized().rule_artifact_ids.front();
        }
        result.output_json = aegis::reporting::export_baseline_json(baseline, meta);

        // Baseline creation should not fail the pipeline just because findings exist.
        result.exit_code = CliExitCode::SuccessNoViolations;
    } catch (const std::exception& ex) {
        result.exit_code = CliExitCode::ExecutionError;
        result.error_message = ex.what();
    }
    return result;
}

CliWorkflowResult CliWorkflow::diff_project(const CliProjectInput& input) const
{
    CliWorkflowResult result;
    try {
        if (!input.baseline_path.has_value()) {
            result.exit_code = CliExitCode::UsageError;
            result.error_message = "Missing required --baseline for diff command";
            return result;
        }

        // Run current project.
        result = run_project(input);
        if (result.exit_code == CliExitCode::ImportFailure || result.exit_code == CliExitCode::ExecutionError) {
            return result;
        }

        const auto baseline = aegis::reporting::read_baseline_json_file(*input.baseline_path);
        const auto diff = aegis::reporting::diff_baseline_against_violations(baseline,
                                                                              result.violations,
                                                                              result.package.project().name);

        aegis::reporting::RunMetadata meta;
        meta.tool_version = "dev";
        meta.project_name = result.package.project().name;
        if (!result.package.normalized().rule_artifact_ids.empty()) {
            meta.rule_pack_id = result.package.normalized().rule_artifact_ids.front();
        }
        result.output_json = aegis::reporting::export_regression_diff_json(diff, meta);

        // Regressions: any newly introduced or severity changes to a worse severity.
        bool has_regressions = diff.summary.new_count > 0;
        for (const auto& c : diff.changed_severity) {
            auto rank = [](aegis::rules::Severity s) {
                using S = aegis::rules::Severity;
                switch (s) {
                case S::Fatal: return 0;
                case S::Error: return 1;
                case S::Warning: return 2;
                case S::Info: return 3;
                }
                return 99;
            };
            if (rank(c.to) < rank(c.from)) {
                has_regressions = true;
                break;
            }
        }
        result.exit_code = has_regressions ? CliExitCode::SuccessWithRegressions : CliExitCode::SuccessNoViolations;
    } catch (const std::exception& ex) {
        result.exit_code = CliExitCode::ExecutionError;
        result.error_message = ex.what();
    }
    return result;
}

} // namespace aegis::scripting
