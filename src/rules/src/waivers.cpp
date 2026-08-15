#include "aegis/rules/waivers.hpp"

#include "aegis/rules/violation.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace aegis::rules {
namespace {

WaiverDiagnostic make_diag(WaiverDiagnosticSeverity severity,
                           std::string code,
                           std::string message,
                           std::optional<std::size_t> source_line = std::nullopt,
                           std::optional<std::size_t> source_row = std::nullopt)
{
    WaiverDiagnostic d;
    d.severity = severity;
    d.code = std::move(code);
    d.message = std::move(message);
    d.source_line = source_line;
    d.source_row = source_row;
    return d;
}

std::string trim(std::string s)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::optional<std::string> get_optional_string(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    if (j.at(key).is_string()) return j.at(key).get<std::string>();
    return std::nullopt;
}

std::vector<std::string> split_csv_row(const std::string& line)
{
    // Simple RFC4180-ish CSV parser: supports quoted fields with "" escapes.
    std::vector<std::string> fields;
    std::string cur;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur.push_back('"');
                    ++i;
                    continue;
                }
                in_quotes = false;
                continue;
            }
            cur.push_back(c);
            continue;
        }

        if (c == '"') {
            in_quotes = true;
            continue;
        }
        if (c == ',') {
            fields.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    fields.push_back(cur);
    return fields;
}

std::string to_lower_ascii(std::string s)
{
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

WaiverEntry parse_entry_object(const nlohmann::json& obj,
                               std::vector<WaiverDiagnostic>& diags,
                               std::optional<std::size_t> source_line,
                               std::optional<std::size_t> source_row)
{
    WaiverEntry entry;
    entry.identity_key = get_optional_string(obj, "identity_key");
    entry.rule_id = get_optional_string(obj, "rule_id");
    entry.comment = get_optional_string(obj, "comment");
    entry.owner = get_optional_string(obj, "owner");
    entry.expires_on = get_optional_string(obj, "expires_on");

    if (!entry.identity_key.has_value() && !entry.rule_id.has_value()) {
        diags.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                  "WAIVER_MISSING_MATCH_KEYS",
                                  "Waiver entry must include at least one of: identity_key, rule_id",
                                  source_line,
                                  source_row));
    }
    return entry;
}

bool starts_with_non_ws(const std::string& s, char c)
{
    for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
        return ch == c;
    }
    return false;
}

