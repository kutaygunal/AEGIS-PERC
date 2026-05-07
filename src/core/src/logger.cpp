#include "aegis/core/logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/common.h>

#include <algorithm>
#include <vector>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Level mapping
// ---------------------------------------------------------------------------
static spdlog::level::level_enum to_spdlog(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info:  return spdlog::level::info;
        case LogLevel::Warn:  return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Fatal: return spdlog::level::critical;
        case LogLevel::Off:   return spdlog::level::off;
    }
    return spdlog::level::info;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct Logger::Impl {
public:
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> file_sink;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink;
};

std::unique_ptr<Logger::Impl> Logger::s_impl = nullptr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void Logger::initialize(const LoggerConfig& config) {
    if (s_impl) {
        shutdown();
    }

    s_impl = std::make_unique<Impl>();

    std::vector<spdlog::sink_ptr> sinks;

    // Console sink
    if (config.console_enabled) {
        s_impl->console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        s_impl->console_sink->set_level(to_spdlog(config.console_level));
        sinks.push_back(s_impl->console_sink);
    }

    // Rotating file sink
    if (config.file_enabled) {
        std::filesystem::create_directories(config.log_dir);
        auto log_path = (config.log_dir / "aegis-perc.log").string();
        s_impl->file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path,
            config.max_file_size_bytes,
            config.max_files);
        s_impl->file_sink->set_level(to_spdlog(config.file_level));
        sinks.push_back(s_impl->file_sink);
    }

    s_impl->logger = std::make_shared<spdlog::logger>("aegis", sinks.begin(), sinks.end());
    s_impl->logger->set_level(spdlog::level::trace); // let sinks filter; logger passes everything
    s_impl->logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread=%t] %v");
    s_impl->logger->flush_on(spdlog::level::err);
}

void Logger::shutdown() {
    if (s_impl && s_impl->logger) {
        s_impl->logger->flush();
    }
    s_impl.reset();
}

bool Logger::is_initialized() {
    return s_impl != nullptr;
}

// ---------------------------------------------------------------------------
// Severity control
// ---------------------------------------------------------------------------
void Logger::set_level(LogLevel level) {
    if (!s_impl || !s_impl->logger) return;
    auto sl = to_spdlog(level);
    s_impl->logger->set_level(sl);
    if (s_impl->console_sink) s_impl->console_sink->set_level(sl);
    if (s_impl->file_sink)    s_impl->file_sink->set_level(sl);
}

void Logger::set_console_level(LogLevel level) {
    if (!s_impl || !s_impl->console_sink) return;
    s_impl->console_sink->set_level(to_spdlog(level));
}

void Logger::set_file_level(LogLevel level) {
    if (!s_impl || !s_impl->file_sink) return;
    s_impl->file_sink->set_level(to_spdlog(level));
}

// ---------------------------------------------------------------------------
// Logging helpers
// ---------------------------------------------------------------------------
void Logger::log(LogLevel level,
                 const std::string& module,
                 const std::string& message) {
    if (!is_initialized()) return;
    auto sl = to_spdlog(level);
    if (!s_impl->logger->should_log(sl)) return;
    s_impl->logger->log(sl, "[{}] {}", module, message);
}

void Logger::trace(const std::string& module, const std::string& message) {
    log(LogLevel::Trace, module, message);
}

void Logger::debug(const std::string& module, const std::string& message) {
    log(LogLevel::Debug, module, message);
}

void Logger::info(const std::string& module, const std::string& message) {
    log(LogLevel::Info, module, message);
}

void Logger::warn(const std::string& module, const std::string& message) {
    log(LogLevel::Warn, module, message);
}

void Logger::error(const std::string& module, const std::string& message) {
    log(LogLevel::Error, module, message);
}

void Logger::fatal(const std::string& module, const std::string& message) {
    log(LogLevel::Fatal, module, message);
}

} // namespace aegis::core
