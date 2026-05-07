#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Log severity levels (ordered from most verbose to least)
// ---------------------------------------------------------------------------
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

// ---------------------------------------------------------------------------
// Logger configuration
// ---------------------------------------------------------------------------
struct LoggerConfig {
    std::filesystem::path log_dir;
    std::size_t max_file_size_bytes = 5 * 1024 * 1024; // 5 MB
    std::size_t max_files = 3;
    LogLevel file_level = LogLevel::Debug;
    LogLevel console_level = LogLevel::Info;
    bool console_enabled = true;
    bool file_enabled = true;
};

// ---------------------------------------------------------------------------
// Structured rotating logger
//
// Thin wrapper around spdlog providing:
//   - rotating file sink with configurable size/count
//   - optional console sink
//   - per-call module tagging
//   - thread id in output
//   - runtime severity changes
//
// This is a process-wide convenience facade. For testability,
// callers can initialize/shutdown explicitly. In distributed
// or multi-session scenarios, use the service registry to hold
// an IService-wrapped logger instance instead.
// ---------------------------------------------------------------------------
class Logger {
public:
    // Lifecycle ---------------------------------------------------------------
    static void initialize(const LoggerConfig& config);
    static void shutdown();
    static bool is_initialized();

    // Severity control ------------------------------------------------------
    static void set_level(LogLevel level);          // affects all sinks
    static void set_console_level(LogLevel level);
    static void set_file_level(LogLevel level);

    // Logging -----------------------------------------------------------------
    static void log(LogLevel level,
                    const std::string& module,
                    const std::string& message);

    static void trace(const std::string& module, const std::string& message);
    static void debug(const std::string& module, const std::string& message);
    static void info (const std::string& module, const std::string& message);
    static void warn (const std::string& module, const std::string& message);
    static void error(const std::string& module, const std::string& message);
    static void fatal(const std::string& module, const std::string& message);

private:
    struct Impl;
    static std::unique_ptr<Impl> s_impl;
};

} // namespace aegis::core
