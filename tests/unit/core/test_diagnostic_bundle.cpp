#include <catch2/catch_test_macros.hpp>
#include "aegis/core/diagnostic_bundle.hpp"
#include "aegis/core/config.hpp"

#include <zip.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void write_file(const fs::path& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

static std::vector<std::string> list_zip_entries(const fs::path& zip_path) {
    std::vector<std::string> names;
    struct zip_t* zip = zip_open(zip_path.string().c_str(), 0, 'r');
    if (!zip) return names;
    ssize_t n = zip_entries_total(zip);
    for (ssize_t i = 0; i < n; ++i) {
        if (zip_entry_openbyindex(zip, static_cast<size_t>(i)) == 0) {
            const char* name = zip_entry_name(zip);
            if (name) names.emplace_back(name);
            zip_entry_close(zip);
        }
    }
    zip_close(zip);
    return names;
}

static std::string read_zip_entry(const fs::path& zip_path, const char* entry_name) {
    struct zip_t* zip = zip_open(zip_path.string().c_str(), 0, 'r');
    if (!zip) return {};
    std::string result;
    if (zip_entry_open(zip, entry_name) == 0) {
        void* buf = nullptr;
        size_t bufsize = 0;
        if (zip_entry_read(zip, &buf, &bufsize) >= 0 && buf) {
            result.assign(static_cast<char*>(buf), bufsize);
            std::free(buf);
        }
        zip_entry_close(zip);
    }
    zip_close(zip);
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_CASE("DiagnosticBundle creates a zip with expected entries", "[core][p1-005][DiagnosticBundle]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_bundle";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    fs::path log_dir = tmp / "logs";
    fs::create_directories(log_dir);
    write_file(log_dir / "aegis-perc.log", "test log line 1\ntest log line 2\n");
    write_file(log_dir / "aegis-perc.1.log", "rotated log\n");

    aegis::core::Config cfg;
    cfg.load_from_string(R"({
        "core": { "logging": { "level": "debug" } },
        "api": { "key": "super_secret_123", "endpoint": "https://example.com" }
    })");

    std::string config_json = cfg.to_json_string(2);

    fs::path output_dir = tmp / "out";
    auto result = aegis::core::DiagnosticBundle::create(output_dir, log_dir, config_json);

    REQUIRE(result.success);
    REQUIRE(fs::exists(result.bundle_path));
    REQUIRE(fs::file_size(result.bundle_path) > 0);

    auto entries = list_zip_entries(result.bundle_path);
    std::set<std::string> entry_set(entries.begin(), entries.end());

    REQUIRE(entry_set.count("config_snapshot.json") == 1);
    REQUIRE(entry_set.count("build_info.json") == 1);
    REQUIRE(entry_set.count("os_info.json") == 1);

    bool has_log = false;
    for (const auto& e : entries) {
        if (e.find("logs/") != std::string::npos && e.find(".log") != std::string::npos) {
            has_log = true;
        }
    }
    REQUIRE(has_log);

    fs::remove_all(tmp);
}

TEST_CASE("DiagnosticBundle redacts sensitive keys", "[core][p1-005][DiagnosticBundle]")
{
    std::string raw = R"({
        "password": "hunter2",
        "api_key": "sk-12345",
        "secret_token": "abc",
        "normal_key": "visible",
        "nested": {
            "private_key": "pk-999",
            "credential": "admin:pass"
        }
    })";

    std::string redacted = aegis::core::DiagnosticBundle::redact_sensitive_keys(raw);
    REQUIRE(redacted.find("hunter2") == std::string::npos);
    REQUIRE(redacted.find("sk-12345") == std::string::npos);
    REQUIRE(redacted.find("abc") == std::string::npos);
    REQUIRE(redacted.find("pk-999") == std::string::npos);
    REQUIRE(redacted.find("admin:pass") == std::string::npos);
    REQUIRE(redacted.find("visible") != std::string::npos);
    REQUIRE(redacted.find("[REDACTED]") != std::string::npos);
}

TEST_CASE("DiagnosticBundle redacts inside config snapshot in bundle", "[core][p1-005][DiagnosticBundle]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_bundle_redact";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    fs::path log_dir = tmp / "logs";
    fs::create_directories(log_dir);
    write_file(log_dir / "dummy.log", "dummy");

    std::string raw = R"({"password":"secret123","normal":"keep"})";
    auto result = aegis::core::DiagnosticBundle::create(tmp / "out", log_dir, raw);
    REQUIRE(result.success);

    std::string config_snapshot = read_zip_entry(result.bundle_path, "config_snapshot.json");
    REQUIRE(config_snapshot.find("secret123") == std::string::npos);
    REQUIRE(config_snapshot.find("[REDACTED]") != std::string::npos);
    REQUIRE(config_snapshot.find("keep") != std::string::npos);

    fs::remove_all(tmp);
}

TEST_CASE("DiagnosticBundle progress callbacks", "[core][p1-005][DiagnosticBundle]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_bundle_progress";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    fs::path log_dir = tmp / "logs";
    fs::create_directories(log_dir);
    write_file(log_dir / "test.log", "hello");

    std::vector<std::string> steps;
    auto cb = [&steps](const aegis::core::BundleProgress& p) {
        steps.push_back(p.current_step);
    };

    auto result = aegis::core::DiagnosticBundle::create(tmp / "out", log_dir, "{}", cb);
    REQUIRE(result.success);
    REQUIRE(!steps.empty());
    REQUIRE(steps.back() == "Complete");

    fs::remove_all(tmp);
}

TEST_CASE("DiagnosticBundle handles missing log dir gracefully", "[core][p1-005][DiagnosticBundle]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_bundle_nologs";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    auto result = aegis::core::DiagnosticBundle::create(tmp / "out", tmp / "nonexistent", "{}");
    REQUIRE(result.success);
    REQUIRE(fs::exists(result.bundle_path));

    // Should still contain the three metadata files
    auto entries = list_zip_entries(result.bundle_path);
    bool has_config = false, has_build = false, has_os = false;
    for (const auto& e : entries) {
        if (e == "config_snapshot.json") has_config = true;
        if (e == "build_info.json") has_build = true;
        if (e == "os_info.json") has_os = true;
    }
    REQUIRE(has_config);
    REQUIRE(has_build);
    REQUIRE(has_os);

    fs::remove_all(tmp);
}
