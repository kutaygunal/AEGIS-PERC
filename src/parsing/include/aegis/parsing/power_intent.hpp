#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::parsing {

struct PowerIntentAssignment {
    std::string instance_name;
    std::string domain_name;
    double voltage = 0.0;
    std::size_t source_row = 0;
};

struct PowerIntentDiagnostic {
    enum class Severity {
        Warning,
        Error
    };

    Severity severity = Severity::Warning;
    std::string code;
    std::string message;
    std::optional<std::size_t> source_row;
    std::optional<std::string> instance_name;
};

struct PowerIntentData {
    std::vector<PowerIntentAssignment> assignments;
    std::vector<PowerIntentDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class PowerIntentParser {
public:
    PowerIntentData parse_csv_string(const std::string& content,
                                     const std::string& source_name = "<memory>") const;
    PowerIntentData parse_csv_file(const std::filesystem::path& path) const;
};

} // namespace aegis::parsing
