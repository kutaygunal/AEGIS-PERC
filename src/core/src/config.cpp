#include "aegis/core/config.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace aegis::core {

// ---------------------------------------------------------------------------
// ConfigValue helpers
// ---------------------------------------------------------------------------
bool ConfigValue::is_scalar() const noexcept {
    return std::holds_alternative<bool>(data) ||
           std::holds_alternative<std::int64_t>(data) ||
           std::holds_alternative<double>(data) ||
           std::holds_alternative<std::string>(data);
}

bool ConfigValue::is_array() const noexcept {
    return std::holds_alternative<ConfigArray>(data);
}

bool ConfigValue::is_object() const noexcept {
    return std::holds_alternative<ConfigObject>(data);
}

std::string ConfigValue::to_string() const {
    if (auto* p = std::get_if<bool>(&data)) return *p ? "true" : "false";
    if (auto* p = std::get_if<std::int64_t>(&data)) return std::to_string(*p);
    if (auto* p = std::get_if<double>(&data)) return std::to_string(*p);
    if (auto* p = std::get_if<std::string>(&data)) return *p;
    if (auto* p = std::get_if<ConfigArray>(&data)) return "[array]";
    if (auto* p = std::get_if<ConfigObject>(&data)) return "{object}";
    return "null";
}

// ---------------------------------------------------------------------------
// ConfigValidationException
// ---------------------------------------------------------------------------
ConfigValidationException::ConfigValidationException(const std::string& message)
    : std::runtime_error(message) {}

// ---------------------------------------------------------------------------
// Helpers: key splitting and flattening
// ---------------------------------------------------------------------------
std::vector<std::string> Config::split_key(const std::string& key) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= key.size(); ++i) {
        if (i == key.size() || key[i] == '.') {
            if (i > start) {
                parts.emplace_back(key.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return parts;
}

// Flatten a nested JSON object into dot-separated keys.
static void flatten_json(const nlohmann::json& j,
                         const std::string& prefix,
                         std::unordered_map<std::string, ConfigValue>& out) {
    if (j.is_null()) return;

    if (j.is_boolean()) {
        out[prefix] = j.get<bool>();
    } else if (j.is_number_integer()) {
        out[prefix] = j.get<std::int64_t>();
    } else if (j.is_number_float()) {
        out[prefix] = j.get<double>();
    } else if (j.is_string()) {
        out[prefix] = j.get<std::string>();
    } else if (j.is_array()) {
        ConfigArray arr;
        for (const auto& el : j) {
            if (el.is_boolean())        arr.emplace_back(el.get<bool>());
            else if (el.is_number_integer()) arr.emplace_back(el.get<std::int64_t>());
            else if (el.is_number_float())   arr.emplace_back(el.get<double>());
            else if (el.is_string())         arr.emplace_back(el.get<std::string>());
        }
        out[prefix] = std::move(arr);
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            auto child_prefix = prefix.empty() ? it.key() : prefix + "." + it.key();
            flatten_json(it.value(), child_prefix, out);
        }
    }
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
bool Config::load_from_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    std::ifstream f(path);
    if (!f) return false;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return false;
    }

    flatten_json(j, "", m_flat);
    return true;
}

bool Config::load_from_string(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        flatten_json(j, "", m_flat);
        return true;
    } catch (...) {
        return false;
    }
}

