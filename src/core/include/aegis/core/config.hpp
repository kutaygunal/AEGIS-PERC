#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Config value types
// ---------------------------------------------------------------------------
using ConfigScalar = std::variant<bool, std::int64_t, double, std::string>;

struct ConfigValue;
using ConfigArray  = std::vector<ConfigValue>;
using ConfigObject = std::unordered_map<std::string, ConfigValue>;

struct ConfigValue {
    using Storage = std::variant<bool, std::int64_t, double, std::string, ConfigArray, ConfigObject>;
    Storage data;

    ConfigValue() = default;
    ConfigValue(bool v) : data(v) {}
    ConfigValue(std::int64_t v) : data(v) {}
    ConfigValue(int v) : data(static_cast<std::int64_t>(v)) {}
    ConfigValue(double v) : data(v) {}
    ConfigValue(std::string v) : data(std::move(v)) {}
    ConfigValue(const char* v) : data(std::string(v)) {}
    ConfigValue(ConfigArray v) : data(std::move(v)) {}
    ConfigValue(ConfigObject v) : data(std::move(v)) {}

    template<typename T>
    const T* as() const {
        return std::get_if<T>(&data);
    }

    template<typename T>
    T* as() {
        return std::get_if<T>(&data);
    }

    bool is_scalar() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;
    std::string to_string() const;
};

// ---------------------------------------------------------------------------
// Schema field descriptor
// ---------------------------------------------------------------------------
enum class ConfigType {
    Boolean,
    Integer,
    Double,
    String,
    Array,
    Object,
    Any
};

struct ConfigSchemaField {
    ConfigType type = ConfigType::Any;
    ConfigValue default_value;
    bool required = false;
    std::string description;
};

using ConfigSchema = std::map<std::string, ConfigSchemaField>;

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
class Config {
public:
    // Loading ---------------------------------------------------------------
    // Load from JSON file. Returns false if file missing (non-fatal; defaults used).
    bool load_from_file(const std::filesystem::path& path);

    // Parse JSON string directly (for tests / embedded defaults).
    bool load_from_string(const std::string& json);

    // Override from environment variables with AEGIS_ prefix.
    // Key "core.logging.level" matches env var AEGIS_CORE_LOGGING_LEVEL.
    void load_from_environment();

    // Programmatic set (for CLI or programmatic changes).
    void set(const std::string& key, ConfigValue value);

    // Validate against schema. Throws ConfigValidationException on mismatch.
    void validate(const ConfigSchema& schema) const;

    // Typed accessors -------------------------------------------------------
    // Get a value by dot-separated key. Returns nullopt if not found.
    std::optional<ConfigValue> get(const std::string& key) const;

    // Get with default fallback.
    ConfigValue get_or(const std::string& key, ConfigValue default_value) const;

    // Convenience typed getters with default fallback.
    bool        get_bool  (const std::string& key, bool        default_v) const;
    std::int64_t get_int   (const std::string& key, std::int64_t default_v) const;
    double      get_double(const std::string& key, double      default_v) const;
    std::string get_string(const std::string& key, std::string default_v) const;

    // Check existence
    bool has(const std::string& key) const;

    // Observability ---------------------------------------------------------
    using ChangeCallback = std::function<void(const std::string& key, const ConfigValue& value)>;
    // Register a callback invoked whenever a key changes. Returns handle for unsubscription.
    std::size_t on_change(ChangeCallback cb);
    void unsubscribe(std::size_t handle);

    // Inspection ------------------------------------------------------------
    void dump(std::ostream& os) const;
    std::string to_json_string(int indent = 2) const;

    // Default factory -------------------------------------------------------
    static Config make_with_defaults(const ConfigSchema& schema);

private:
    std::unordered_map<std::string, ConfigValue> m_flat;
    std::unordered_map<std::string, ConfigValue> m_defaults;
    std::vector<std::pair<std::size_t, ChangeCallback>> m_callbacks;
    std::size_t m_next_handle = 0;

    void notify(const std::string& key, const ConfigValue& value);
    static std::vector<std::string> split_key(const std::string& key);
};

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
class ConfigValidationException : public std::runtime_error {
public:
    explicit ConfigValidationException(const std::string& message);
};

} // namespace aegis::core
