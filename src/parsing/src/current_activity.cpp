#include "aegis/parsing/current_activity.hpp"

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
        throw std::runtime_error("Unable to open current/activity CSV file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool parse_current_milliamps(const std::string& text, double& out_value)
{
    std::string normalized = trim(text);
    if (normalized.empty()) {
        return false;
    }

    std::string lowered = to_lower(normalized);
    double multiplier = 1.0;
    if (lowered.size() >= 2 && lowered.substr(lowered.size() - 2) == "ma") {
        lowered = trim(lowered.substr(0, lowered.size() - 2));
        multiplier = 1.0;
    } else if (!lowered.empty() && lowered.back() == 'a') {
        lowered = trim(lowered.substr(0, lowered.size() - 1));
        multiplier = 1000.0;
    }

    try {
        std::size_t pos = 0;
        const double numeric = std::stod(lowered, &pos);
        if (pos != lowered.size()) {
            return false;
        }
        out_value = numeric * multiplier;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool CurrentActivityData::has_errors() const noexcept
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == CurrentActivityDiagnostic::Severity::Error) {
            return true;
        }
    }
    return false;
}

CurrentActivityData CurrentActivityParser::parse_csv_string(const std::string& content,
                                                            const std::string& /*source_name*/) const
{
    CurrentActivityData data;
    std::istringstream input(content);
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    std::unordered_map<std::string, std::size_t> index_by_name;
    std::unordered_map<std::string, std::size_t> record_by_net;

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
            if (!index_by_name.count("net_name") || !index_by_name.count("current_ma") ||
                !index_by_name.count("voltage_domain") || !index_by_name.count("layer")) {
                data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                            "CURRENT_CSV_HEADER_INVALID",
                                            "CSV header must contain net_name, current_mA, voltage_domain, and layer columns",
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

        CurrentActivityRecord record;
        record.net_name = read_cell("net_name");
        record.voltage_domain = read_cell("voltage_domain");
        record.layer = read_cell("layer");
        record.source_row = line_number;
        const std::string current_text = read_cell("current_ma");

        if (record.net_name.empty()) {
            data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                        "CURRENT_NET_MISSING",
                                        "Current/activity row is missing required net name",
                                        line_number,
                                        std::nullopt});
            continue;
        }
        if (current_text.empty()) {
            data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                        "CURRENT_VALUE_MISSING",
                                        "Current/activity row is missing required current value",
                                        line_number,
                                        record.net_name});
            continue;
        }
        if (record.voltage_domain.empty()) {
            data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                        "CURRENT_DOMAIN_MISSING",
                                        "Current/activity row is missing required voltage domain",
                                        line_number,
                                        record.net_name});
            continue;
        }
        if (record.layer.empty()) {
            data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                        "CURRENT_LAYER_MISSING",
                                        "Current/activity row is missing required layer",
                                        line_number,
                                        record.net_name});
            continue;
        }
        if (!parse_current_milliamps(current_text, record.current_milliamps)) {
            data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                        "CURRENT_VALUE_INVALID",
                                        "Current/activity value must be numeric and use supported units for net '" + record.net_name + "'",
                                        line_number,
                                        record.net_name});
            continue;
        }

        if (const auto it = record_by_net.find(record.net_name); it != record_by_net.end()) {
            const auto& previous = data.records[it->second];
            const bool conflict = previous.layer != record.layer ||
                                  previous.voltage_domain != record.voltage_domain ||
                                  previous.current_milliamps != record.current_milliamps;
            data.diagnostics.push_back({conflict ? CurrentActivityDiagnostic::Severity::Warning
                                                 : CurrentActivityDiagnostic::Severity::Warning,
                                        conflict ? "CURRENT_NET_CONFLICT"
                                                 : "CURRENT_NET_DUPLICATE",
                                        conflict
                                            ? "Conflicting current/activity row for net '" + record.net_name + "'; latest row wins"
                                            : "Duplicate current/activity row for net '" + record.net_name + "'; latest row wins",
                                        line_number,
                                        record.net_name});
            data.records[it->second] = record;
            continue;
        }

        record_by_net[record.net_name] = data.records.size();
        data.records.push_back(std::move(record));
    }

    if (!header_seen) {
        data.diagnostics.push_back({CurrentActivityDiagnostic::Severity::Error,
                                    "CURRENT_CSV_EMPTY",
                                    "Current/activity CSV is empty",
                                    std::nullopt,
                                    std::nullopt});
    }

    return data;
}

CurrentActivityData CurrentActivityParser::parse_csv_file(const std::filesystem::path& path) const
{
    return parse_csv_string(read_text_file(path), path.string());
}

} // namespace aegis::parsing
