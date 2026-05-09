#include "aegis/storage/project_package.hpp"

#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace aegis::storage {
namespace {

using json = nlohmann::json;

std::string make_generated_artifact_id(ArtifactCategory category, std::size_t ordinal)
{
    return to_string(category) + "-" + std::to_string(ordinal + 1);
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open manifest file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

ArtifactCategory category_from_bucket_name(const std::string& name)
{
    static const std::unordered_map<std::string, ArtifactCategory> map{
        {"technology", ArtifactCategory::Technology},
        {"layout", ArtifactCategory::Layout},
        {"netlist", ArtifactCategory::Netlist},
        {"rules", ArtifactCategory::Rules},
        {"power", ArtifactCategory::Power},
        {"current", ArtifactCategory::Current},
        {"waivers", ArtifactCategory::Waivers},
        {"external_reports", ArtifactCategory::ExternalReports},
    };
    if (const auto it = map.find(name); it != map.end()) {
        return it->second;
    }
    return ArtifactCategory::Unknown;
}

void push_artifact_ids_for_category(std::vector<std::string>& out,
                                    ArtifactCategory category,
                                    const SourceArtifact& artifact)
{
    if (artifact.category == category) {
        out.push_back(artifact.id);
    }
}

json artifact_bucket_json(const std::vector<SourceArtifact>& artifacts, ArtifactCategory category)
{
    json bucket = json::array();
    for (const auto& artifact : artifacts) {
        if (artifact.category != category) {
            continue;
        }
        bucket.push_back({
            {"id", artifact.id},
            {"path", artifact.path.generic_string()},
            {"role", to_string(artifact.role)},
            {"optional", artifact.optional},
            {"origin", artifact.origin}
        });
    }
    return bucket;
}

json lef_layer_summary_json(const LefLayerSummary& layer)
{
    json item{{"name", layer.name}, {"type", layer.type}};
    if (layer.width.has_value()) item["width"] = *layer.width;
    if (layer.pitch_x.has_value()) item["pitch_x"] = *layer.pitch_x;
    if (layer.pitch_y.has_value()) item["pitch_y"] = *layer.pitch_y;
    if (layer.direction.has_value()) item["direction"] = *layer.direction;
    return item;
}

json lef_macro_summary_json(const LefMacroSummary& macro)
{
    return json{{"name", macro.name},
                {"macro_class", macro.macro_class},
                {"width", macro.width},
                {"height", macro.height},
                {"pin_count", macro.pin_count},
                {"has_obstruction", macro.has_obstruction}};
}

json lef_library_summary_json(const LefTechnologyData& library)
{
    json item{{"artifact_id", library.artifact_id},
              {"source_path", library.source_path.generic_string()},
              {"version", library.version},
              {"site_count", library.site_count},
              {"layer_count", library.layer_count},
              {"via_count", library.via_count},
              {"macro_count", library.macro_count},
              {"diagnostic_count", library.diagnostic_count},
              {"has_errors", library.has_errors},
              {"layers", json::array()},
              {"macros", json::array()}};
    for (const auto& layer : library.layers) {
        item["layers"].push_back(lef_layer_summary_json(layer));
    }
    for (const auto& macro : library.macros) {
        item["macros"].push_back(lef_macro_summary_json(macro));
    }
    return item;
}

} // namespace

std::string to_string(ArtifactRole role)
{
    switch (role) {
    case ArtifactRole::Lef: return "lef";
    case ArtifactRole::Def: return "def";
    case ArtifactRole::Verilog: return "verilog";
    case ArtifactRole::SystemVerilog: return "systemverilog";
    case ArtifactRole::Spice: return "spice";
    case ArtifactRole::Spi: return "spi";
    case ArtifactRole::Cdl: return "cdl";
    case ArtifactRole::AegisRulePack: return "aegis_rule_pack";
    case ArtifactRole::PowerDomainsCsv: return "power_domains_csv";
    case ArtifactRole::CurrentCsv: return "current_csv";
    case ArtifactRole::WaiverCsv: return "waiver_csv";
    case ArtifactRole::WaiverYaml: return "waiver_yaml";
    case ArtifactRole::WaiverJson: return "waiver_json";
    case ArtifactRole::ImportedReport: return "imported_report";
    case ArtifactRole::Unknown: break;
    }
    return "unknown";
}

std::string to_string(ArtifactCategory category)
{
    switch (category) {
    case ArtifactCategory::Technology: return "technology";
    case ArtifactCategory::Layout: return "layout";
    case ArtifactCategory::Netlist: return "netlist";
    case ArtifactCategory::Rules: return "rules";
    case ArtifactCategory::Power: return "power";
    case ArtifactCategory::Current: return "current";
    case ArtifactCategory::Waivers: return "waivers";
    case ArtifactCategory::ExternalReports: return "external_reports";
    case ArtifactCategory::Unknown: break;
    }
    return "unknown";
}

std::string to_string(ValidationStatus status)
{
    switch (status) {
    case ValidationStatus::Valid: return "valid";
    case ValidationStatus::Warning: return "warning";
    case ValidationStatus::Invalid: return "invalid";
    case ValidationStatus::Unknown: break;
    }
    return "unknown";
}

std::string to_string(DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::Info: return "info";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    }
    return "info";
}