void Config::load_from_environment() {
    // Scan known config keys (defaults + flat) and check for AEGIS_* env vars.
    std::unordered_set<std::string> candidates;
    for (const auto& [k, _] : m_defaults) candidates.insert(k);
    for (const auto& [k, _] : m_flat)    candidates.insert(k);

    for (const auto& key : candidates) {
        std::string env_key = "AEGIS_";
        for (char c : key) {
            env_key += (c == '.') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
        if (const char* val = std::getenv(env_key.c_str())) {
#ifdef _MSC_VER
#pragma warning(pop)
#endif
            auto it = m_defaults.find(key);
            if (it != m_defaults.end() && it->second.is_scalar()) {
                if (std::get_if<bool>(&it->second.data)) {
                    std::string vlower;
                    for (char c : std::string(val)) vlower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    set(key, vlower == "true" || vlower == "1" || vlower == "yes" || vlower == "on");
                } else if (std::get_if<std::int64_t>(&it->second.data)) {
                    std::int64_t iv = 0;
                    std::from_chars(val, val + std::strlen(val), iv);
                    set(key, iv);
                } else if (std::get_if<double>(&it->second.data)) {
                    set(key, std::stod(val));
                } else {
                    set(key, ConfigValue(std::string(val)));
                }
            } else {
                set(key, ConfigValue(std::string(val)));
            }
        }
    }
}

void Config::set(const std::string& key, ConfigValue value) {
    m_flat[key] = std::move(value);
    notify(key, m_flat[key]);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
void Config::validate(const ConfigSchema& schema) const {
    for (const auto& [key, field] : schema) {
        auto it = m_flat.find(key);
        if (it == m_flat.end()) {
            if (field.required) {
                throw ConfigValidationException("Missing required config key: " + key);
            }
            continue;
        }

        const auto& val = it->second;
        bool type_ok = false;
        switch (field.type) {
            case ConfigType::Boolean: type_ok = std::holds_alternative<bool>(val.data); break;
            case ConfigType::Integer: type_ok = std::holds_alternative<std::int64_t>(val.data); break;
            case ConfigType::Double:  type_ok = std::holds_alternative<double>(val.data); break;
            case ConfigType::String:  type_ok = std::holds_alternative<std::string>(val.data); break;
            case ConfigType::Array:   type_ok = std::holds_alternative<ConfigArray>(val.data); break;
            case ConfigType::Object:  type_ok = std::holds_alternative<ConfigObject>(val.data); break;
            case ConfigType::Any:     type_ok = true; break;
        }
        if (!type_ok) {
            throw ConfigValidationException("Type mismatch for config key: " + key);
        }
    }
    // Reject unknown keys
    for (const auto& [key, _] : m_flat) {
        if (schema.find(key) == schema.end()) {
            throw ConfigValidationException("Unknown config key: " + key);
        }
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::optional<ConfigValue> Config::get(const std::string& key) const {
    auto it = m_flat.find(key);
    if (it != m_flat.end()) return it->second;
    auto dit = m_defaults.find(key);
    if (dit != m_defaults.end()) return dit->second;
    return std::nullopt;
}

ConfigValue Config::get_or(const std::string& key, ConfigValue default_value) const {
    auto v = get(key);
    return v ? *v : std::move(default_value);
}

bool Config::get_bool(const std::string& key, bool default_v) const {
    auto v = get(key);
    if (!v) return default_v;
    if (auto* p = std::get_if<bool>(&v->data)) return *p;
    return default_v;
}

std::int64_t Config::get_int(const std::string& key, std::int64_t default_v) const {
    auto v = get(key);
    if (!v) return default_v;
    if (auto* p = std::get_if<std::int64_t>(&v->data)) return *p;
    return default_v;
}

double Config::get_double(const std::string& key, double default_v) const {
    auto v = get(key);
    if (!v) return default_v;
    if (auto* p = std::get_if<double>(&v->data)) return *p;
    return default_v;
}

std::string Config::get_string(const std::string& key, std::string default_v) const {
    auto v = get(key);
    if (!v) return default_v;
    if (auto* p = std::get_if<std::string>(&v->data)) return *p;
    return default_v;
}

bool Config::has(const std::string& key) const {
    return m_flat.count(key) > 0 || m_defaults.count(key) > 0;
}

// ---------------------------------------------------------------------------
// Observability
// ---------------------------------------------------------------------------
std::size_t Config::on_change(ChangeCallback cb) {
    auto handle = m_next_handle++;
    m_callbacks.emplace_back(handle, std::move(cb));
    return handle;
}

void Config::unsubscribe(std::size_t handle) {
    m_callbacks.erase(
        std::remove_if(m_callbacks.begin(), m_callbacks.end(),
            [handle](const auto& p) { return p.first == handle; }),
        m_callbacks.end());
}

void Config::notify(const std::string& key, const ConfigValue& value) {
    for (auto& [_, cb] : m_callbacks) {
        if (cb) cb(key, value);
    }
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------
void Config::dump(std::ostream& os) const {
    for (const auto& [k, v] : m_flat) {
        os << k << " = " << v.to_string() << "\n";
    }
}

std::string Config::to_json_string(int indent) const {
    nlohmann::json j;
    for (const auto& [k, v] : m_flat) {
        auto parts = split_key(k);
        nlohmann::json* target = &j;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!target->contains(parts[i])) {
                (*target)[parts[i]] = nlohmann::json::object();
            }
            target = &(*target)[parts[i]];
        }
        if (parts.empty()) continue;
        auto& leaf = (*target)[parts.back()];
        if (auto* pb = std::get_if<bool>(&v.data)) leaf = *pb;
        else if (auto* pi = std::get_if<std::int64_t>(&v.data)) leaf = *pi;
        else if (auto* pd = std::get_if<double>(&v.data)) leaf = *pd;
        else if (auto* ps = std::get_if<std::string>(&v.data)) leaf = *ps;
        else if (auto* pa = std::get_if<ConfigArray>(&v.data)) {
            leaf = nlohmann::json::array();
            for (const auto& el : *pa) {
                leaf.push_back(el.to_string());
            }
        }
        else if (auto* po = std::get_if<ConfigObject>(&v.data)) {
            leaf = nlohmann::json::object();
            for (const auto& [ek, ev] : *po) {
                leaf[ek] = ev.to_string();
            }
        }
    }
    return j.dump(indent);
}

// ---------------------------------------------------------------------------
// Default factory
// ---------------------------------------------------------------------------
Config Config::make_with_defaults(const ConfigSchema& schema) {
    Config cfg;
    for (const auto& [key, field] : schema) {
        cfg.m_defaults[key] = field.default_value;
        if (field.required && cfg.m_flat.find(key) == cfg.m_flat.end()) {
            // ensure flat also has it if we want to expose immediately
            // not strictly necessary; get() falls back to defaults
        }
    }
    return cfg;
}

} // namespace aegis::core
