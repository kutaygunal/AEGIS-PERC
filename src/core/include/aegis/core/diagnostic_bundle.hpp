#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Bundle export progress
// ---------------------------------------------------------------------------
struct BundleProgress {
    std::string current_step;
    std::size_t files_processed = 0;
    std::size_t total_files = 0;
    bool complete = false;
    bool failed = false;
    std::string error_message;
};

// ---------------------------------------------------------------------------
// Bundle export result
// ---------------------------------------------------------------------------
struct BundleResult {
    std::filesystem::path bundle_path;
    bool success = false;
    std::string error_message;
};

// ---------------------------------------------------------------------------
// Diagnostic bundle exporter
//
// Creates a timestamped zip archive containing:
//   - logs/          (all .log files from the log directory)
//   - config_snapshot.json  (redacted)
//   - build_info.json
//   - os_info.json
//
// Sensitive values are redacted by default.
// ---------------------------------------------------------------------------
class DiagnosticBundle {
public:
    // Create a timestamped zip bundle.
    // Reports progress via the optional callback.
    static BundleResult create(
        const std::filesystem::path& output_dir,
        const std::filesystem::path& log_dir,
        const std::string& config_json_snapshot,
        std::function<void(const BundleProgress&)> on_progress = nullptr);

    // Redact known sensitive keys from a JSON config string.
    static std::string redact_sensitive_keys(const std::string& config_json);
};

} // namespace aegis::core
