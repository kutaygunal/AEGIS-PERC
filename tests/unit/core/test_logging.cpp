#include <catch2/catch_test_macros.hpp>
#include "aegis/core/logger.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::vector<std::string> read_lines(const fs::path& p) {
    std::vector<std::string> lines;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
    }
    return lines;
}

static int count_log_files(const fs::path& dir) {
    int count = 0;
    if (!fs::exists(dir)) return 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            ++count;
        }
    }
    return count;
}

static std::vector<std::string> read_all_log_lines(const fs::path& dir) {
    std::vector<std::string> all;
    if (!fs::exists(dir)) return all;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            auto lines = read_lines(entry.path());
            all.insert(all.end(), lines.begin(), lines.end());
        }
    }
    return all;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_CASE("Logger lifecycle", "[core][p1-003][Logging]")
{
    REQUIRE(!aegis::core::Logger::is_initialized());

    fs::path tmp = fs::temp_directory_path() / "aegis_test_lifecycle";
    fs::remove_all(tmp);

    aegis::core::LoggerConfig cfg;
    cfg.log_dir = tmp;
    cfg.file_enabled = true;
    cfg.console_enabled = false;
    cfg.file_level = aegis::core::LogLevel::Debug;

    aegis::core::Logger::initialize(cfg);
    REQUIRE(aegis::core::Logger::is_initialized());

    aegis::core::Logger::shutdown();
    REQUIRE(!aegis::core::Logger::is_initialized());

    fs::remove_all(tmp);
}

TEST_CASE("Logger severity filtering", "[core][p1-003][Logging]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_severity";
    fs::remove_all(tmp);

    aegis::core::LoggerConfig cfg;
    cfg.log_dir = tmp;
    cfg.file_enabled = true;
    cfg.console_enabled = false;
    cfg.file_level = aegis::core::LogLevel::Info; // drop Trace / Debug

    aegis::core::Logger::initialize(cfg);
    aegis::core::Logger::trace("mod", "trace_msg");
    aegis::core::Logger::debug("mod", "debug_msg");
    aegis::core::Logger::info("mod", "info_msg");
    aegis::core::Logger::error("mod", "error_msg");
    aegis::core::Logger::shutdown();

    auto lines = read_all_log_lines(tmp);
    REQUIRE(!lines.empty());

    bool has_trace = false, has_debug = false, has_info = false, has_error = false;
    for (const auto& line : lines) {
        if (line.find("trace_msg") != std::string::npos) has_trace = true;
        if (line.find("debug_msg") != std::string::npos) has_debug = true;
        if (line.find("info_msg")  != std::string::npos) has_info  = true;
        if (line.find("error_msg") != std::string::npos) has_error = true;
    }

    REQUIRE(!has_trace);
    REQUIRE(!has_debug);
    REQUIRE(has_info);
    REQUIRE(has_error);

    fs::remove_all(tmp);
}

TEST_CASE("Logger output format", "[core][p1-003][Logging]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_format";
    fs::remove_all(tmp);

    aegis::core::LoggerConfig cfg;
    cfg.log_dir = tmp;
    cfg.file_enabled = true;
    cfg.console_enabled = false;
    cfg.file_level = aegis::core::LogLevel::Info;

    aegis::core::Logger::initialize(cfg);
    aegis::core::Logger::info("graph_engine", "format_check");
    aegis::core::Logger::shutdown();

    auto lines = read_lines(tmp / "aegis-perc.log");
    REQUIRE(!lines.empty());

    const auto& line = lines[0];
    REQUIRE(line.find("graph_engine") != std::string::npos); // module tag
    REQUIRE(line.find("thread=")     != std::string::npos); // thread id
    REQUIRE(line.find("[info]")     != std::string::npos); // severity
    REQUIRE(line.find("format_check") != std::string::npos); // message

    fs::remove_all(tmp);
}

TEST_CASE("Logger file rotation", "[core][p1-003][Logging]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_rotation";
    fs::remove_all(tmp);

    aegis::core::LoggerConfig cfg;
    cfg.log_dir = tmp;
    cfg.file_enabled = true;
    cfg.console_enabled = false;
    cfg.file_level = aegis::core::LogLevel::Info;
    cfg.max_file_size_bytes = 32; // tiny to force rotation
    cfg.max_files = 2;

    aegis::core::Logger::initialize(cfg);
    for (int i = 0; i < 30; ++i) {
        aegis::core::Logger::info("rot", "msg_" + std::to_string(i));
    }
    aegis::core::Logger::shutdown();

    int file_count = count_log_files(tmp);
    REQUIRE(file_count > 1); // rotation produced multiple files

    fs::remove_all(tmp);
}

TEST_CASE("Logger independent sink levels", "[core][p1-003][Logging]")
{
    fs::path tmp = fs::temp_directory_path() / "aegis_test_independent";
    fs::remove_all(tmp);

    aegis::core::LoggerConfig cfg;
    cfg.log_dir = tmp;
    cfg.file_enabled = true;
    cfg.console_enabled = true;
    cfg.file_level    = aegis::core::LogLevel::Warn;
    cfg.console_level = aegis::core::LogLevel::Info;

    aegis::core::Logger::initialize(cfg);
    aegis::core::Logger::info("mod", "info_msg");
    aegis::core::Logger::warn("mod", "warn_msg");
    aegis::core::Logger::shutdown();

    // File should only contain warn (info filtered by file sink)
    auto lines = read_all_log_lines(tmp);
    bool has_warn = false, has_info = false;
    for (const auto& line : lines) {
        if (line.find("warn_msg") != std::string::npos) has_warn = true;
        if (line.find("info_msg") != std::string::npos) has_info = true;
    }
    REQUIRE(has_warn);
    REQUIRE(!has_info);

    fs::remove_all(tmp);
}
