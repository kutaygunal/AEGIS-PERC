#include <catch2/catch_test_macros.hpp>
#include "aegis/core/config.hpp"
#include "aegis/core/config_migrator.hpp"

#include <string>

using namespace aegis::core;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static Config make_legacy_config() {
    Config cfg;
    cfg.load_from_string(R"({
        "core.log_level": "info",
        "core.log_dir": "/var/log/aegis",
        "core.use_async_log": true
    })");
    return cfg;
}

static Config make_v1_config() {
    Config cfg;
    cfg.load_from_string(R"({
        "version": 1,
        "core.logging.level": "debug",
        "core.logging.directory": "/tmp/logs"
    })");
    return cfg;
}

static Config make_v2_config() {
    Config cfg;
    cfg.load_from_string(R"({
        "version": 2,
        "core.logging.level": "warn",
        "core.logging.directory": "/opt/logs",
        "core.logging.rotation.max_size_mib": "50",
        "core.logging.rotation.max_files": 10,
        "core.ui.animations": false
    })");
    return cfg;
}

// ---------------------------------------------------------------------------
// Detect version
// ---------------------------------------------------------------------------
TEST_CASE("detect_version returns 0 for legacy config", "[core][p1-009][ConfigMigration]") {
    auto cfg = make_legacy_config();
    REQUIRE(ConfigMigrator::detect_version(cfg) == 0);
}

TEST_CASE("detect_version returns int version", "[core][p1-009][ConfigMigration]") {
    Config cfg;
    cfg.load_from_string(R"({"version": 3})");
    REQUIRE(ConfigMigrator::detect_version(cfg) == 3);
}

TEST_CASE("detect_version returns 0 for string version", "[core][p1-009][ConfigMigration]") {
    Config cfg;
    cfg.load_from_string(R"({"version": "2"})");
    REQUIRE(ConfigMigrator::detect_version(cfg) == 2);
}

// ---------------------------------------------------------------------------
// No-op migration
// ---------------------------------------------------------------------------
TEST_CASE("migrate same version is no-op", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_v2_config();
    auto result = migrator.migrate(cfg, 2);

    REQUIRE(result.success);
    REQUIRE(result.from_version == 2);
    REQUIRE(result.to_version == 2);
    REQUIRE(!result.has_errors());
}

// ---------------------------------------------------------------------------
// Chain: legacy -> v1
// ---------------------------------------------------------------------------
TEST_CASE("migrate legacy to v1 transforms keys", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_legacy_config();
    auto result = migrator.migrate(cfg, 1);

    REQUIRE(result.success);
    REQUIRE(result.from_version == 0);
    REQUIRE(result.to_version == 1);
    REQUIRE(ConfigMigrator::detect_version(result.migrated_config) == 1);

    REQUIRE(result.migrated_config.has("core.logging.level"));
    REQUIRE(result.migrated_config.get_string("core.logging.level", "") == "info");

    REQUIRE(result.migrated_config.has("core.logging.directory"));
    REQUIRE(result.migrated_config.get_string("core.logging.directory", "") == "/var/log/aegis");

    REQUIRE(!result.has_errors());
}

// ---------------------------------------------------------------------------
// Chain: v1 -> v2
// ---------------------------------------------------------------------------
TEST_CASE("migrate v1 to v2 adds defaults and renames", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_v1_config();
    auto result = migrator.migrate(cfg, 2);

    REQUIRE(result.success);
    REQUIRE(result.from_version == 1);
    REQUIRE(result.to_version == 2);
    REQUIRE(ConfigMigrator::detect_version(result.migrated_config) == 2);

    // v1 -> v2: add defaults
    REQUIRE(result.migrated_config.has("core.logging.rotation.max_files"));
    REQUIRE(result.migrated_config.get_int("core.logging.rotation.max_files", 0) == 5);

    REQUIRE(result.migrated_config.has("core.ui.animations"));
    REQUIRE(result.migrated_config.get_bool("core.ui.animations", false) == true);

    // Preserve existing v1 keys
    REQUIRE(result.migrated_config.get_string("core.logging.level", "") == "debug");
    REQUIRE(result.migrated_config.get_string("core.logging.directory", "") == "/tmp/logs");
}

