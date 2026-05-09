#include "aegis/storage/import_validation.hpp"

#include "aegis/parsing/spice_parser.hpp"
#include "aegis/parsing/verilog_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace aegis::storage {
namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string extension_of(const std::filesystem::path& path)
{
    return to_lower(path.extension().string());
}

std::string read_head(const std::filesystem::path& path, std::size_t max_lines = 8)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream out;
    std::string line;
    std::size_t count = 0;
    while (count < max_lines && std::getline(input, line)) {
        out << line << '\n';
        ++count;
    }
    return to_lower(out.str());
}

void add_candidate(std::vector<RoleDetectionCandidate>& out,
                   ArtifactRole role,
                   double confidence,
                   std::string reason)
{
    out.push_back({role, category_for_role(role), confidence, std::move(reason)});
}

bool contains_all(const std::string& haystack, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (haystack.find(needle) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::string make_artifact_id(std::size_t index)
{
    return "artifact-" + std::to_string(index + 1);
}

void push_error(std::vector<ImportDiagnostic>& diagnostics,
                std::string code,
                std::string message,
                std::optional<std::string> artifact_id = std::nullopt)
{
    diagnostics.push_back({DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(artifact_id)});
}

void push_warning(std::vector<ImportDiagnostic>& diagnostics,
                  std::string code,
                  std::string message,
                  std::optional<std::string> artifact_id = std::nullopt)
{
    diagnostics.push_back({DiagnosticSeverity::Warning, std::move(code), std::move(message), std::move(artifact_id)});
}

} // namespace

const RoleDetectionCandidate* FileRoleDetection::best() const noexcept
{
    if (candidates.empty()) {
        return nullptr;
    }
    return &*std::max_element(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.confidence < b.confidence;
    });
}

bool FileRoleDetection::is_ambiguous() const noexcept
{
    if (candidates.size() < 2) {
        return false;
    }
    const auto* first = best();
    if (!first) {
        return false;
    }
    int same_top = 0;
    for (const auto& candidate : candidates) {
        if (candidate.confidence == first->confidence) {
            ++same_top;
        }
    }
    return same_top > 1;
}