WaiverParseResult parse_yaml_minimal(std::istream& in)
{
    // Minimal YAML subset (deterministic) accepted:
    // - top-level list:
    //     - identity_key: "..."
    //       rule_id: "..."
    // - or:
    //     waivers:
    //       - identity_key: ...
    //         rule_id: ...
    //
    // Values may be unquoted scalars (until end-of-line) or double-quoted strings.
    WaiverParseResult result;

    auto parse_scalar = [](std::string v) -> std::string {
        v = trim(v);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            // Very small subset: no escape processing beyond stripping quotes.
            v = v.substr(1, v.size() - 2);
        }
        return v;
    };

    bool in_waivers_block = false;
    WaiverEntry current;
    bool have_current = false;

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string raw = line;
        line = trim(line);
        if (line.empty()) continue;
        if (line.starts_with('#')) continue;

        if (line == "waivers:" || line == "waiver:" || line == "waivers") {
            in_waivers_block = true;
            continue;
        }

        if (line.starts_with("-")) {
            // Flush previous entry.
            if (have_current) {
                if (!current.identity_key.has_value() && !current.rule_id.has_value()) {
                    result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                           "WAIVER_MISSING_MATCH_KEYS",
                                                           "Waiver entry must include at least one of: identity_key, rule_id",
                                                           line_no));
                } else {
                    result.waivers.push_back(std::move(current));
                }
                current = WaiverEntry{};
            }
            have_current = true;

            // "- key: value" form supported.
            std::string rest = trim(line.substr(1));
            if (rest.empty()) {
                continue;
            }
            const auto colon = rest.find(':');
            if (colon == std::string::npos) {
                result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                                       "WAIVER_YAML_UNSUPPORTED_LINE",
                                                       "Unsupported YAML list item (expected 'key: value')",
                                                       line_no));
                continue;
            }
            const std::string key = to_lower_ascii(trim(rest.substr(0, colon)));
            const std::string value = parse_scalar(rest.substr(colon + 1));

            auto set_field = [&](const std::string& k, const std::string& v) {
                if (k == "identity_key") current.identity_key = v;
                else if (k == "rule_id") current.rule_id = v;
                else if (k == "comment") current.comment = v;
                else if (k == "owner") current.owner = v;
                else if (k == "expires_on") current.expires_on = v;
                else {
                    result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                                           "WAIVER_UNKNOWN_FIELD",
                                                           "Unknown waiver field '" + k + "'",
                                                           line_no));
                }
            };
            set_field(key, value);
            continue;
        }

        // "key: value" form (continuation lines)
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                                   "WAIVER_YAML_UNSUPPORTED_LINE",
                                                   "Unsupported YAML line (expected 'key: value')",
                                                   line_no));
            continue;
        }
        const std::string key = to_lower_ascii(trim(line.substr(0, colon)));
        const std::string value = parse_scalar(line.substr(colon + 1));

        if (!in_waivers_block && key == "waivers") {
            in_waivers_block = true;
            continue;
        }
        if (!have_current) {
            // Key/value without an active list entry.
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                                   "WAIVER_YAML_OUTSIDE_ENTRY",
                                                   "YAML key/value encountered outside a waiver entry",
                                                   line_no));
            continue;
        }

        if (key == "identity_key") current.identity_key = value;
        else if (key == "rule_id") current.rule_id = value;
        else if (key == "comment") current.comment = value;
        else if (key == "owner") current.owner = value;
        else if (key == "expires_on") current.expires_on = value;
        else {
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                                   "WAIVER_UNKNOWN_FIELD",
                                                   "Unknown waiver field '" + key + "'",
                                                   line_no));
        }
    }

    if (have_current) {
        if (!current.identity_key.has_value() && !current.rule_id.has_value()) {
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                   "WAIVER_MISSING_MATCH_KEYS",
                                                   "Waiver entry must include at least one of: identity_key, rule_id"));
        } else {
            result.waivers.push_back(std::move(current));
        }
    }

    return result;
}

} // namespace

bool WaiverParseResult::has_errors() const noexcept
{
    for (const auto& d : diagnostics) {
        if (d.severity == WaiverDiagnosticSeverity::Error) return true;
    }
    return false;
}

WaiverApplicationSummary apply_waivers_in_place(std::vector<aegis::rules::Violation>& violations,
                                               const std::vector<WaiverEntry>& waivers)
{
    WaiverApplicationSummary summary;
    summary.total_violations = violations.size();

    // Deterministic matching:
    // - If waiver specifies identity_key, it matches that key (and also rule_id if provided).
    // - If waiver specifies only rule_id, it matches any violation with that rule_id.
    // - If multiple waivers match, the earliest in input order wins.
    struct IndexedWaiver {
        const WaiverEntry* entry = nullptr;
        std::size_t order = 0;
        bool has_identity = false;
    };

    std::unordered_map<std::string, std::vector<IndexedWaiver>> by_identity;
    std::unordered_map<std::string, std::vector<IndexedWaiver>> by_rule;

    for (std::size_t i = 0; i < waivers.size(); ++i) {
        const auto& w = waivers[i];
        IndexedWaiver iw{&w, i, w.identity_key.has_value()};
        if (w.identity_key.has_value()) {
            by_identity[*w.identity_key].push_back(iw);
        }
        if (w.rule_id.has_value() && !w.rule_id->empty()) {
            by_rule[*w.rule_id].push_back(iw);
        }
    }

    auto attach_waiver_metadata = [](aegis::rules::Violation& v,
                                    const WaiverEntry& w,
                                    std::string_view match_kind) {
        auto m = v.metadata.with("waived", true).with("waiver_match", std::string(match_kind));
        if (w.identity_key.has_value()) m = m.with("waiver_identity_key", *w.identity_key);
        if (w.rule_id.has_value()) m = m.with("waiver_rule_id", *w.rule_id);
        if (w.comment.has_value()) m = m.with("waiver_comment", *w.comment);
        if (w.owner.has_value()) m = m.with("waiver_owner", *w.owner);
        if (w.expires_on.has_value()) m = m.with("waiver_expires_on", *w.expires_on);
        v.metadata = std::move(m);
    };

    for (auto& v : violations) {
        bool waived = false;
        const std::string key = v.identity_key();

        // Identity-key match first.
        auto it_id = by_identity.find(key);
        if (it_id != by_identity.end()) {
            for (const auto& candidate : it_id->second) {
                const auto& w = *candidate.entry;
                if (w.rule_id.has_value() && w.rule_id.value() != v.rule_id) {
                    continue;
                }
                attach_waiver_metadata(v, w, "identity_key");
                waived = true;
                break;
            }
        }

        // Rule-only match.
        if (!waived) {
            auto it_rule = by_rule.find(v.rule_id);
            if (it_rule != by_rule.end()) {
                for (const auto& candidate : it_rule->second) {
                    const auto& w = *candidate.entry;
                    if (w.identity_key.has_value()) {
                        continue; // identity waivers are handled above
                    }
                    attach_waiver_metadata(v, w, "rule_id");
                    waived = true;
                    break;
                }
            }
        }

        if (waived) {
            ++summary.waived_violations;
        } else {
            v.metadata = v.metadata.with("waived", false);
        }
    }

    summary.active_violations = summary.total_violations - summary.waived_violations;
    return summary;
}