ArtifactRole artifact_role_from_string(const std::string& value)
{
    static const std::unordered_map<std::string, ArtifactRole> map{
        {"lef", ArtifactRole::Lef},
        {"def", ArtifactRole::Def},
        {"verilog", ArtifactRole::Verilog},
        {"systemverilog", ArtifactRole::SystemVerilog},
        {"spice", ArtifactRole::Spice},
        {"spi", ArtifactRole::Spi},
        {"cdl", ArtifactRole::Cdl},
        {"aegis_rule_pack", ArtifactRole::AegisRulePack},
        {"power_domains_csv", ArtifactRole::PowerDomainsCsv},
        {"current_csv", ArtifactRole::CurrentCsv},
        {"waiver_csv", ArtifactRole::WaiverCsv},
        {"waiver_yaml", ArtifactRole::WaiverYaml},
        {"waiver_json", ArtifactRole::WaiverJson},
        {"imported_report", ArtifactRole::ImportedReport},
    };
    if (const auto it = map.find(value); it != map.end()) {
        return it->second;
    }
    return ArtifactRole::Unknown;
}

ArtifactCategory artifact_category_from_string(const std::string& value)
{
    return category_from_bucket_name(value);
}

ValidationStatus validation_status_from_string(const std::string& value)
{
    if (value == "valid") return ValidationStatus::Valid;
    if (value == "warning") return ValidationStatus::Warning;
    if (value == "invalid") return ValidationStatus::Invalid;
    return ValidationStatus::Unknown;
}

DiagnosticSeverity diagnostic_severity_from_string(const std::string& value)
{
    if (value == "warning") return DiagnosticSeverity::Warning;
    if (value == "error") return DiagnosticSeverity::Error;
    return DiagnosticSeverity::Info;
}

ArtifactCategory category_for_role(ArtifactRole role)
{
    switch (role) {
    case ArtifactRole::Lef: return ArtifactCategory::Technology;
    case ArtifactRole::Def: return ArtifactCategory::Layout;
    case ArtifactRole::Verilog:
    case ArtifactRole::SystemVerilog:
    case ArtifactRole::Spice:
    case ArtifactRole::Spi:
    case ArtifactRole::Cdl: return ArtifactCategory::Netlist;
    case ArtifactRole::AegisRulePack: return ArtifactCategory::Rules;
    case ArtifactRole::PowerDomainsCsv: return ArtifactCategory::Power;
    case ArtifactRole::CurrentCsv: return ArtifactCategory::Current;
    case ArtifactRole::WaiverCsv:
    case ArtifactRole::WaiverYaml:
    case ArtifactRole::WaiverJson: return ArtifactCategory::Waivers;
    case ArtifactRole::ImportedReport: return ArtifactCategory::ExternalReports;
    case ArtifactRole::Unknown: break;
    }
    return ArtifactCategory::Unknown;
}