FileRoleDetection ImportPreflightValidator::detect_file_role(const std::filesystem::path& path) const
{
    FileRoleDetection detection;
    detection.path = path;

    const std::string ext = extension_of(path);
    const std::string head = read_head(path);
    const std::string parent = to_lower(path.parent_path().filename().string());

    if (ext == ".lef") add_candidate(detection.candidates, ArtifactRole::Lef, 0.98, "Extension .lef");
    if (ext == ".def") add_candidate(detection.candidates, ArtifactRole::Def, 0.98, "Extension .def");
    if (ext == ".sv") add_candidate(detection.candidates, ArtifactRole::SystemVerilog, 0.95, "Extension .sv");
    if (ext == ".v") add_candidate(detection.candidates, ArtifactRole::Verilog, 0.92, "Extension .v");
    if (ext == ".sp") add_candidate(detection.candidates, ArtifactRole::Spice, 0.92, "Extension .sp");
    if (ext == ".spi") add_candidate(detection.candidates, ArtifactRole::Spi, 0.92, "Extension .spi");
    if (ext == ".cdl") add_candidate(detection.candidates, ArtifactRole::Cdl, 0.92, "Extension .cdl");

    if (ext == ".yaml" || ext == ".yml") {
        if (head.find("rules:") != std::string::npos || head.find("rule_id:") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::AegisRulePack, 0.96, "YAML content contains rule markers");
        }
        if (head.find("waiver") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::WaiverYaml, 0.75, "YAML content contains waiver marker");
        }
    }

    if (ext == ".json") {
        if (head.find("\"rules\"") != std::string::npos || head.find("\"rule_id\"") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::AegisRulePack, 0.90, "JSON content contains rule markers");
        }
        if (head.find("waiver") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::WaiverJson, 0.75, "JSON content contains waiver marker");
        }
        if (head.find("manifest_version") != std::string::npos && head.find("artifacts") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::Unknown, 0.40, "Looks like AEGIS manifest");
        }
    }

    if (ext == ".csv") {
        if (contains_all(head, {"instance", "domain", "voltage"})) {
            add_candidate(detection.candidates, ArtifactRole::PowerDomainsCsv, 0.97, "CSV header matches power-domain schema");
        }
        if (contains_all(head, {"net_name", "current"})) {
            add_candidate(detection.candidates, ArtifactRole::CurrentCsv, 0.97, "CSV header matches current/activity schema");
        }
        if (head.find("waiver") != std::string::npos) {
            add_candidate(detection.candidates, ArtifactRole::WaiverCsv, 0.78, "CSV content contains waiver marker");
        }
        if (detection.candidates.empty()) {
            add_candidate(detection.candidates, ArtifactRole::ImportedReport, 0.55, "Generic CSV report fallback");
        }
    }

    if (ext == ".rpt" || ext == ".txt" || ext == ".xml") {
        add_candidate(detection.candidates, ArtifactRole::ImportedReport, 0.75, "Report-like extension");
    }

    if (parent == "rules") add_candidate(detection.candidates, ArtifactRole::AegisRulePack, 0.45, "Parent folder suggests rules");
    if (parent == "power") add_candidate(detection.candidates, ArtifactRole::PowerDomainsCsv, 0.35, "Parent folder suggests power data");
    if (parent == "reports") add_candidate(detection.candidates, ArtifactRole::ImportedReport, 0.30, "Parent folder suggests reports");
    if (parent == "waivers") add_candidate(detection.candidates, ArtifactRole::WaiverCsv, 0.30, "Parent folder suggests waivers");

    std::sort(detection.candidates.begin(), detection.candidates.end(), [](const auto& a, const auto& b) {
        return a.confidence > b.confidence;
    });
    return detection;
}

ProjectPackage ImportPreflightValidator::scan_project_folder(const std::filesystem::path& root,
                                                             const std::string& project_name) const
{
    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = project_name.empty() ? root.filename().string() : project_name;

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        package.diagnostics().push_back({DiagnosticSeverity::Error,
                                         "IMPORT_ROOT_INVALID",
                                         "Project folder does not exist or is not a directory",
                                         std::nullopt});
        package.set_validation_status(ValidationStatus::Invalid);
        return package;
    }

    std::size_t index = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(root); it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) {
            continue;
        }

        const auto relative = std::filesystem::relative(it->path(), root);
        const auto detection = detect_file_role(it->path());
        const auto* best = detection.best();

        if (!best || best->role == ArtifactRole::Unknown) {
            push_warning(package.diagnostics(),
                         "UNKNOWN_ARTIFACT",
                         "Unable to classify file '" + relative.generic_string() + "'");
            continue;
        }

        SourceArtifact artifact;
        artifact.id = make_artifact_id(index++);
        artifact.path = relative;
        artifact.role = best->role;
        artifact.category = best->category;
        artifact.origin = "scan";
        artifact.optional = artifact.category == ArtifactCategory::Power ||
                            artifact.category == ArtifactCategory::Current ||
                            artifact.category == ArtifactCategory::Waivers ||
                            artifact.category == ArtifactCategory::ExternalReports;
        package.artifacts().push_back(artifact);

        if (detection.is_ambiguous()) {
            push_warning(package.diagnostics(),
                         "AMBIGUOUS_ROLE",
                         "Multiple role candidates detected for '" + relative.generic_string() + "'; selected '" + to_string(best->role) + "'",
                         artifact.id);
        }

        if (best->role == ArtifactRole::Verilog || best->role == ArtifactRole::SystemVerilog) {
            try {
                aegis::parsing::VerilogParser parser;
                (void)parser.parse_to_layout_ir(it->path());
            } catch (const std::exception& ex) {
                push_error(package.diagnostics(),
                           "VERILOG_PARSE_FAILED",
                           "Failed to parse Verilog netlist '" + relative.generic_string() + "': " + ex.what(),
                           artifact.id);
            }
        }
        if (best->role == ArtifactRole::Spice || best->role == ArtifactRole::Spi || best->role == ArtifactRole::Cdl) {
            try {
                aegis::parsing::SpiceParser parser;
                (void)parser.parse_to_layout_ir(it->path());
            } catch (const std::exception& ex) {
                push_error(package.diagnostics(),
                           "SPICE_PARSE_FAILED",
                           "Failed to parse SPICE/CDL netlist '" + relative.generic_string() + "': " + ex.what(),
                           artifact.id);
            }
        }
    }

    package.rebuild_normalized_view();
    const auto validation = validate(package);
    package.diagnostics().insert(package.diagnostics().end(), validation.begin(), validation.end());
    package.set_validation_status(derive_status(package.diagnostics()));
    return package;
}

