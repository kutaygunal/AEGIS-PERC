#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::parsing {

struct CurrentActivityRecord {
    std::string net_name;
    double current_milliamps = 0.0;
    std::string voltage_domain;
    std::string layer;
    std::size_t source_row = 0;
};

struct CurrentActivityDiagnostic {
    enum class Severity {
        Warning,
        Error
    };

    Severity severity = Severity::Warning;
    std::string code;
    std::string message;
    std::optional<std::size_t> source_row;
    std::optional<std::string> net_name;
};

struct CurrentActivityData {
    std::vector<CurrentActivityRecord> records;
    std::vector<CurrentActivityDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class CurrentActivityParser {
public:
    CurrentActivityData parse_csv_string(const std::string& content,
                                         const std::string& source_name = "<memory>") const;
    CurrentActivityData parse_csv_file(const std::filesystem::path& path) const;
};

} // namespace aegis::parsing
