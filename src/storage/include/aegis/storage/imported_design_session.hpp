#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/lef_parser.hpp"
#include "aegis/parsing/power_intent.hpp"
#include "aegis/storage/project_package.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aegis::storage {

enum class ImportedDesignObjectKind {
    Layer,
    Instance,
    Port,
    Net,
    Device,
    TechnologyMacro
};

enum class SessionDiagnosticSeverity {
    Info,
    Warning,
    Error
};

enum class SessionBuildStatus {
    Ready,
    Warning,
    Error
};

enum class SessionBuildStage {
    ResolveArtifacts,
    TechnologyParse,
    LayoutParse,
    NetlistParse,
    RuleLink,
    GraphBuild,
    PowerEnrichment,
    CurrentEnrichment
};

struct SourceProvenance {
    std::string artifact_id;
    std::filesystem::path artifact_path;
    ArtifactRole role = ArtifactRole::Unknown;
    ArtifactCategory category = ArtifactCategory::Unknown;
    std::string origin;
    std::string parser_name;
    std::optional<std::size_t> source_line;
    std::optional<std::size_t> source_row;
};

struct SessionDiagnostic {
    SessionDiagnosticSeverity severity = SessionDiagnosticSeverity::Info;
    SessionBuildStage stage = SessionBuildStage::ResolveArtifacts;
    std::string code;
    std::string message;
    std::optional<std::string> artifact_id;
    std::optional<std::size_t> source_line;
    std::optional<std::size_t> source_row;
};

struct ImportedDesignObject {
    std::string stable_id;
    ImportedDesignObjectKind kind = ImportedDesignObjectKind::Layer;
    std::string name;
    std::string display_name;
    SourceProvenance provenance;
    std::map<std::string, std::string> metadata;
};

class ImportedDesignSession {
public:
    ImportedDesignSession();
    ~ImportedDesignSession();

    ImportedDesignSession(const ImportedDesignSession&) = delete;
    ImportedDesignSession& operator=(const ImportedDesignSession&) = delete;
    ImportedDesignSession(ImportedDesignSession&&) noexcept;
    ImportedDesignSession& operator=(ImportedDesignSession&&) noexcept;

    ProjectPackage& package() noexcept;
    const ProjectPackage& package() const noexcept;

    std::filesystem::path& base_path() noexcept;
    const std::filesystem::path& base_path() const noexcept;

    std::vector<aegis::parsing::LefLibraryData>& technology_libraries() noexcept;
    const std::vector<aegis::parsing::LefLibraryData>& technology_libraries() const noexcept;

    aegis::parsing::LayoutIR& physical_ir() noexcept;
    const aegis::parsing::LayoutIR& physical_ir() const noexcept;

    aegis::parsing::LayoutIR& logical_ir() noexcept;
    const aegis::parsing::LayoutIR& logical_ir() const noexcept;

    aegis::parsing::LayoutIR& combined_ir() noexcept;
    const aegis::parsing::LayoutIR& combined_ir() const noexcept;

    std::optional<aegis::parsing::PowerIntentData>& power_intent() noexcept;
    const std::optional<aegis::parsing::PowerIntentData>& power_intent() const noexcept;

    std::optional<aegis::parsing::CurrentActivityData>& current_activity() noexcept;
    const std::optional<aegis::parsing::CurrentActivityData>& current_activity() const noexcept;

    aegis::graph::ConnectivityGraph& graph() noexcept;
    const aegis::graph::ConnectivityGraph& graph() const noexcept;

    std::vector<std::string>& unresolved_graph_references() noexcept;
    const std::vector<std::string>& unresolved_graph_references() const noexcept;

    std::vector<ImportedDesignObject>& objects() noexcept;
    const std::vector<ImportedDesignObject>& objects() const noexcept;

    std::vector<SessionDiagnostic>& diagnostics() noexcept;
    const std::vector<SessionDiagnostic>& diagnostics() const noexcept;

    std::vector<std::string>& rule_artifact_ids() noexcept;
    const std::vector<std::string>& rule_artifact_ids() const noexcept;

    std::vector<std::filesystem::path>& rule_artifact_paths() noexcept;
    const std::vector<std::filesystem::path>& rule_artifact_paths() const noexcept;

    [[nodiscard]] SessionBuildStatus status() const noexcept;
    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] std::size_t object_count(ImportedDesignObjectKind kind) const noexcept;
    [[nodiscard]] const ImportedDesignObject* find_object_by_stable_id(const std::string& stable_id) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class ImportedDesignSessionBuilder {
public:
    ImportedDesignSession build(const ProjectPackage& package,
                                const std::filesystem::path& base_path) const;
};

std::string to_string(ImportedDesignObjectKind kind);
std::string to_string(SessionDiagnosticSeverity severity);
std::string to_string(SessionBuildStatus status);
std::string to_string(SessionBuildStage stage);

} // namespace aegis::storage
