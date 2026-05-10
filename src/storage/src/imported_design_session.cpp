#include "aegis/storage/imported_design_session.hpp"

#include "aegis/graph/current_activity_application.hpp"
#include "aegis/graph/power_intent_application.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/parsing/def_parser.hpp"
#include "aegis/parsing/power_intent.hpp"
#include "aegis/parsing/spice_parser.hpp"
#include "aegis/parsing/verilog_parser.hpp"

#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace aegis::storage {
namespace {

using aegis::graph::ConnectivityGraph;
using aegis::graph::EdgeType;
using aegis::graph::NetNode;
using aegis::graph::PinNode;
using aegis::parsing::CurrentActivityData;
using aegis::parsing::CurrentActivityDiagnostic;
using aegis::parsing::CurrentActivityParser;
using aegis::parsing::DefParser;
using aegis::parsing::LayoutIR;
using aegis::parsing::LefDiagnostic;
using aegis::parsing::LefLibraryData;
using aegis::parsing::LefParser;
using aegis::parsing::PowerIntentData;
using aegis::parsing::PowerIntentDiagnostic;
using aegis::parsing::PowerIntentParser;
using aegis::parsing::SpiceParser;
using aegis::parsing::VerilogParser;

std::filesystem::path resolve_artifact_path(const std::filesystem::path& base_path,
                                            const SourceArtifact& artifact)
{
    if (artifact.path.is_absolute()) {
        return artifact.path;
    }
    return base_path / artifact.path;
}

LayoutIR append_ir(LayoutIR combined, const LayoutIR& next)
{
    if (combined.design_name.empty()) {
        combined.design_name = next.design_name;
    }
    if (combined.description.empty()) {
        combined.description = next.description;
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

SessionDiagnostic make_diagnostic(SessionDiagnosticSeverity severity,
                                  SessionBuildStage stage,
                                  std::string code,
                                  std::string message,
                                  std::optional<std::string> artifact_id = std::nullopt,
                                  std::optional<std::size_t> source_line = std::nullopt,
                                  std::optional<std::size_t> source_row = std::nullopt)
{
    SessionDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.stage = stage;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.artifact_id = std::move(artifact_id);
    diagnostic.source_line = source_line;
    diagnostic.source_row = source_row;
    return diagnostic;
}

SessionDiagnosticSeverity to_session_severity(DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::Warning: return SessionDiagnosticSeverity::Warning;
    case DiagnosticSeverity::Error: return SessionDiagnosticSeverity::Error;
    case DiagnosticSeverity::Info: break;
    }
    return SessionDiagnosticSeverity::Info;
}

SessionDiagnosticSeverity to_session_severity(LefDiagnostic::Severity severity)
{
    switch (severity) {
    case LefDiagnostic::Severity::Warning: return SessionDiagnosticSeverity::Warning;
    case LefDiagnostic::Severity::Error: return SessionDiagnosticSeverity::Error;
    case LefDiagnostic::Severity::Info: break;
    }
    return SessionDiagnosticSeverity::Info;
}

SessionDiagnosticSeverity to_session_severity(CurrentActivityDiagnostic::Severity severity)
{
    return severity == CurrentActivityDiagnostic::Severity::Error
        ? SessionDiagnosticSeverity::Error
        : SessionDiagnosticSeverity::Warning;
}

SessionDiagnosticSeverity to_session_severity(PowerIntentDiagnostic::Severity severity)
{
    return severity == PowerIntentDiagnostic::Severity::Error
        ? SessionDiagnosticSeverity::Error
        : SessionDiagnosticSeverity::Warning;
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
        throw std::runtime_error("Unsupported netlist role for imported design session: '" + to_string(role) + "'");
    }
}

std::string make_stable_id(ImportedDesignObjectKind kind,
                           const std::string& artifact_id,
                           const std::string& name,
                           std::unordered_map<std::string, std::size_t>& counters)
{
    const std::string key = to_string(kind) + ":" + artifact_id + ":" + name;
    const std::size_t ordinal = ++counters[key];
    std::ostringstream id;
    id << artifact_id << ":" << to_string(kind) << ":" << name;
    if (ordinal > 1) {
        id << "#" << ordinal;
    }
    return id.str();
}

void add_object(std::vector<ImportedDesignObject>& out,
                std::unordered_map<std::string, std::size_t>& counters,
                ImportedDesignObjectKind kind,
                std::string name,
                std::string display_name,
                SourceProvenance provenance,
                std::map<std::string, std::string> metadata = {})
{
    ImportedDesignObject object;
    object.kind = kind;
    object.name = std::move(name);
    object.display_name = std::move(display_name);
    object.provenance = std::move(provenance);
    object.metadata = std::move(metadata);
    object.stable_id = make_stable_id(kind, object.provenance.artifact_id, object.name, counters);
    out.push_back(std::move(object));
}

SourceProvenance make_provenance(const SourceArtifact& artifact,
                                 const std::filesystem::path& resolved_path,
                                 std::string parser_name)
{
    SourceProvenance provenance;
    provenance.artifact_id = artifact.id;
    provenance.artifact_path = resolved_path;
    provenance.role = artifact.role;
    provenance.category = artifact.category;
    provenance.origin = artifact.origin;
    provenance.parser_name = std::move(parser_name);
    return provenance;
}

void add_layout_ir_objects(std::vector<ImportedDesignObject>& out,
                           std::unordered_map<std::string, std::size_t>& counters,
                           const LayoutIR& ir,
                           const SourceArtifact& artifact,
                           const std::filesystem::path& resolved_path,
                           ImportedDesignObjectKind device_kind)
{
    const auto base = make_provenance(artifact, resolved_path,
                                      artifact.role == ArtifactRole::Def ? "DefParser" : "NetlistParser");

    for (const auto& layer : ir.layers) {
        auto provenance = base;
        add_object(out,
                   counters,
                   ImportedDesignObjectKind::Layer,
                   layer.name,
                   layer.name,
                   std::move(provenance),
                   {{"purpose", layer.purpose},
                    {"order", std::to_string(layer.order)},
                    {"color", layer.color},
                    {"layer_scope", artifact.category == ArtifactCategory::Technology ? "technology" : "design"}});
    }

    for (const auto& net : ir.nets) {
        auto provenance = base;
        auto metadata = net.properties;
        metadata.emplace("pin_count", std::to_string(net.pin_names.size()));
        add_object(out,
                   counters,
                   ImportedDesignObjectKind::Net,
                   net.name,
                   net.name,
                   std::move(provenance),
                   std::move(metadata));
    }

    for (const auto& port : ir.ports) {
        auto provenance = base;
        std::map<std::string, std::string> metadata{{"direction", port.direction}, {"net_name", port.net_name}};
        if (port.layer.has_value()) {
            metadata.emplace("layer", *port.layer);
        }
        if (port.location.has_value()) {
            metadata.emplace("x", std::to_string(port.location->x));
            metadata.emplace("y", std::to_string(port.location->y));
        }
        add_object(out,
                   counters,
                   ImportedDesignObjectKind::Port,
                   port.name,
                   port.name,
                   std::move(provenance),
                   std::move(metadata));
    }

    for (const auto& device : ir.devices) {
        auto provenance = base;
        auto metadata = device.properties;
        metadata.emplace("type", device.type);
        metadata.emplace("pin_count", std::to_string(device.pins.size()));
        add_object(out,
                   counters,
                   device_kind,
                   device.name,
                   device.name,
                   std::move(provenance),
                   std::move(metadata));
    }
}

void add_lef_objects(std::vector<ImportedDesignObject>& out,
                     std::unordered_map<std::string, std::size_t>& counters,
                     const LefLibraryData& library,
                     const SourceArtifact& artifact,
                     const std::filesystem::path& resolved_path)
{
    for (const auto& layer : library.layers) {
        auto provenance = make_provenance(artifact, resolved_path, "LefParser");
        provenance.source_line = layer.source_line > 0 ? std::optional<std::size_t>(layer.source_line) : std::nullopt;
        std::map<std::string, std::string> metadata{{"type", layer.type}, {"layer_scope", "technology"}};
        if (layer.width_value.has_value()) {
            metadata.emplace("width", std::to_string(*layer.width_value));
        }
        if (layer.pitch.has_value()) {
            metadata.emplace("pitch_x", std::to_string(layer.pitch->x));
            metadata.emplace("pitch_y", std::to_string(layer.pitch->y));
        }
        add_object(out,
                   counters,
                   ImportedDesignObjectKind::Layer,
                   layer.name,
                   layer.name,
                   std::move(provenance),
                   std::move(metadata));
    }

    for (const auto& macro : library.macros) {
        auto provenance = make_provenance(artifact, resolved_path, "LefParser");
        provenance.source_line = macro.source_line > 0 ? std::optional<std::size_t>(macro.source_line) : std::nullopt;
        add_object(out,
                   counters,
                   ImportedDesignObjectKind::TechnologyMacro,
                   macro.name,
                   macro.name,
                   std::move(provenance),
                   {{"macro_class", macro.macro_class},
                    {"width", std::to_string(macro.width)},
                    {"height", std::to_string(macro.height)},
                    {"pin_count", std::to_string(macro.pins.size())}});
    }
}

void ensure_graph_net_exists(ConnectivityGraph& graph, const std::string& net_name)
{
    if (graph.find_net(net_name).has_value()) {
        return;
    }
    const auto net_id = graph.add_net(NetNode{net_name, {}});
    const auto pin_id = graph.add_pin(PinNode{"port_" + net_name, "INPUT", std::nullopt, std::nullopt, std::nullopt});
    graph.add_edge(pin_id, net_id, {EdgeType::NetToPin, ""});
}

void ensure_graph_device_exists(ConnectivityGraph& graph, const std::string& instance_name)
{
    if (!graph.find_device(instance_name).has_value()) {
        graph.add_device({instance_name, "BLOCK", {}});
    }
}

} // namespace

