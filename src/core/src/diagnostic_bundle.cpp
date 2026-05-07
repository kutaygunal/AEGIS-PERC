#include "aegis/core/diagnostic_bundle.hpp"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace aegis::core {

namespace {

void report_progress(std::function<void(const BundleProgress&)>& cb, BundleProgress& p) {
    if (cb) cb(p);
}

std::string timestamp_filename() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "aegis-diagnostic-"
        << std::put_time(&tm, "%Y-%m-%d-%H%M%S")
        << ".zip";
    return oss.str();
}

nlohmann::json collect_build_info() {
    nlohmann::json j;
#ifdef _MSC_VER
    j["compiler"] = std::string("MSVC ") + std::to_string(_MSC_VER);
#elif defined(__clang__)
    j["compiler"] = std::string("Clang ") + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    j["compiler"] = std::string("GCC ") + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    j["compiler"] = "Unknown";
#endif
    j["build_date"] = __DATE__;
    j["build_time"] = __TIME__;
    j["cxx_standard"] = __cplusplus;
    return j;
}

nlohmann::json collect_os_info() {
    nlohmann::json j;
#ifdef _WIN32
    j["platform"] = "Windows";
#elif defined(__linux__)
    j["platform"] = "Linux";
#elif defined(__APPLE__)
    j["platform"] = "macOS";
#else
    j["platform"] = "Unknown";
#endif
    j["thread_count"] = static_cast<int>(std::thread::hardware_concurrency());
    return j;
}

bool is_sensitive_key(const std::string& key) {
    static const std::vector<std::string> patterns = {
        "password", "secret", "token", "credential", "api_key", "private_key", "auth"
    };
    std::string lower;
    lower.reserve(key.size());
    for (char c : key) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& p : patterns) {
        if (lower.find(p) != std::string::npos) return true;
    }
    return false;
}

void redact_json(nlohmann::json& j) {
    if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            if (is_sensitive_key(key) && value.is_string()) {
                value = "[REDACTED]";
            } else {
                redact_json(value);
            }
        }
    } else if (j.is_array()) {
        for (auto& el : j) {
            redact_json(el);
        }
    }
}

bool add_buffer(struct zip_t* zip, const char* entry_name, const std::string& content) {
    if (zip_entry_open(zip, entry_name) != 0) return false;
    bool ok = zip_entry_write(zip, content.data(), content.size()) == 0;
    zip_entry_close(zip);
    return ok;
}

bool add_file(struct zip_t* zip, const std::filesystem::path& file_path, const char* entry_name) {
    if (zip_entry_open(zip, entry_name) != 0) return false;
    bool ok = zip_entry_fwrite(zip, file_path.string().c_str()) == 0;
    zip_entry_close(zip);
    return ok;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Redaction
// ---------------------------------------------------------------------------
std::string DiagnosticBundle::redact_sensitive_keys(const std::string& config_json) {
    try {
        auto j = nlohmann::json::parse(config_json);
        redact_json(j);
        return j.dump(2);
    } catch (...) {
        return config_json;
    }
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
BundleResult DiagnosticBundle::create(
    const std::filesystem::path& output_dir,
    const std::filesystem::path& log_dir,
    const std::string& config_json_snapshot,
    std::function<void(const BundleProgress&)> on_progress)
{
    BundleResult result;
    BundleProgress progress;

    std::filesystem::create_directories(output_dir);
    auto zip_path = output_dir / timestamp_filename();

    // Enumerate log files
    std::vector<std::filesystem::path> log_files;
    if (std::filesystem::exists(log_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(log_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                log_files.push_back(entry.path());
            }
        }
    }
    progress.total_files = 3 + log_files.size(); // config + build + os + logs

    struct zip_t* zip = zip_open(zip_path.string().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (!zip) {
        result.error_message = "Failed to create zip archive";
        progress.failed = true;
        progress.error_message = result.error_message;
        report_progress(on_progress, progress);
        return result;
    }

    // Config snapshot (redacted)
    progress.current_step = "Adding config snapshot";
    std::string redacted = redact_sensitive_keys(config_json_snapshot);
    if (!add_buffer(zip, "config_snapshot.json", redacted)) {
        zip_close(zip);
        result.error_message = "Failed to add config snapshot";
        progress.failed = true;
        progress.error_message = result.error_message;
        report_progress(on_progress, progress);
        return result;
    }
    ++progress.files_processed;
    report_progress(on_progress, progress);

    // Build info
    progress.current_step = "Adding build info";
    if (!add_buffer(zip, "build_info.json", collect_build_info().dump(2))) {
        zip_close(zip);
        result.error_message = "Failed to add build info";
        progress.failed = true;
        progress.error_message = result.error_message;
        report_progress(on_progress, progress);
        return result;
    }
    ++progress.files_processed;
    report_progress(on_progress, progress);

    // OS info
    progress.current_step = "Adding OS info";
    if (!add_buffer(zip, "os_info.json", collect_os_info().dump(2))) {
        zip_close(zip);
        result.error_message = "Failed to add OS info";
        progress.failed = true;
        progress.error_message = result.error_message;
        report_progress(on_progress, progress);
        return result;
    }
    ++progress.files_processed;
    report_progress(on_progress, progress);

    // Log files
    for (const auto& log : log_files) {
        progress.current_step = "Adding log: " + log.filename().string();
        add_file(zip, log, ("logs/" + log.filename().string()).c_str());
        ++progress.files_processed;
        report_progress(on_progress, progress);
    }

    zip_close(zip);

    result.bundle_path = zip_path;
    result.success = true;
    progress.complete = true;
    progress.current_step = "Complete";
    report_progress(on_progress, progress);
    return result;
}

} // namespace aegis::core
