#pragma once

#include "aegis/storage/imported_design_session.hpp"
#include "aegis/parsing/layout_ir.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace aegis::storage {

struct SessionCacheMetadata {
    int schema_version = 1;
    std::string created_at;
    std::filesystem::path base_path;
};

struct SessionArtifactSignature {
    std::uintmax_t size = 0;
    std::time_t mtime = 0;

    bool operator==(const SessionArtifactSignature& other) const noexcept {
        return size == other.size && mtime == other.mtime;
    }
    bool operator!=(const SessionArtifactSignature& other) const noexcept {
        return !(*this == other);
    }
};

class SessionCache {
public:
    static constexpr int kCurrentSchemaVersion = 1;
    static constexpr const char* kCacheFileName = ".aegis/session_cache.json";

    [[nodiscard]] bool save(const ImportedDesignSession& session,
                            const std::filesystem::path& base_path) const;

    [[nodiscard]] std::optional<ImportedDesignSession> load(
        const std::filesystem::path& base_path) const;

    [[nodiscard]] bool is_cache_valid(const std::filesystem::path& base_path) const;

    [[nodiscard]] static std::filesystem::path cache_path_for(
        const std::filesystem::path& base_path);

private:
    [[nodiscard]] bool serialize_session(const ImportedDesignSession& session,
                                          const std::filesystem::path& base_path,
                                          std::string& out_json) const;

    [[nodiscard]] std::optional<ImportedDesignSession> deserialize_session(
        const std::string& json_text,
        const std::filesystem::path& base_path) const;

    [[nodiscard]] bool compute_artifact_signatures(
        const ProjectPackage& package,
        const std::filesystem::path& base_path,
        std::map<std::string, SessionArtifactSignature>& out) const;

    [[nodiscard]] bool signatures_match(
        const std::map<std::string, SessionArtifactSignature>& cached,
        const ProjectPackage& package,
        const std::filesystem::path& base_path) const;
};

} // namespace aegis::storage