ProjectPackage ProjectPackage::from_manifest_json(const std::string& json_text)
{
    const json root = json::parse(json_text);

    ProjectPackage pkg;
    pkg.m_manifest_version = root.value("manifest_version", 1);
    if (root.contains("project")) {
        const auto& project = root.at("project");
        pkg.m_project.name = project.value("name", "");
        pkg.m_project.description = project.value("description", "");
        pkg.m_project.customer = project.value("customer", "");
        pkg.m_project.design_stage = project.value("design_stage", "");
    }

    if (root.contains("validation")) {
        const auto& validation = root.at("validation");
        pkg.m_validation_status = validation_status_from_string(
            validation.value("status", "unknown"));
    }

    if (root.contains("artifacts")) {
        const auto& artifacts = root.at("artifacts");
        for (auto it = artifacts.begin(); it != artifacts.end(); ++it) {
            const ArtifactCategory bucket_category = category_from_bucket_name(it.key());
            if (!it.value().is_array()) {
                throw std::runtime_error("Artifact bucket '" + it.key() + "' must be an array");
            }
            std::size_t ordinal = 0;
            for (const auto& item : it.value()) {
                SourceArtifact artifact;
                artifact.path = item.at("path").get<std::string>();
                artifact.role = artifact_role_from_string(item.value("role", "unknown"));
                artifact.category = bucket_category != ArtifactCategory::Unknown
                    ? bucket_category
                    : category_for_role(artifact.role);
                artifact.optional = item.value("optional", false);
                artifact.origin = item.value("origin", std::string("manifest"));
                artifact.id = item.value("id", make_generated_artifact_id(artifact.category, ordinal));
                ++ordinal;
                pkg.m_artifacts.push_back(std::move(artifact));
            }
        }
    }

    if (root.contains("normalized") && root.at("normalized").is_object()) {
        const auto& normalized = root.at("normalized");
        if (normalized.contains("lef_libraries") && normalized.at("lef_libraries").is_array()) {
            for (const auto& item : normalized.at("lef_libraries")) {
                LefTechnologyData library;
                library.artifact_id = item.value("artifact_id", "");
                library.source_path = item.value("source_path", "");
                library.version = item.value("version", "");
                library.site_count = item.value("site_count", std::size_t{0});
                library.layer_count = item.value("layer_count", std::size_t{0});
                library.via_count = item.value("via_count", std::size_t{0});
                library.macro_count = item.value("macro_count", std::size_t{0});
                library.diagnostic_count = item.value("diagnostic_count", std::size_t{0});
                library.has_errors = item.value("has_errors", false);
                if (item.contains("layers") && item.at("layers").is_array()) {
                    for (const auto& layer_item : item.at("layers")) {
                        LefLayerSummary layer;
                        layer.name = layer_item.value("name", "");
                        layer.type = layer_item.value("type", "");
                        if (layer_item.contains("width")) layer.width = layer_item.at("width").get<double>();
                        if (layer_item.contains("pitch_x")) layer.pitch_x = layer_item.at("pitch_x").get<double>();
                        if (layer_item.contains("pitch_y")) layer.pitch_y = layer_item.at("pitch_y").get<double>();
                        if (layer_item.contains("direction")) layer.direction = layer_item.at("direction").get<std::string>();
                        library.layers.push_back(std::move(layer));
                    }
                }
                if (item.contains("macros") && item.at("macros").is_array()) {
                    for (const auto& macro_item : item.at("macros")) {
                        LefMacroSummary macro;
                        macro.name = macro_item.value("name", "");
                        macro.macro_class = macro_item.value("macro_class", "");
                        macro.width = macro_item.value("width", 0.0);
                        macro.height = macro_item.value("height", 0.0);
                        macro.pin_count = macro_item.value("pin_count", std::size_t{0});
                        macro.has_obstruction = macro_item.value("has_obstruction", false);
                        library.macros.push_back(std::move(macro));
                    }
                }
                pkg.m_normalized.lef_libraries.push_back(std::move(library));
            }
        }
    }

    if (root.contains("diagnostics") && root.at("diagnostics").is_array()) {
        for (const auto& item : root.at("diagnostics")) {
            ImportDiagnostic diagnostic;
            diagnostic.severity = diagnostic_severity_from_string(item.value("severity", "info"));
            diagnostic.code = item.value("code", "");
            diagnostic.message = item.value("message", "");
            if (item.contains("artifact_id")) {
                diagnostic.artifact_id = item.at("artifact_id").get<std::string>();
            }
            pkg.m_diagnostics.push_back(std::move(diagnostic));
        }
    }

    pkg.rebuild_normalized_view();
    return pkg;
}

ProjectPackage ProjectPackage::from_manifest_file(const std::filesystem::path& path)
{
    return from_manifest_json(read_text_file(path));
}