struct ImportedDesignSession::Impl {
    ProjectPackage package;
    std::filesystem::path base_path;
    std::vector<LefLibraryData> technology_libraries;
    LayoutIR physical_ir;
    LayoutIR logical_ir;
    LayoutIR combined_ir;
    std::optional<PowerIntentData> power_intent;
    std::optional<CurrentActivityData> current_activity;
    ConnectivityGraph graph;
    std::vector<std::string> unresolved_graph_references;
    std::vector<ImportedDesignObject> objects;
    std::vector<SessionDiagnostic> diagnostics;
    std::vector<std::string> rule_artifact_ids;
    std::vector<std::filesystem::path> rule_artifact_paths;
};

ImportedDesignSession::ImportedDesignSession() : m_impl(std::make_unique<Impl>()) {}
ImportedDesignSession::~ImportedDesignSession() = default;
ImportedDesignSession::ImportedDesignSession(ImportedDesignSession&&) noexcept = default;
ImportedDesignSession& ImportedDesignSession::operator=(ImportedDesignSession&&) noexcept = default;

ProjectPackage& ImportedDesignSession::package() noexcept { return m_impl->package; }
const ProjectPackage& ImportedDesignSession::package() const noexcept { return m_impl->package; }
std::filesystem::path& ImportedDesignSession::base_path() noexcept { return m_impl->base_path; }
const std::filesystem::path& ImportedDesignSession::base_path() const noexcept { return m_impl->base_path; }
std::vector<LefLibraryData>& ImportedDesignSession::technology_libraries() noexcept { return m_impl->technology_libraries; }
const std::vector<LefLibraryData>& ImportedDesignSession::technology_libraries() const noexcept { return m_impl->technology_libraries; }
LayoutIR& ImportedDesignSession::physical_ir() noexcept { return m_impl->physical_ir; }
const LayoutIR& ImportedDesignSession::physical_ir() const noexcept { return m_impl->physical_ir; }
LayoutIR& ImportedDesignSession::logical_ir() noexcept { return m_impl->logical_ir; }
const LayoutIR& ImportedDesignSession::logical_ir() const noexcept { return m_impl->logical_ir; }
LayoutIR& ImportedDesignSession::combined_ir() noexcept { return m_impl->combined_ir; }
const LayoutIR& ImportedDesignSession::combined_ir() const noexcept { return m_impl->combined_ir; }
std::optional<PowerIntentData>& ImportedDesignSession::power_intent() noexcept { return m_impl->power_intent; }
const std::optional<PowerIntentData>& ImportedDesignSession::power_intent() const noexcept { return m_impl->power_intent; }
std::optional<CurrentActivityData>& ImportedDesignSession::current_activity() noexcept { return m_impl->current_activity; }
const std::optional<CurrentActivityData>& ImportedDesignSession::current_activity() const noexcept { return m_impl->current_activity; }
ConnectivityGraph& ImportedDesignSession::graph() noexcept { return m_impl->graph; }
const ConnectivityGraph& ImportedDesignSession::graph() const noexcept { return m_impl->graph; }
std::vector<std::string>& ImportedDesignSession::unresolved_graph_references() noexcept { return m_impl->unresolved_graph_references; }
const std::vector<std::string>& ImportedDesignSession::unresolved_graph_references() const noexcept { return m_impl->unresolved_graph_references; }
std::vector<ImportedDesignObject>& ImportedDesignSession::objects() noexcept { return m_impl->objects; }
const std::vector<ImportedDesignObject>& ImportedDesignSession::objects() const noexcept { return m_impl->objects; }
std::vector<SessionDiagnostic>& ImportedDesignSession::diagnostics() noexcept { return m_impl->diagnostics; }
const std::vector<SessionDiagnostic>& ImportedDesignSession::diagnostics() const noexcept { return m_impl->diagnostics; }
std::vector<std::string>& ImportedDesignSession::rule_artifact_ids() noexcept { return m_impl->rule_artifact_ids; }
const std::vector<std::string>& ImportedDesignSession::rule_artifact_ids() const noexcept { return m_impl->rule_artifact_ids; }
std::vector<std::filesystem::path>& ImportedDesignSession::rule_artifact_paths() noexcept { return m_impl->rule_artifact_paths; }
const std::vector<std::filesystem::path>& ImportedDesignSession::rule_artifact_paths() const noexcept { return m_impl->rule_artifact_paths; }

