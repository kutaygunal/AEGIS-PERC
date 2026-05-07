#include <catch2/catch_test_macros.hpp>
#include "aegis/core/config.hpp"

#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static fs::path make_temp_path(const std::string& name) {
    return fs::temp_directory_path() / name;
}

static void write_file(const fs::path& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_CASE("Config load from JSON string and flat accessors", "[core][p1-004][Config]")
{
    aegis::core::Config cfg;
    REQUIRE(cfg.load_from_string(R"({
        "core": {
            "logging": {
                "level": "debug",
                "max_size": 1048576
            }
        },
        "ui": {
            "window": {
                "width": 1280,
                "height": 720
            }
        }
    })"));

    REQUIRE(cfg.has("core.logging.level"));
    REQUIRE(cfg.get_string("core.logging.level", "") == "debug");
    REQUIRE(cfg.get_int("core.logging.max_size", 0) == 1048576);
    REQUIRE(cfg.get_int("ui.window.width", 0) == 1280);
    REQUIRE(cfg.get_int("ui.window.height", 0) == 720);

    REQUIRE(!cfg.has("missing.key"));
    REQUIRE(cfg.get_string("missing.key", "fallback") == "fallback");
    REQUIRE(cfg.get_bool("missing.flag", true) == true);
    REQUIRE(cfg.get_int("missing.num", 42) == 42);
    REQUIRE(cfg.get_double("missing.d", 3.14) == 3.14);
}

TEST_CASE("Config load from file", "[core][p1-004][Config]")
{
    fs::path p = make_temp_path("aegis_test_config.json");
    write_file(p, R"({"core":{"version":1,"enabled":true}})");

    aegis::core::Config cfg;
    REQUIRE(cfg.load_from_file(p));
    REQUIRE(cfg.get_int("core.version", 0) == 1);
    REQUIRE(cfg.get_bool("core.enabled", false) == true);

    fs::remove(p);
}

TEST_CASE("Config schema validation", "[core][p1-004][Config]")
{
    aegis::core::ConfigSchema schema;
    schema["core.logging.level"]  = { aegis::core::ConfigType::String,  "info", true,  "Log level" };
    schema["core.logging.max_size"] = { aegis::core::ConfigType::Integer, 5242880, false, "Max file size" };
    schema["ui.window.width"]     = { aegis::core::ConfigType::Integer, 1280,    false, "Window width" };

    aegis::core::Config cfg;
    cfg.load_from_string(R"({
        "core": { "logging": { "level": "debug", "max_size": 1048576 } },
        "ui": { "window": { "width": 1920 } }
    })");

    // Valid config passes
    REQUIRE_NOTHROW(cfg.validate(schema));

    // Missing required key fails
    aegis::core::Config cfg2;
    cfg2.load_from_string(R"({"core":{"logging":{}}})");
    REQUIRE_THROWS_AS(cfg2.validate(schema), aegis::core::ConfigValidationException);

    // Unknown key fails
    aegis::core::Config cfg3;
    cfg3.load_from_string(R"({"unknown":{"key":123}})");
    REQUIRE_THROWS_AS(cfg3.validate(schema), aegis::core::ConfigValidationException);

    // Wrong type fails
    aegis::core::Config cfg4;
    cfg4.load_from_string(R"({"core":{"logging":{"level":123}}})");
    REQUIRE_THROWS_AS(cfg4.validate(schema), aegis::core::ConfigValidationException);
}

TEST_CASE("Config defaults and fallback", "[core][p1-004][Config]")
{
    aegis::core::ConfigSchema schema;
    schema["core.logging.level"]  = { aegis::core::ConfigType::String,  std::string("info"), true,  "Log level" };
    schema["core.logging.max_size"] = { aegis::core::ConfigType::Integer, std::int64_t(5242880), false, "Max file size" };
    schema["ui.theme"]            = { aegis::core::ConfigType::String,  std::string("dark"),   false, "UI theme" };

    aegis::core::Config cfg = aegis::core::Config::make_with_defaults(schema);

    // Defaults are returned when no override exists
    REQUIRE(cfg.get_string("core.logging.level", "") == "info");
    REQUIRE(cfg.get_int("core.logging.max_size", 0) == 5242880);
    REQUIRE(cfg.get_string("ui.theme", "") == "dark");

    // Override a default
    cfg.set("core.logging.level", std::string("debug"));
    REQUIRE(cfg.get_string("core.logging.level", "") == "debug");
}

TEST_CASE("Config observability", "[core][p1-004][Config]")
{
    aegis::core::Config cfg;
    cfg.load_from_string(R"({"core":{"flag":true}})");

    std::set<std::string> changed_keys;
    std::size_t h1 = cfg.on_change([&changed_keys](const std::string& key, const aegis::core::ConfigValue&) {
        changed_keys.insert(key);
    });

    cfg.set("core.flag", false);
    REQUIRE(changed_keys.count("core.flag") == 1);

    cfg.set("core.new_key", std::string("hello"));
    REQUIRE(changed_keys.count("core.new_key") == 1);

    // Unsubscribe and verify no more callbacks
    cfg.unsubscribe(h1);
    std::size_t before = changed_keys.size();
    cfg.set("core.flag", true);
    REQUIRE(changed_keys.size() == before);
}

TEST_CASE("Config JSON round-trip", "[core][p1-004][Config]")
{
    aegis::core::Config cfg;
    cfg.load_from_string(R"({
        "a": 1,
        "b": "text",
        "c": true,
        "nested": { "x": 42 }
    })");

    std::string json = cfg.to_json_string(2);
    REQUIRE(!json.empty());
    REQUIRE(json.find("\"a\": 1") != std::string::npos);
    REQUIRE(json.find("\"b\": \"text\"") != std::string::npos);
    REQUIRE(json.find("\"c\": true") != std::string::npos);
    REQUIRE(json.find("\"x\": 42") != std::string::npos);
}
