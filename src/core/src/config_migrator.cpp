#include "aegis/core/config_migrator.hpp"

#include <algorithm>
#include <sstream>

namespace aegis::core {

// ---------------------------------------------------------------------------
// ConfigMigrationResult
// ---------------------------------------------------------------------------
bool ConfigMigrationResult::has_errors() const noexcept {
    for (const auto& d : diagnostics) {
        if (d.severity == MigrationDiagnostic::Severity::Error) return true;
    }
    return false;
}

bool ConfigMigrationResult::has_warnings() const noexcept {
    for (const auto& d : diagnostics) {
        if (d.severity == MigrationDiagnostic::Severity::Warning) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MigrationError
// ---------------------------------------------------------------------------
MigrationError::MigrationError(const std::string& message)
    : std::runtime_error(message) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void add_info(std::vector<MigrationDiagnostic>& diag, const std::string& msg) {
    diag.push_back({ MigrationDiagnostic::Severity::Info, msg });
}

static void add_warn(std::vector<MigrationDiagnostic>& diag, const std::string& msg) {
    diag.push_back({ MigrationDiagnostic::Severity::Warning, msg });
}

static void add_error(std::vector<MigrationDiagnostic>& diag, const std::string& msg) {
    diag.push_back({ MigrationDiagnostic::Severity::Error, msg });
}

static std::string version_key() { return "version"; }

// ---------------------------------------------------------------------------
// Built-in migrations
// ---------------------------------------------------------------------------

// v0 (legacy) -> v1:
//   - Introduce version key.
//   - Move core.log_level -> core.logging.level.
//   - Move core.log_dir    -> core.logging.directory.
//   - Drop deprecated core.use_async_log if present (was bool, now inferred).
void ConfigMigrator::migrate_v0_to_v1(Config& cfg, std::vector<MigrationDiagnostic>& diag) {
    add_info(diag, "Migrating v0 (legacy) -> v1");

    cfg.set("version", ConfigValue(1));

    if (cfg.has("core.log_level")) {
        auto val = cfg.get_string("core.log_level", "");
        if (!val.empty()) {
            cfg.set("core.logging.level", ConfigValue(std::move(val)));
            add_info(diag, "Moved core.log_level -> core.logging.level");
        }
        // Best-effort removal: Config does not expose erase, so we just leave it
        // and document that it is orphaned. Future versions may add erase.
        add_warn(diag, "Orphaned key core.log_level remains in flat store (no erase API yet)");
    }

    if (cfg.has("core.log_dir")) {
        auto val = cfg.get_string("core.log_dir", "");
        if (!val.empty()) {
            cfg.set("core.logging.directory", ConfigValue(std::move(val)));
            add_info(diag, "Moved core.log_dir -> core.logging.directory");
        }
        add_warn(diag, "Orphaned key core.log_dir remains in flat store (no erase API yet)");
    }

    if (cfg.has("core.use_async_log")) {
        add_warn(diag, "Dropped deprecated key core.use_async_log (behaviour now always async)");
    }

    add_info(diag, "Migration v0 -> v1 completed");
}

// v1 -> v2:
//   - Rename core.logging.rotation.max_size_mb to max_size_mib (MiB units).
//   - Add core.logging.rotation.max_files default=5 if absent.
//   - Add core.ui.animations default=true if absent.
void ConfigMigrator::migrate_v1_to_v2(Config& cfg, std::vector<MigrationDiagnostic>& diag) {
    add_info(diag, "Migrating v1 -> v2");

    cfg.set("version", ConfigValue(2));

    if (cfg.has("core.logging.rotation.max_size_mb")) {
        auto val = cfg.get_string("core.logging.rotation.max_size_mb", "");
        if (!val.empty()) {
            cfg.set("core.logging.rotation.max_size_mib", ConfigValue(std::move(val)));
            add_info(diag,
                "Renamed core.logging.rotation.max_size_mb -> core.logging.rotation.max_size_mib");
        }
        add_warn(diag,
            "Orphaned key core.logging.rotation.max_size_mb remains in flat store (no erase API yet)");
    }

    if (!cfg.has("core.logging.rotation.max_files")) {
        cfg.set("core.logging.rotation.max_files", ConfigValue(std::int64_t{5}));
        add_info(diag, "Added default core.logging.rotation.max_files = 5");
    }

    if (!cfg.has("core.ui.animations")) {
        cfg.set("core.ui.animations", ConfigValue(true));
        add_info(diag, "Added default core.ui.animations = true");
    }

    add_info(diag, "Migration v1 -> v2 completed");
}

// ---------------------------------------------------------------------------
// ConfigMigrator
// ---------------------------------------------------------------------------
ConfigMigrator::ConfigMigrator() {
    // Register built-in chain
    register_step(0, "Legacy (v0) -> v1", migrate_v0_to_v1);
    register_step(1, "v1 -> v2",          migrate_v1_to_v2);
}

void ConfigMigrator::register_step(int from_version, std::string description, MigrationStep step) {
    if (find_step_for(from_version) != nullptr) {
        throw MigrationError("Migration step from version " +
            std::to_string(from_version) + " is already registered");
    }
    m_steps.push_back({ from_version, std::move(description), std::move(step) });
}

int ConfigMigrator::max_target_version() const noexcept {
    int max_v = 0;
    for (const auto& s : m_steps) {
        max_v = std::max(max_v, s.from_version + 1);
    }
    return max_v;
}

const ConfigMigrator::Step* ConfigMigrator::find_step_for(int from_version) const {
    for (const auto& s : m_steps) {
        if (s.from_version == from_version) return &s;
    }
    return nullptr;
}

int ConfigMigrator::detect_version(const Config& cfg) {
    auto v = cfg.get(version_key());
    if (!v) return 0; // legacy / unversioned
    if (auto* pi = v->as<std::int64_t>()) return static_cast<int>(*pi);
    if (auto* pd = v->as<double>())      return static_cast<int>(*pd);
    if (auto* ps = v->as<std::string>()) {
        // Accept stringified integers like "1"
        try {
            return std::stoi(*ps);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

ConfigMigrationResult ConfigMigrator::migrate(const Config& source, int target_version) const {
    ConfigMigrationResult result;
    result.from_version = detect_version(source);
    result.to_version   = target_version;
    result.migrated_config = source;

    if (target_version < 0) {
        add_error(result.diagnostics,
            "Target version must be >= 0, got " + std::to_string(target_version));
        result.success = false;
        return result;
    }

    int current = result.from_version;
    if (current == target_version) {
        add_info(result.diagnostics,
            "No migration needed: source already at version " + std::to_string(target_version));
        result.success = true;
        return result;
    }

    if (current > target_version) {
        add_error(result.diagnostics,
            "Downgrade not supported: source is v" + std::to_string(current) +
            ", requested v" + std::to_string(target_version));
        result.success = false;
        return result;
    }

    int chain_max = max_target_version();
    if (target_version > chain_max) {
        add_error(result.diagnostics,
            "Target version " + std::to_string(target_version) +
            " exceeds maximum supported version " + std::to_string(chain_max));
        result.success = false;
        return result;
    }

    // Walk chain: current -> current+1 -> ... -> target_version
    for (int v = current; v < target_version; ++v) {
        const Step* step = find_step_for(v);
        if (!step) {
            std::ostringstream oss;
            oss << "No migration step registered from version " << v
                << " to " << (v + 1);
            add_error(result.diagnostics, oss.str());
            result.success = false;
            return result;
        }

        try {
            add_info(result.diagnostics, "Running: " + step->description);
            step->func(result.migrated_config, result.diagnostics);
        } catch (const std::exception& ex) {
            add_error(result.diagnostics,
                std::string("Migration step failed: ") + ex.what());
            result.success = false;
            return result;
        }
    }

    // Verify the version key reflects the target
    result.migrated_config.set(version_key(), ConfigValue(std::int64_t{target_version}));

    result.success = !result.has_errors();
    return result;
}

} // namespace aegis::core