SessionBuildStatus ImportedDesignSession::status() const noexcept
{
    bool saw_warning = false;
    for (const auto& diagnostic : m_impl->diagnostics) {
        if (diagnostic.severity == SessionDiagnosticSeverity::Error) {
            return SessionBuildStatus::Error;
        }
        if (diagnostic.severity == SessionDiagnosticSeverity::Warning) {
            saw_warning = true;
        }
    }
    return saw_warning ? SessionBuildStatus::Warning : SessionBuildStatus::Ready;
}

bool ImportedDesignSession::has_errors() const noexcept
{
    return status() == SessionBuildStatus::Error;
}

std::size_t ImportedDesignSession::object_count(ImportedDesignObjectKind kind) const noexcept
{
    std::size_t count = 0;
    for (const auto& object : m_impl->objects) {
        if (object.kind == kind) {
            ++count;
        }
    }
    return count;
}

const ImportedDesignObject* ImportedDesignSession::find_object_by_stable_id(const std::string& stable_id) const noexcept
{
    for (const auto& object : m_impl->objects) {
        if (object.stable_id == stable_id) {
            return &object;
        }
    }
    return nullptr;
}

ImportedDesignSession ImportedDesignSessionBuilder::build(const ProjectPackage& package,
                                                          const std::filesystem::path& base_path) const
{
    ImportedDesignSession session;
    session.package() = package;
    session.base_path() = base_path;

    for (const auto& diagnostic : package.diagnostics()) {
        session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                        SessionBuildStage::ResolveArtifacts,
                                                        diagnostic.code,
                                                        diagnostic.message,
                                                        diagnostic.artifact_id));
    }

    std::unordered_map<std::string, std::size_t> object_id_counters;

    for (const auto& artifact_id : package.normalized().technology_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Technology artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }

        const auto resolved = resolve_artifact_path(base_path, *artifact);
        try {
            LefParser parser;
            const auto library = parser.parse_file(resolved);
            session.technology_libraries().push_back(library);
            add_lef_objects(session.objects(), object_id_counters, session.technology_libraries().back(), *artifact, resolved);
            for (const auto& diagnostic : session.technology_libraries().back().diagnostics) {
                session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                                SessionBuildStage::TechnologyParse,
                                                                diagnostic.code,
                                                                "LEF '" + artifact->path.generic_string() + "': " + diagnostic.message,
                                                                artifact->id,
                                                                diagnostic.line));
            }
        } catch (const std::exception& ex) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::TechnologyParse,
                                                            "SESSION_LEF_PARSE_FAILED",
                                                            "Failed to parse LEF artifact '" + resolved.generic_string() + "': " + ex.what(),
                                                            artifact->id));
        }
    }

    for (const auto& artifact_id : package.normalized().layout_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Layout artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }

        const auto resolved = resolve_artifact_path(base_path, *artifact);
        try {
            DefParser parser;
            const auto ir = parser.parse_to_layout_ir(resolved);
            add_layout_ir_objects(session.objects(), object_id_counters, ir, *artifact, resolved, ImportedDesignObjectKind::Instance);
            session.physical_ir() = append_ir(std::move(session.physical_ir()), ir);
        } catch (const std::exception& ex) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::LayoutParse,
                                                            "SESSION_DEF_PARSE_FAILED",
                                                            "Failed to parse DEF artifact '" + resolved.generic_string() + "': " + ex.what(),
                                                            artifact->id));
        }
    }

    for (const auto& artifact_id : package.normalized().netlist_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Netlist artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }

        const auto resolved = resolve_artifact_path(base_path, *artifact);
        try {
            const auto ir = parse_netlist_artifact(resolved, artifact->role);
            add_layout_ir_objects(session.objects(), object_id_counters, ir, *artifact, resolved, ImportedDesignObjectKind::Device);
            session.logical_ir() = append_ir(std::move(session.logical_ir()), ir);
        } catch (const std::exception& ex) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::NetlistParse,
                                                            "SESSION_NETLIST_PARSE_FAILED",
                                                            "Failed to parse netlist artifact '" + resolved.generic_string() + "': " + ex.what(),
                                                            artifact->id));
        }
    }

    for (const auto& artifact_id : package.normalized().rule_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Rule artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }
        session.rule_artifact_ids().push_back(artifact->id);
        session.rule_artifact_paths().push_back(resolve_artifact_path(base_path, *artifact));
    }

    session.combined_ir() = append_ir(session.physical_ir(), session.logical_ir());
    session.graph() = ConnectivityGraph::from_layout_ir(session.combined_ir(), session.unresolved_graph_references());
    for (const auto& unresolved : session.unresolved_graph_references()) {
        session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Warning,
                                                        SessionBuildStage::GraphBuild,
                                                        "SESSION_GRAPH_UNRESOLVED_REFERENCE",
                                                        "Unresolved connectivity reference during imported session graph build: " + unresolved));
    }

    for (const auto& artifact_id : package.normalized().power_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Power artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }

        const auto resolved = resolve_artifact_path(base_path, *artifact);
        try {
            PowerIntentParser parser;
            auto data = parser.parse_csv_file(resolved);
            for (const auto& diagnostic : data.diagnostics) {
                session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                                SessionBuildStage::PowerEnrichment,
                                                                diagnostic.code,
                                                                diagnostic.message,
                                                                artifact->id,
                                                                std::nullopt,
                                                                diagnostic.source_row));
            }
            if (data.has_errors()) {
                continue;
            }
            if (!session.power_intent().has_value()) {
                session.power_intent() = PowerIntentData{};
            }
            auto& merged = *session.power_intent();
            merged.assignments.insert(merged.assignments.end(), data.assignments.begin(), data.assignments.end());
            merged.diagnostics.insert(merged.diagnostics.end(), data.diagnostics.begin(), data.diagnostics.end());
            for (const auto& assignment : data.assignments) {
                ensure_graph_device_exists(session.graph(), assignment.instance_name);
            }
            const auto applied = aegis::graph::apply_power_intent(session.graph(), data);
            for (const auto& diagnostic : applied.diagnostics) {
                session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                                SessionBuildStage::PowerEnrichment,
                                                                diagnostic.code,
                                                                diagnostic.message,
                                                                artifact->id,
                                                                std::nullopt,
                                                                diagnostic.source_row));
            }
        } catch (const std::exception& ex) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::PowerEnrichment,
                                                            "SESSION_POWER_PARSE_FAILED",
                                                            "Failed to parse power-domain artifact '" + resolved.generic_string() + "': " + ex.what(),
                                                            artifact->id));
        }
    }

    for (const auto& artifact_id : package.normalized().current_artifact_ids) {
        const auto* artifact = package.find_artifact_by_id(artifact_id);
        if (artifact == nullptr) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::ResolveArtifacts,
                                                            "SESSION_ARTIFACT_MISSING",
                                                            "Current/activity artifact id '" + artifact_id + "' was not found in the package",
                                                            artifact_id));
            continue;
        }

        const auto resolved = resolve_artifact_path(base_path, *artifact);
        try {
            CurrentActivityParser parser;
            auto data = parser.parse_csv_file(resolved);
            for (const auto& diagnostic : data.diagnostics) {
                session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                                SessionBuildStage::CurrentEnrichment,
                                                                diagnostic.code,
                                                                diagnostic.message,
                                                                artifact->id,
                                                                std::nullopt,
                                                                diagnostic.source_row));
            }
            if (data.has_errors()) {
                continue;
            }
            if (!session.current_activity().has_value()) {
                session.current_activity() = CurrentActivityData{};
            }
            auto& merged = *session.current_activity();
            merged.records.insert(merged.records.end(), data.records.begin(), data.records.end());
            merged.diagnostics.insert(merged.diagnostics.end(), data.diagnostics.begin(), data.diagnostics.end());
            for (const auto& record : data.records) {
                ensure_graph_net_exists(session.graph(), record.net_name);
            }
            const auto applied = aegis::graph::apply_current_activity(session.graph(), data);
            for (const auto& diagnostic : applied.diagnostics) {
                session.diagnostics().push_back(make_diagnostic(to_session_severity(diagnostic.severity),
                                                                SessionBuildStage::CurrentEnrichment,
                                                                diagnostic.code,
                                                                diagnostic.message,
                                                                artifact->id,
                                                                std::nullopt,
                                                                diagnostic.source_row));
            }
        } catch (const std::exception& ex) {
            session.diagnostics().push_back(make_diagnostic(SessionDiagnosticSeverity::Error,
                                                            SessionBuildStage::CurrentEnrichment,
                                                            "SESSION_CURRENT_PARSE_FAILED",
                                                            "Failed to parse current/activity artifact '" + resolved.generic_string() + "': " + ex.what(),
                                                            artifact->id));
        }
    }

    return session;
}

