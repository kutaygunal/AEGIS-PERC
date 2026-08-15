#include "aegis/storage/session_cache.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <ctime>

namespace aegis::storage {

inline void to_json(nlohmann::json& j, ImportedDesignObjectKind kind)
{
    j = to_string(kind);
}

inline void from_json(const nlohmann::json& j, ImportedDesignObjectKind& kind)
{
    const std::string s = j.get<std::string>();
    if (s == "instance") kind = ImportedDesignObjectKind::Instance;
    else if (s == "port") kind = ImportedDesignObjectKind::Port;
    else if (s == "net") kind = ImportedDesignObjectKind::Net;
    else if (s == "device") kind = ImportedDesignObjectKind::Device;
    else if (s == "technology_macro") kind = ImportedDesignObjectKind::TechnologyMacro;
    else kind = ImportedDesignObjectKind::Layer;
}

inline void to_json(nlohmann::json& j, SessionDiagnosticSeverity sev)
{
    j = to_string(sev);
}

inline void from_json(const nlohmann::json& j, SessionDiagnosticSeverity& sev)
{
    const std::string s = j.get<std::string>();
    if (s == "warning") sev = SessionDiagnosticSeverity::Warning;
    else if (s == "error") sev = SessionDiagnosticSeverity::Error;
    else sev = SessionDiagnosticSeverity::Info;
}

inline void to_json(nlohmann::json& j, SessionBuildStage stage)
{
    j = to_string(stage);
}

inline void from_json(const nlohmann::json& j, SessionBuildStage& stage)
{
    const std::string s = j.get<std::string>();
    if (s == "technology_parse") stage = SessionBuildStage::TechnologyParse;
    else if (s == "layout_parse") stage = SessionBuildStage::LayoutParse;
    else if (s == "netlist_parse") stage = SessionBuildStage::NetlistParse;
    else if (s == "rule_link") stage = SessionBuildStage::RuleLink;
    else if (s == "graph_build") stage = SessionBuildStage::GraphBuild;
    else if (s == "power_enrichment") stage = SessionBuildStage::PowerEnrichment;
    else if (s == "current_enrichment") stage = SessionBuildStage::CurrentEnrichment;
    else stage = SessionBuildStage::ResolveArtifacts;
}

inline void to_json(nlohmann::json& j, const SourceProvenance& p)
{
    j = nlohmann::json{
        {"artifact_id", p.artifact_id},
        {"artifact_path", p.artifact_path.generic_string()},
        {"role", to_string(p.role)},
        {"category", to_string(p.category)},
        {"origin", p.origin},
        {"parser_name", p.parser_name}
    };
    if (p.source_line.has_value()) j["source_line"] = *p.source_line;
    if (p.source_row.has_value()) j["source_row"] = *p.source_row;
}

inline void from_json(const nlohmann::json& j, SourceProvenance& p)
{
    j.at("artifact_id").get_to(p.artifact_id);
    p.artifact_path = j.at("artifact_path").get<std::string>();
    p.role = artifact_role_from_string(j.at("role").get<std::string>());
    p.category = artifact_category_from_string(j.at("category").get<std::string>());
    j.at("origin").get_to(p.origin);
    j.at("parser_name").get_to(p.parser_name);
    if (j.contains("source_line")) p.source_line = j["source_line"].get<std::size_t>();
    if (j.contains("source_row")) p.source_row = j["source_row"].get<std::size_t>();
}

inline void to_json(nlohmann::json& j, const SessionDiagnostic& d)
{
    j = nlohmann::json{
        {"severity", d.severity},
        {"stage", d.stage},
        {"code", d.code},
        {"message", d.message}
    };
    if (d.artifact_id.has_value()) j["artifact_id"] = *d.artifact_id;
    if (d.source_line.has_value()) j["source_line"] = *d.source_line;
    if (d.source_row.has_value()) j["source_row"] = *d.source_row;
}

inline void from_json(const nlohmann::json& j, SessionDiagnostic& d)
{
    j.at("severity").get_to(d.severity);
    j.at("stage").get_to(d.stage);
    j.at("code").get_to(d.code);
    j.at("message").get_to(d.message);
    if (j.contains("artifact_id")) d.artifact_id = j["artifact_id"].get<std::string>();
    if (j.contains("source_line")) d.source_line = j["source_line"].get<std::size_t>();
    if (j.contains("source_row")) d.source_row = j["source_row"].get<std::size_t>();
}

inline void to_json(nlohmann::json& j, const ImportedDesignObject& obj)
{
    j = nlohmann::json{
        {"stable_id", obj.stable_id},
        {"kind", obj.kind},
        {"name", obj.name},
        {"display_name", obj.display_name},
        {"provenance", obj.provenance},
        {"metadata", obj.metadata}
    };
}

inline void from_json(const nlohmann::json& j, ImportedDesignObject& obj)
{
    j.at("stable_id").get_to(obj.stable_id);
    j.at("kind").get_to(obj.kind);
    j.at("name").get_to(obj.name);
    j.at("display_name").get_to(obj.display_name);
    j.at("provenance").get_to(obj.provenance);
    j.at("metadata").get_to(obj.metadata);
}

inline void to_json(nlohmann::json& j, const SessionArtifactSignature& sig)
{
    j = nlohmann::json{{"size", sig.size}, {"mtime", sig.mtime}};
}

inline void from_json(const nlohmann::json& j, SessionArtifactSignature& sig)
{
    j.at("size").get_to(sig.size);
    j.at("mtime").get_to(sig.mtime);
}

namespace {

std::string current_iso_timestamp()
{
    const auto now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &now);
#else
    gmtime_r(&now, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

} // namespace

std::filesystem::path SessionCache::cache_path_for(const std::filesystem::path& base_path)
{
    return base_path / kCacheFileName;
}

bool SessionCache::compute_artifact_signatures(
    const ProjectPackage& package,
    const std::filesystem::path& base_path,
    std::map<std::string, SessionArtifactSignature>& out) const
{
    for (const auto& artifact : package.artifacts()) {
        const auto resolved = artifact.path.is_absolute() ? artifact.path : (base_path / artifact.path);
        if (!std::filesystem::exists(resolved)) {
            return false;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(resolved, ec);
        const auto mtime = std::filesystem::last_write_time(resolved, ec);
        if (ec) {
            return false;
        }
        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            mtime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t mtime_t = std::chrono::system_clock::to_time_t(sctp);
        out[artifact.id] = SessionArtifactSignature{size, mtime_t};
    }
    return true;
}

bool SessionCache::signatures_match(
    const std::map<std::string, SessionArtifactSignature>& cached,
    const ProjectPackage& package,
    const std::filesystem::path& base_path) const
{
    std::map<std::string, SessionArtifactSignature> current;
    if (!compute_artifact_signatures(package, base_path, current)) {
        return false;
    }
    return current == cached;
}

bool SessionCache::serialize_session(const ImportedDesignSession& session,
                                      const std::filesystem::path& base_path,
                                      std::string& out_json) const
{
    try {
        nlohmann::json j;
        j["schema_version"] = kCurrentSchemaVersion;
        j["created_at"] = current_iso_timestamp();
        j["base_path"] = base_path.generic_string();
        j["package"] = nlohmann::json::parse(session.package().to_manifest_json());

        std::map<std::string, SessionArtifactSignature> signatures;
        if (!compute_artifact_signatures(session.package(), base_path, signatures)) {
            return false;
        }
        j["artifact_signatures"] = signatures;

        j["physical_ir"] = session.physical_ir();
        j["combined_ir"] = session.combined_ir();
        j["objects"] = session.objects();
        j["diagnostics"] = session.diagnostics();
        j["unresolved_references"] = session.unresolved_graph_references();
        j["status"] = to_string(session.status());
        j["rule_artifact_ids"] = session.rule_artifact_ids();
        j["has_power_intent"] = session.power_intent().has_value();
        j["has_current_activity"] = session.current_activity().has_value();

        out_json = j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<ImportedDesignSession> SessionCache::deserialize_session(
    const std::string& json_text,
    const std::filesystem::path& base_path) const
{
    try {
        const nlohmann::json j = nlohmann::json::parse(json_text);
        const int schema = j.value("schema_version", 0);
        if (schema != kCurrentSchemaVersion) {
            return std::nullopt;
        }

        ImportedDesignSession session;

        const std::string manifest = j.at("package").dump();
        session.package() = ProjectPackage::from_manifest_json(manifest);
        session.base_path() = base_path;

        j.at("physical_ir").get_to(session.physical_ir());
        j.at("combined_ir").get_to(session.combined_ir());
        j.at("objects").get_to(session.objects());
        j.at("diagnostics").get_to(session.diagnostics());
        j.at("unresolved_references").get_to(session.unresolved_graph_references());
        j.at("rule_artifact_ids").get_to(session.rule_artifact_ids());

        try {
            auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(session.combined_ir(),
                                                                          session.unresolved_graph_references());
            session.graph() = std::move(graph);
        } catch (...) {
            // Graph rebuild failed; session remains usable but graph is empty
        }

        if (j.value("has_power_intent", false)) {
            session.power_intent() = aegis::parsing::PowerIntentData{};
        }
        if (j.value("has_current_activity", false)) {
            session.current_activity() = aegis::parsing::CurrentActivityData{};
        }

        return session;
    } catch (...) {
        return std::nullopt;
    }
}

bool SessionCache::save(const ImportedDesignSession& session,
                        const std::filesystem::path& base_path) const
{
    try {
        std::string json_text;
        if (!serialize_session(session, base_path, json_text)) {
            return false;
        }
        const auto path = cache_path_for(base_path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        if (!out.good()) {
            return false;
        }
        out << json_text;
        return out.good();
    } catch (...) {
        return false;
    }
}

bool SessionCache::is_cache_valid(const std::filesystem::path& base_path) const
{
    try {
        const auto path = cache_path_for(base_path);
        if (!std::filesystem::exists(path)) {
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in.good()) {
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        const nlohmann::json j = nlohmann::json::parse(text);
        const int schema = j.value("schema_version", 0);
        if (schema != kCurrentSchemaVersion) {
            return false;
        }
        const std::string manifest = j.at("package").dump();
        const auto package = ProjectPackage::from_manifest_json(manifest);
        const auto cached_sigs = j.at("artifact_signatures").get<std::map<std::string, SessionArtifactSignature>>();
        return signatures_match(cached_sigs, package, base_path);
    } catch (...) {
        return false;
    }
}

std::optional<ImportedDesignSession> SessionCache::load(
    const std::filesystem::path& base_path) const
{
    try {
        if (!is_cache_valid(base_path)) {
            return std::nullopt;
        }
        const auto path = cache_path_for(base_path);
        std::ifstream in(path, std::ios::binary);
        if (!in.good()) {
            return std::nullopt;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        return deserialize_session(text, base_path);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace aegis::storage