TEST_CASE("migrate v1 to v2 renames max_size_mb when present", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_v1_config();
    cfg.set("core.logging.rotation.max_size_mb", ConfigValue(std::string("100")));

    auto result = migrator.migrate(cfg, 2);
    REQUIRE(result.success);
    REQUIRE(result.migrated_config.has("core.logging.rotation.max_size_mib"));
    REQUIRE(result.migrated_config.get_string("core.logging.rotation.max_size_mib", "") == "100");
}

// ---------------------------------------------------------------------------
// Full chain: legacy -> v2
// ---------------------------------------------------------------------------
TEST_CASE("migrate legacy to v2 chains both steps", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_legacy_config();
    auto result = migrator.migrate(cfg, 2);

    REQUIRE(result.success);
    REQUIRE(result.from_version == 0);
    REQUIRE(result.to_version == 2);
    REQUIRE(ConfigMigrator::detect_version(result.migrated_config) == 2);

    // v0 -> v1 transforms
    REQUIRE(result.migrated_config.get_string("core.logging.level", "") == "info");
    REQUIRE(result.migrated_config.get_string("core.logging.directory", "") == "/var/log/aegis");

    // v1 -> v2 defaults
    REQUIRE(result.migrated_config.get_int("core.logging.rotation.max_files", 0) == 5);
    REQUIRE(result.migrated_config.get_bool("core.ui.animations", false) == true);

    // Diagnostics should note both steps
    REQUIRE(!result.diagnostics.empty());
}

// ---------------------------------------------------------------------------
// Graceful failure
// ---------------------------------------------------------------------------
TEST_CASE("migrate unsupported target version fails gracefully", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_legacy_config();
    auto result = migrator.migrate(cfg, 99);

    REQUIRE(!result.success);
    REQUIRE(result.has_errors());
    REQUIRE(result.to_version == 99);
}

TEST_CASE("migrate downgrade is rejected", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_v2_config();
    auto result = migrator.migrate(cfg, 1);

    REQUIRE(!result.success);
    REQUIRE(result.has_errors());
    REQUIRE(result.from_version == 2);
    REQUIRE(result.to_version == 1);
}

TEST_CASE("migrate negative target is rejected", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_legacy_config();
    auto result = migrator.migrate(cfg, -1);

    REQUIRE(!result.success);
    REQUIRE(result.has_errors());
}

// ---------------------------------------------------------------------------
// Duplicate registration throws
// ---------------------------------------------------------------------------
TEST_CASE("duplicate migration registration throws", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    REQUIRE_THROWS_AS(
        migrator.register_step(0, "duplicate", [](Config&, std::vector<MigrationDiagnostic>&) {}),
        MigrationError
    );
}

// ---------------------------------------------------------------------------
// Custom migrator
// ---------------------------------------------------------------------------
TEST_CASE("custom migration step is applied", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    // Register v2 -> v3 custom step
    migrator.register_step(2, "v2 -> v3 (custom)", [](Config& cfg, std::vector<MigrationDiagnostic>& diag) {
        cfg.set("custom.added", ConfigValue(std::string("hello")));
        diag.push_back({ MigrationDiagnostic::Severity::Info, "Added custom.added" });
    });

    auto cfg = make_v2_config();
    auto result = migrator.migrate(cfg, 3);

    REQUIRE(result.success);
    REQUIRE(ConfigMigrator::detect_version(result.migrated_config) == 3);
    REQUIRE(result.migrated_config.get_string("custom.added", "") == "hello");
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
TEST_CASE("migration produces diagnostics", "[core][p1-009][ConfigMigration]") {
    ConfigMigrator migrator;
    auto cfg = make_legacy_config();
    auto result = migrator.migrate(cfg, 2);

    bool found_v0_v1 = false;
    bool found_v1_v2 = false;
    for (const auto& d : result.diagnostics) {
        if (d.message.find("v0 (legacy) -> v1") != std::string::npos) found_v0_v1 = true;
        if (d.message.find("v1 -> v2") != std::string::npos)           found_v1_v2 = true;
    }
    REQUIRE(found_v0_v1);
    REQUIRE(found_v1_v2);
}