std::string ProjectPackage::to_manifest_json(int indent) const
{
    json root;
    root["manifest_version"] = m_manifest_version;
    root["project"] = {
        {"name", m_project.name},
        {"description", m_project.description},
        {"customer", m_project.customer},
        {"design_stage", m_project.design_stage}
    };

    root["artifacts"] = {
        {"technology", artifact_bucket_json(m_artifacts, ArtifactCategory::Technology)},
        {"layout", artifact_bucket_json(m_artifacts, ArtifactCategory::Layout)},
        {"netlist", artifact_bucket_json(m_artifacts, ArtifactCategory::Netlist)},
        {"rules", artifact_bucket_json(m_artifacts, ArtifactCategory::Rules)},
        {"power", artifact_bucket_json(m_artifacts, ArtifactCategory::Power)},
        {"current", artifact_bucket_json(m_artifacts, ArtifactCategory::Current)},
        {"waivers", artifact_bucket_json(m_artifacts, ArtifactCategory::Waivers)},
        {"external_reports", artifact_bucket_json(m_artifacts, ArtifactCategory::ExternalReports)}
    };

    root["validation"] = {
        {"status", to_string(m_validation_status)}
    };

    root["normalized"] = {
        {"technology_artifact_ids", m_normalized.technology_artifact_ids},
        {"layout_artifact_ids", m_normalized.layout_artifact_ids},
        {"netlist_artifact_ids", m_normalized.netlist_artifact_ids},
        {"rule_artifact_ids", m_normalized.rule_artifact_ids},
        {"power_artifact_ids", m_normalized.power_artifact_ids},
        {"current_artifact_ids", m_normalized.current_artifact_ids},
        {"waiver_artifact_ids", m_normalized.waiver_artifact_ids},
        {"external_report_artifact_ids", m_normalized.external_report_artifact_ids},
        {"lef_libraries", json::array()}
    };
    for (const auto& library : m_normalized.lef_libraries) {
        root["normalized"]["lef_libraries"].push_back(lef_library_summary_json(library));
    }

    root["diagnostics"] = json::array();
    for (const auto& diagnostic : m_diagnostics) {
        json item{
            {"severity", to_string(diagnostic.severity)},
            {"code", diagnostic.code},
            {"message", diagnostic.message}
        };
        if (diagnostic.artifact_id.has_value()) {
            item["artifact_id"] = *diagnostic.artifact_id;
        }
        root["diagnostics"].push_back(std::move(item));
    }

    return root.dump(indent);
}

void ProjectPackage::set_manifest_version(int version)
{
    m_manifest_version = version;
}

int ProjectPackage::manifest_version() const noexcept
{
    return m_manifest_version;
}

ProjectMetadata& ProjectPackage::project() noexcept
{
    return m_project;
}

const ProjectMetadata& ProjectPackage::project() const noexcept
{
    return m_project;
}

std::vector<SourceArtifact>& ProjectPackage::artifacts() noexcept
{
    return m_artifacts;
}

const std::vector<SourceArtifact>& ProjectPackage::artifacts() const noexcept
{
    return m_artifacts;
}

NormalizedProjectData& ProjectPackage::normalized() noexcept
{
    return m_normalized;
}

const NormalizedProjectData& ProjectPackage::normalized() const noexcept
{
    return m_normalized;
}

std::vector<ImportDiagnostic>& ProjectPackage::diagnostics() noexcept
{
    return m_diagnostics;
}

const std::vector<ImportDiagnostic>& ProjectPackage::diagnostics() const noexcept
{
    return m_diagnostics;
}

ValidationStatus ProjectPackage::validation_status() const noexcept
{
    return m_validation_status;
}

void ProjectPackage::set_validation_status(ValidationStatus status) noexcept
{
    m_validation_status = status;
}

void ProjectPackage::rebuild_normalized_view()
{
    const auto existing_lef_libraries = m_normalized.lef_libraries;
    m_normalized = {};
    m_normalized.lef_libraries = existing_lef_libraries;
    for (const auto& artifact : m_artifacts) {
        push_artifact_ids_for_category(m_normalized.technology_artifact_ids,
                                       ArtifactCategory::Technology,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.layout_artifact_ids,
                                       ArtifactCategory::Layout,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.netlist_artifact_ids,
                                       ArtifactCategory::Netlist,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.rule_artifact_ids,
                                       ArtifactCategory::Rules,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.power_artifact_ids,
                                       ArtifactCategory::Power,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.current_artifact_ids,
                                       ArtifactCategory::Current,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.waiver_artifact_ids,
                                       ArtifactCategory::Waivers,
                                       artifact);
        push_artifact_ids_for_category(m_normalized.external_report_artifact_ids,
                                       ArtifactCategory::ExternalReports,
                                       artifact);
    }
}

const SourceArtifact* ProjectPackage::find_artifact_by_id(const std::string& id) const
{
    for (const auto& artifact : m_artifacts) {
        if (artifact.id == id) {
            return &artifact;
        }
    }
    return nullptr;
}

const LefTechnologyData* ProjectPackage::find_lef_technology_by_artifact_id(const std::string& id) const
{
    for (const auto& library : m_normalized.lef_libraries) {
        if (library.artifact_id == id) {
            return &library;
        }
    }
    return nullptr;
}

} // namespace aegis::storage