WaiverParseResult parse_waivers_csv_file(const std::filesystem::path& path)
{
    WaiverParseResult result;
    std::ifstream f(path);
    if (!f) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                               "WAIVER_OPEN_FAILED",
                                               "Failed to open waiver CSV file: '" + path.generic_string() + "'"));
        return result;
    }

    std::string header_line;
    if (!std::getline(f, header_line)) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                               "WAIVER_EMPTY_FILE",
                                               "Waiver CSV file is empty: '" + path.generic_string() + "'"));
        return result;
    }

    const auto header_fields = split_csv_row(header_line);
    std::unordered_map<std::string, std::size_t> col_index;
    for (std::size_t i = 0; i < header_fields.size(); ++i) {
        col_index[to_lower_ascii(trim(header_fields[i]))] = i;
    }

    auto col = [&](std::string_view name) -> std::optional<std::size_t> {
        auto it = col_index.find(std::string{name});
        if (it == col_index.end()) return std::nullopt;
        return it->second;
    };

    const auto identity_col = col("identity_key");
    const auto rule_col = col("rule_id");
    const auto comment_col = col("comment");
    const auto owner_col = col("owner");
    const auto expires_col = col("expires_on");

    if (!identity_col.has_value() && !rule_col.has_value()) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                               "WAIVER_CSV_MISSING_COLUMNS",
                                               "Waiver CSV must include at least one of: identity_key, rule_id",
                                               1));
        return result;
    }

    // Unknown columns: warnings (deterministic order by appearance).
    for (std::size_t i = 0; i < header_fields.size(); ++i) {
        const std::string name = to_lower_ascii(trim(header_fields[i]));
        if (name.empty()) continue;
        if (name == "identity_key" || name == "rule_id" || name == "comment" || name == "owner" || name == "expires_on") {
            continue;
        }
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Warning,
                                               "WAIVER_UNKNOWN_COLUMN",
                                               "Unknown waiver CSV column '" + name + "'",
                                               1));
    }

    std::string row;
    std::size_t row_no = 1; // header is 1
    while (std::getline(f, row)) {
        ++row_no;
        if (trim(row).empty()) continue;
        const auto fields = split_csv_row(row);

        auto get_field = [&](std::optional<std::size_t> idx) -> std::optional<std::string> {
            if (!idx.has_value()) return std::nullopt;
            if (*idx >= fields.size()) return std::nullopt;
            const std::string v = trim(fields[*idx]);
            if (v.empty()) return std::nullopt;
            return v;
        };

        WaiverEntry entry;
        entry.identity_key = get_field(identity_col);
        entry.rule_id = get_field(rule_col);
        entry.comment = get_field(comment_col);
        entry.owner = get_field(owner_col);
        entry.expires_on = get_field(expires_col);

        if (!entry.identity_key.has_value() && !entry.rule_id.has_value()) {
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                   "WAIVER_MISSING_MATCH_KEYS",
                                                   "Waiver entry must include at least one of: identity_key, rule_id",
                                                   std::nullopt,
                                                   row_no - 1));
            continue;
        }
        result.waivers.push_back(std::move(entry));
    }

    return result;
}

