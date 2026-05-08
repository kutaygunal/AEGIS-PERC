#include "aegis/parsing/power_intent.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace aegis::parsing {
namespace {

std::string trim(std::string s)
{
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> split_csv_line(const std::string& line)
{
    std::vector<std::string> cells;
    std::string current;
    bool in_quotes = false;

    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (c == ',' && !in_quotes) {
            cells.push_back(trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    cells.push_back(trim(current));
    return cells;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open power-domain CSV file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

bool PowerIntentData::has_errors() const noexcept
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == PowerIntentDiagnostic::Severity::Error) {
            return true;
        }
    }
    return false;
}

PowerIntentData PowerIntentParser::parse_csv_string(const std::string& content,
                                                    const std::string& /*source_name*/) const
{
    PowerIntentData data;
    std::istringstream input(content);
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    std::unordered_map<std::string, std::size_t> index_by_name;
    std::unordered_map<std::string, std::size_t> assignment_by_instance;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string stripped = trim(line);
        if (stripped.empty()) {
            continue;
        }

        const auto cells = split_csv_line(line);
        if (!header_seen) {
            for (std::size_t i = 0; i < cells.size(); ++i) {
                index_by_name[to_lower(cells[i])] = i;
            }
            header_seen = true;
            if (!index_by_name.count("instance") || !index_by_name.count("domain") || !index_by_name.count("voltage")) {
                data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                            "POWER_CSV_HEADER_INVALID",
                                            "CSV header must contain instance, domain, and voltage columns",
                                            line_number,
                                            std::nullopt});
                return data;
            }
            continue;
        }

        const auto read_cell = [&](const std::string& name) -> std::string {
            const auto idx = index_by_name.at(name);
            return idx < cells.size() ? trim(cells[idx]) : std::string{};
        };

        PowerIntentAssignment assignment;
        assignment.instance_name = read_cell("instance");
        assignment.domain_name = read_cell("domain");
        assignment.source_row = line_number;
        const std::string voltage_text = read_cell("voltage");

        if (assignment.instance_name.empty()) {
            data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                        "POWER_INSTANCE_MISSING",
                                        "Power-domain row is missing required instance name",
                                        line_number,
                                        std::nullopt});
            continue;
        }
        if (assignment.domain_name.empty()) {
            data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                        "POWER_DOMAIN_MISSING",
                                        "Power-domain row is missing required domain name",
                                        line_number,
                                        assignment.instance_name});
            continue;
        }
        if (voltage_text.empty()) {
            data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                        "POWER_VOLTAGE_MISSING",
                                        "Power-domain row is missing required voltage value",
                                        line_number,
                                        assignment.instance_name});
            continue;
        }

        try {
            std::size_t pos = 0;
            assignment.voltage = std::stod(voltage_text, &pos);
            if (pos != voltage_text.size()) {
                throw std::invalid_argument("trailing characters");
            }
        } catch (...) {
            data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                        "POWER_VOLTAGE_INVALID",
                                        "Power-domain voltage must be numeric for instance '" + assignment.instance_name + "'",
                                        line_number,
                                        assignment.instance_name});
            continue;
        }

        if (const auto it = assignment_by_instance.find(assignment.instance_name); it != assignment_by_instance.end()) {
            data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Warning,
                                        "POWER_INSTANCE_DUPLICATE",
                                        "Duplicate power-domain mapping for instance '" + assignment.instance_name + "'; latest row wins",
                                        line_number,
                                        assignment.instance_name});
            data.assignments[it->second] = assignment;
            continue;
        }

        assignment_by_instance[assignment.instance_name] = data.assignments.size();
        data.assignments.push_back(std::move(assignment));
    }

    if (!header_seen) {
        data.diagnostics.push_back({PowerIntentDiagnostic::Severity::Error,
                                    "POWER_CSV_EMPTY",
                                    "Power-domain CSV is empty",
                                    std::nullopt,
                                    std::nullopt});
    }

    return data;
}

PowerIntentData PowerIntentParser::parse_csv_file(const std::filesystem::path& path) const
{
    return parse_csv_string(read_text_file(path), path.string());
}

} // namespace aegis::parsing