std::string to_string(ImportedDesignObjectKind kind)
{
    switch (kind) {
    case ImportedDesignObjectKind::Instance: return "instance";
    case ImportedDesignObjectKind::Port: return "port";
    case ImportedDesignObjectKind::Net: return "net";
    case ImportedDesignObjectKind::Device: return "device";
    case ImportedDesignObjectKind::TechnologyMacro: return "technology_macro";
    case ImportedDesignObjectKind::Layer: break;
    }
    return "layer";
}

std::string to_string(SessionDiagnosticSeverity severity)
{
    switch (severity) {
    case SessionDiagnosticSeverity::Warning: return "warning";
    case SessionDiagnosticSeverity::Error: return "error";
    case SessionDiagnosticSeverity::Info: break;
    }
    return "info";
}

std::string to_string(SessionBuildStatus status)
{
    switch (status) {
    case SessionBuildStatus::Warning: return "warning";
    case SessionBuildStatus::Error: return "error";
    case SessionBuildStatus::Ready: break;
    }
    return "ready";
}

std::string to_string(SessionBuildStage stage)
{
    switch (stage) {
    case SessionBuildStage::TechnologyParse: return "technology_parse";
    case SessionBuildStage::LayoutParse: return "layout_parse";
    case SessionBuildStage::NetlistParse: return "netlist_parse";
    case SessionBuildStage::RuleLink: return "rule_link";
    case SessionBuildStage::GraphBuild: return "graph_build";
    case SessionBuildStage::PowerEnrichment: return "power_enrichment";
    case SessionBuildStage::CurrentEnrichment: return "current_enrichment";
    case SessionBuildStage::ResolveArtifacts: break;
    }
    return "resolve_artifacts";
}

} // namespace aegis::storage