WaiverParseResult parse_waivers_json_file(const std::filesystem::path& path)
{
    WaiverParseResult result;
    std::ifstream f(path);
    if (!f) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                               "WAIVER_OPEN_FAILED",
                                               "Failed to open waiver JSON file: '" + path.generic_string() + "'"));
        return result;
    }

    try {
        const auto j = nlohmann::json::parse(f, nullptr, true, true);
        nlohmann::json waivers = j;
        if (j.is_object() && j.contains("waivers")) {
            waivers = j.at("waivers");
        }
        if (!waivers.is_array()) {
            result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                   "WAIVER_JSON_BAD_FORMAT",
                                                   "Waiver JSON must be an array or an object containing 'waivers' array"));
            return result;
        }

        for (std::size_t i = 0; i < waivers.size(); ++i) {
            if (!waivers[i].is_object()) {
                result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                       "WAIVER_JSON_BAD_ENTRY",
                                                       "Waiver entry must be an object",
                                                       std::nullopt,
                                                       i + 1));
                continue;
            }
            result.waivers.push_back(parse_entry_object(waivers[i], result.diagnostics, std::nullopt, i + 1));
        }
    } catch (const std::exception& ex) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                               "WAIVER_JSON_PARSE_FAILED",
                                               std::string("Failed to parse waiver JSON: ") + ex.what()));
    }

    // Drop entries that were missing match keys (errors already recorded).
    result.waivers.erase(std::remove_if(result.waivers.begin(), result.waivers.end(),
                                        [](const WaiverEntry& e) {
                                            return !e.identity_key.has_value() && !e.rule_id.has_value();
                                        }),
                         result.waivers.end());
    return result;
}

WaiverParseResult parse_waivers_yaml_file(const std::filesystem::path& path)
{
    WaiverParseResult result;
    std::ifstream f(path);
    if (!f) {
        result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                               "WAIVER_OPEN_FAILED",
                                               "Failed to open waiver YAML file: '" + path.generic_string() + "'"));
        return result;
    }

    // YAML is a superset of JSON; support JSON-in-YAML deterministically.
    std::stringstream buffer;
    buffer << f.rdbuf();
    const std::string content = buffer.str();

    if (starts_with_non_ws(content, '[') || starts_with_non_ws(content, '{')) {
        std::istringstream in(content);
        try {
            const auto j = nlohmann::json::parse(in, nullptr, true, true);
            // Delegate JSON logic.
            // NOTE: parse_waivers_json_file reads from disk; reuse common logic inline to keep determinism.
            nlohmann::json waivers = j;
            if (j.is_object() && j.contains("waivers")) {
                waivers = j.at("waivers");
            }
            if (!waivers.is_array()) {
                result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                       "WAIVER_YAML_BAD_FORMAT",
                                                       "Waiver YAML (JSON form) must be an array or an object containing 'waivers' array"));
                return result;
            }
            for (std::size_t i = 0; i < waivers.size(); ++i) {
                if (!waivers[i].is_object()) {
                    result.diagnostics.push_back(make_diag(WaiverDiagnosticSeverity::Error,
                                                           "WAIVER_YAML_BAD_ENTRY",
                                                           "Waiver entry must be an object",
                                                           std::nullopt,
                                                           i + 1));
                    continue;
                }
                result.waivers.push_back(parse_entry_object(waivers[i], result.diagnostics, std::nullopt, i + 1));
            }
            result.waivers.erase(std::remove_if(result.waivers.begin(), result.waivers.end(),
                                                [](const WaiverEntry& e) {
                                                    return !e.identity_key.has_value() && !e.rule_id.has_value();
                                                }),
                                 result.waivers.end());
            return result;
        } catch (const std::exception&) {
            // Fall through to minimal YAML parsing.
        }
    }

    std::istringstream in(content);
    result = parse_yaml_minimal(in);
    return result;
}

} // namespace aegis::rules
