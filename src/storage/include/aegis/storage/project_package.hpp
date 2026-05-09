#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::storage {

enum class ArtifactRole {
    Unknown,
    Lef,
    Def,
    Verilog,
    SystemVerilog,
    Spice,
    Spi,
    Cdl,
    AegisRulePack,
    PowerDomainsCsv,
    CurrentCsv,
    WaiverCsv,
    WaiverYaml,
    WaiverJson,
    ImportedReport
};

enum class ArtifactCategory {
    Unknown,
    Technology,
    Layout,
    Netlist,
    Rules,
    Power,
    Current,
    Waivers,
    ExternalReports
};

enum class ValidationStatus {
    Unknown,
    Valid,
    Warning,
    Invalid
};

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct ProjectMetadata {
    std::string name;
    std::string description;
    std::string customer;
    std::string design_stage;
};

struct SourceArtifact {
    std::string id;
    std::filesystem::path path;
    ArtifactRole role = ArtifactRole::Unknown;
    ArtifactCategory category = ArtifactCategory::Unknown;
    bool optional = false;
    std::string origin = "manifest";
};

struct ImportDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string code;
    std::string message;
    std::optional<std::string> artifact_id;
};

struct LefLayerSummary {
    std::string name;
    std::string type;
    std::optional<double> width;
    std::optional<double> pitch_x;
    std::optional<double> pitch_y;
    std::optional<std::string> direction;
};

struct LefMacroSummary {
    std::string name;
    std::string macro_class;
    double width = 0.0;
    double height = 0.0;
    std::size_t pin_count = 0;
    bool has_obstruction = false;
};

struct LefTechnologyData {
    std::string artifact_id;
    std::filesystem::path source_path;
    std::string version;
    std::size_t site_count = 0;
    std::size_t layer_count = 0;
    std::size_t via_count = 0;
    std::size_t macro_count = 0;
    std::size_t diagnostic_count = 0;
    bool has_errors = false;
    std::vector<LefLayerSummary> layers;
    std::vector<LefMacroSummary> macros;
};

struct NormalizedProjectData {
    std::vector<std::string> technology_artifact_ids;
    std::vector<std::string> layout_artifact_ids;
    std::vector<std::string> netlist_artifact_ids;
    std::vector<std::string> rule_artifact_ids;
    std::vector<std::string> power_artifact_ids;
    std::vector<std::string> current_artifact_ids;
    std::vector<std::string> waiver_artifact_ids;
    std::vector<std::string> external_report_artifact_ids;
    std::vector<LefTechnologyData> lef_libraries;
};

class ProjectPackage {
public:
    static ProjectPackage from_manifest_json(const std::string& json_text);
    static ProjectPackage from_manifest_file(const std::filesystem::path& path);

    std::string to_manifest_json(int indent = 2) const;

    void set_manifest_version(int version);
    int manifest_version() const noexcept;

    ProjectMetadata& project() noexcept;
    const ProjectMetadata& project() const noexcept;

    std::vector<SourceArtifact>& artifacts() noexcept;
    const std::vector<SourceArtifact>& artifacts() const noexcept;

    NormalizedProjectData& normalized() noexcept;
    const NormalizedProjectData& normalized() const noexcept;

    std::vector<ImportDiagnostic>& diagnostics() noexcept;
    const std::vector<ImportDiagnostic>& diagnostics() const noexcept;

    ValidationStatus validation_status() const noexcept;
    void set_validation_status(ValidationStatus status) noexcept;

    void rebuild_normalized_view();
    const SourceArtifact* find_artifact_by_id(const std::string& id) const;
    const LefTechnologyData* find_lef_technology_by_artifact_id(const std::string& id) const;

private:
    int m_manifest_version = 1;
    ProjectMetadata m_project;
    std::vector<SourceArtifact> m_artifacts;
    NormalizedProjectData m_normalized;
    std::vector<ImportDiagnostic> m_diagnostics;
    ValidationStatus m_validation_status = ValidationStatus::Unknown;
};

std::string to_string(ArtifactRole role);
std::string to_string(ArtifactCategory category);
std::string to_string(ValidationStatus status);
std::string to_string(DiagnosticSeverity severity);

ArtifactRole artifact_role_from_string(const std::string& value);
ArtifactCategory artifact_category_from_string(const std::string& value);
ValidationStatus validation_status_from_string(const std::string& value);
DiagnosticSeverity diagnostic_severity_from_string(const std::string& value);
ArtifactCategory category_for_role(ArtifactRole role);

} // namespace aegis::storage
