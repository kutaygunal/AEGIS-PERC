#pragma once

#include "aegis/core/config.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Migration diagnostics
// ---------------------------------------------------------------------------
struct MigrationDiagnostic {
    enum class Severity { Info, Warning, Error };

    Severity severity = Severity::Info;
    std::string message;
};

// ---------------------------------------------------------------------------
// Result of a migration run
// ---------------------------------------------------------------------------
struct ConfigMigrationResult {
    bool success = false;
    Config migrated_config;
    std::vector<MigrationDiagnostic> diagnostics;
    int from_version = 0;
    int to_version = 0;

    // Convenience accessors for test assertions
    bool has_errors() const noexcept;
    bool has_warnings() const noexcept;
};

// ---------------------------------------------------------------------------
// Migration function signature
//
// Receives a mutable Config and a diagnostic sink. Must be idempotent where
// possible. Any throw aborts the migration chain and the result is marked invalid.
// ---------------------------------------------------------------------------
using MigrationStep = std::function<void(Config& cfg, std::vector<MigrationDiagnostic>& diag)>;

// ---------------------------------------------------------------------------
// ConfigMigrator
//
// Isolated from runtime Config access. Migrators are registered as a chain of
// steps (version N -> N+1). The migrator walks the chain sequentially; it does
// not skip versions.
// ---------------------------------------------------------------------------
class ConfigMigrator {
public:
    ConfigMigrator();
    ~ConfigMigrator() = default;

    ConfigMigrator(const ConfigMigrator&) = delete;
    ConfigMigrator& operator=(const ConfigMigrator&) = delete;

    // Register a migration step from version N to N+1.
    // The same version may not be registered twice.
    void register_step(int from_version, std::string description, MigrationStep step);

    // Migrate a config from its current version up to target_version.
    // If source has no "version" key it is treated as version 0 (legacy).
    ConfigMigrationResult migrate(const Config& source, int target_version) const;

    // Current schema version supported by this migrator.
    int max_target_version() const noexcept;

    // Detect version from a Config object. Returns 0 if "version" is absent.
    static int detect_version(const Config& cfg);

    // Built-in default migrations (registered in constructor).
    static void migrate_v0_to_v1(Config& cfg, std::vector<MigrationDiagnostic>& diag);
    static void migrate_v1_to_v2(Config& cfg, std::vector<MigrationDiagnostic>& diag);

private:
    struct Step {
        int from_version = 0;
        std::string description;
        MigrationStep func;
    };

    std::vector<Step> m_steps;

    const Step* find_step_for(int from_version) const;
};

// ---------------------------------------------------------------------------
// Exception thrown on programming errors (duplicate registration, bad target).
// Runtime migration failures are expressed via ConfigMigrationResult.
// ---------------------------------------------------------------------------
class MigrationError : public std::runtime_error {
public:
    explicit MigrationError(const std::string& message);
};

} // namespace aegis::core
