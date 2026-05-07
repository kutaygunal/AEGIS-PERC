#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aegis::graph {

// ---------------------------------------------------------------------------
// Property type system
// ---------------------------------------------------------------------------

/**
 * A typed property value.
 *
 * Supports string, integer, floating-point, and boolean attribute values.
 * The variant preserves the original type; coercion happens at accessor time.
 */
using PropertyValue = std::variant<std::string, int, double, bool>;

/**
 * A property map with strongly typed read access.
 *
 * Value-based and copy-friendly.  Mutations go through builder methods
 * that return new copies, preserving caller-side immutability.
 */
class PropertyMap {
public:
    PropertyMap() = default;
    PropertyMap(const PropertyMap&) = default;
    PropertyMap& operator=(const PropertyMap&) = default;
    PropertyMap(PropertyMap&&) = default;
    PropertyMap& operator=(PropertyMap&&) = default;
    ~PropertyMap() = default;

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] bool has(const std::string& key) const;
    [[nodiscard]] std::optional<PropertyValue> raw(const std::string& key) const;

    /**
     * Get a property with automatic type coercion.
     *
     * For numeric target types:
     *   - string values are parsed (stod / stoi)
     *   - bool is cast to 1.0 / 0 or 1 / 0
     *   - int ⇄ double are cast
     *
     * For string target type:
     *   - all scalar types are converted via to_string / (bool ? "true" : "false")
     *
     * Returns std::nullopt if the key is absent or coercion fails.
     */
    template<typename T>
    [[nodiscard]] std::optional<T> get(const std::string& key) const {
        auto v = raw(key);
        if (!v) return std::nullopt;

        if constexpr (std::is_same_v<T, std::string>) {
            if (auto* p = std::get_if<std::string>(&*v)) return *p;
            if (auto* i = std::get_if<int>(&*v))       return std::to_string(*i);
            if (auto* d = std::get_if<double>(&*v))    return std::to_string(*d);
            return std::get<bool>(*v) ? "true" : "false";
        }
        if constexpr (std::is_same_v<T, double>) {
            if (auto* d = std::get_if<double>(&*v))    return *d;
            if (auto* i = std::get_if<int>(&*v))       return static_cast<double>(*i);
            if (auto* b = std::get_if<bool>(&*v))      return *b ? 1.0 : 0.0;
            try { return std::stod(std::get<std::string>(*v)); }
            catch (...) { return std::nullopt; }
        }
        if constexpr (std::is_same_v<T, int>) {
            if (auto* i = std::get_if<int>(&*v))       return *i;
            if (auto* d = std::get_if<double>(&*v))    return static_cast<int>(*d);
            if (auto* b = std::get_if<bool>(&*v))      return *b ? 1 : 0;
            try { return std::stoi(std::get<std::string>(*v)); }
            catch (...) { return std::nullopt; }
        }
        if constexpr (std::is_same_v<T, bool>) {
            if (auto* b = std::get_if<bool>(&*v))      return *b;
            if (auto* i = std::get_if<int>(&*v))       return *i != 0;
            if (auto* d = std::get_if<double>(&*v))    return *d != 0.0;
            const auto& s = std::get<std::string>(*v);
            return s == "true" || s == "1" || s == "yes" || s == "on";
        }
        return std::nullopt;
    }

    /**
     * Return a new PropertyMap with the given key set (or overwritten).
     */
    [[nodiscard]] PropertyMap with(const std::string& key,
                                     PropertyValue value) const;

    /**
     * Return a new PropertyMap with the given key removed.
     */
    [[nodiscard]] PropertyMap without(const std::string& key) const;

    /**
     * Build a PropertyMap from a legacy string-only map (e.g. from LayoutIR).
     *
     * This is a lossy conversion — all values become string variants.
     */
    [[nodiscard]] static PropertyMap from_string_map(
        const std::map<std::string, std::string>& src);

    /**
     * Equality — values must match exactly (including string vs int).
     */
    [[nodiscard]] bool operator==(const PropertyMap& other) const noexcept;
    [[nodiscard]] bool operator!=(const PropertyMap& other) const noexcept;

    [[nodiscard]] auto begin() const { return m_entries.begin(); }
    [[nodiscard]] auto end()   const { return m_entries.end(); }

    friend std::size_t hash_value(const PropertyMap& pmap);