std::vector<ImportDiagnostic> ImportPreflightValidator::validate(const ProjectPackage& package) const
{
    std::vector<ImportDiagnostic> diagnostics;

    const auto count_category = [&](ArtifactCategory category) {
        return static_cast<int>(std::count_if(package.artifacts().begin(), package.artifacts().end(), [&](const auto& artifact) {
            return artifact.category == category;
        }));
    };

    const int technology_count = count_category(ArtifactCategory::Technology);
    const int layout_count = count_category(ArtifactCategory::Layout);
    const int netlist_count = count_category(ArtifactCategory::Netlist);
    const int rules_count = count_category(ArtifactCategory::Rules);

    if (technology_count == 0) push_error(diagnostics, "MISSING_REQUIRED_TECHNOLOGY", "Missing required technology/library artifact (.lef)");
    if (layout_count == 0) push_error(diagnostics, "MISSING_REQUIRED_LAYOUT", "Missing required layout artifact (.def)");
    if (netlist_count == 0) push_error(diagnostics, "MISSING_REQUIRED_NETLIST", "Missing required supported netlist artifact");
    if (rules_count == 0) push_error(diagnostics, "MISSING_REQUIRED_RULES", "Missing required AEGIS rule pack artifact");

    if (technology_count > 1) push_warning(diagnostics, "MULTIPLE_TECHNOLOGY_FILES", "Multiple technology/library artifacts detected");
    if (layout_count > 1) push_warning(diagnostics, "MULTIPLE_LAYOUT_FILES", "Multiple layout artifacts detected");
    if (netlist_count > 1) push_warning(diagnostics, "MULTIPLE_NETLIST_FILES", "Multiple supported netlist artifacts detected");
    if (rules_count > 1) push_warning(diagnostics, "MULTIPLE_RULE_PACKS", "Multiple rule pack artifacts detected");

    std::unordered_map<std::string, std::string> seen_paths;
    for (const auto& artifact : package.artifacts()) {
        const std::string path_key = artifact.path.generic_string();
        if (const auto it = seen_paths.find(path_key); it != seen_paths.end()) {
            push_error(diagnostics,
                       "DUPLICATE_ARTIFACT_PATH",
                       "Artifact path '" + path_key + "' is assigned multiple times",
                       artifact.id);
        } else {
            seen_paths[path_key] = artifact.id;
        }

        if (artifact.category == ArtifactCategory::Unknown || artifact.role == ArtifactRole::Unknown) {
            push_warning(diagnostics,
                         "UNKNOWN_ROLE_ASSIGNMENT",
                         "Artifact '" + path_key + "' has unknown role/category assignment",
                         artifact.id);
        }
    }

    return diagnostics;
}

ValidationStatus ImportPreflightValidator::derive_status(const std::vector<ImportDiagnostic>& diagnostics) const noexcept
{
    bool has_warning = false;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return ValidationStatus::Invalid;
        }
        if (diagnostic.severity == DiagnosticSeverity::Warning) {
            has_warning = true;
        }
    }
    return has_warning ? ValidationStatus::Warning : ValidationStatus::Valid;
}

} // namespace aegis::storage