private:
    std::map<std::string, PropertyValue> m_entries;
};

// ---------------------------------------------------------------------------
// Direction
// ---------------------------------------------------------------------------

enum class Direction { Input, Output, InOut, BiDir };

[[nodiscard]] Direction direction_from_string(const std::string& s);
[[nodiscard]] std::string direction_to_string(Direction d);

// ---------------------------------------------------------------------------
// Geometric point (local, self-contained)
// ---------------------------------------------------------------------------

struct Point {
    double x = 0.0;
    double y = 0.0;
    [[nodiscard]] bool operator==(const Point& o) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Strongly typed design models
// ---------------------------------------------------------------------------

/**
 * A terminal pin on a device or external IO port.
 *
 * Value-based: mutating any field produces a new copy.
 */
struct PinModel {
    std::string              name;
    Direction                direction = Direction::BiDir;
    std::string              net_name;
    std::optional<std::string> layer;
    std::optional<Point>        location;
    PropertyMap              properties;

    [[nodiscard]] bool operator==(const PinModel& o) const noexcept = default;
    [[nodiscard]] bool operator!=(const PinModel& o) const noexcept = default;
};

/**
 * An electrical net (wire).
 */
struct NetModel {
    std::string                   name;
    std::vector<std::string>    pin_names;
    PropertyMap                   properties;

    [[nodiscard]] bool operator==(const NetModel& o) const noexcept = default;
    [[nodiscard]] bool operator!=(const NetModel& o) const noexcept = default;
};

/**
 * A device / component instance (transistor, resistor, capacitor, etc).
 */
struct DeviceModel {
    std::string                    name;
    std::string                    device_type;
    std::map<std::string, std::string> pins; // pin_name → net_name
    PropertyMap                    properties;

    // --- convenience typed accessors ---
    [[nodiscard]] std::optional<double>      width()  const { return properties.get<double>("w"); }
    [[nodiscard]] std::optional<double>      length() const { return properties.get<double>("l"); }
    [[nodiscard]] std::optional<std::string> model()  const { return properties.get<std::string>("model"); }

    // --- builders ---
    [[nodiscard]] DeviceModel with_width(double w)  const;
    [[nodiscard]] DeviceModel with_length(double l) const;
    [[nodiscard]] DeviceModel with_model(std::string m) const;

    [[nodiscard]] bool operator==(const DeviceModel& o) const noexcept = default;
    [[nodiscard]] bool operator!=(const DeviceModel& o) const noexcept = default;
};

/**
 * An external port / pad.
 */
struct PortModel {
    std::string              name;
    Direction                direction = Direction::BiDir;
    std::string              net_name;
    std::optional<std::string> layer;
    std::optional<Point>        location;
    PropertyMap              properties;

    [[nodiscard]] bool operator==(const PortModel& o) const noexcept = default;
    [[nodiscard]] bool operator!=(const PortModel& o) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

[[nodiscard]] std::size_t hash_value(const PropertyMap& pmap);
[[nodiscard]] std::size_t hash_value(const PinModel& pin);
[[nodiscard]] std::size_t hash_value(const NetModel& net);
[[nodiscard]] std::size_t hash_value(const DeviceModel& dev);
[[nodiscard]] std::size_t hash_value(const PortModel& port);

} // namespace aegis::graph

// ---------------------------------------------------------------------------
// std::hash specialisations for use in unordered containers
// ---------------------------------------------------------------------------

template<>
struct std::hash<aegis::graph::PropertyMap> {
    std::size_t operator()(const aegis::graph::PropertyMap& p) const {
        return aegis::graph::hash_value(p);
    }
};

template<>
struct std::hash<aegis::graph::DeviceModel> {
    std::size_t operator()(const aegis::graph::DeviceModel& d) const {
        return aegis::graph::hash_value(d);
    }
};

template<>
struct std::hash<aegis::graph::NetModel> {
    std::size_t operator()(const aegis::graph::NetModel& n) const {
        return aegis::graph::hash_value(n);
    }
};

template<>
struct std::hash<aegis::graph::PinModel> {
    std::size_t operator()(const aegis::graph::PinModel& p) const {
        return aegis::graph::hash_value(p);
    }
};

template<>
struct std::hash<aegis::graph::PortModel> {
    std::size_t operator()(const aegis::graph::PortModel& p) const {
        return aegis::graph::hash_value(p);
    }
};
