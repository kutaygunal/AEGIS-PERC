#include "aegis/graph/data_model.hpp"

#include <cstdint>

namespace aegis::graph {

// ---------------------------------------------------------------------------
// PropertyMap
// ---------------------------------------------------------------------------

bool PropertyMap::has(const std::string& key) const {
    return m_entries.count(key) != 0;
}

std::optional<PropertyValue> PropertyMap::raw(const std::string& key) const {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return std::nullopt;
    return it->second;
}

PropertyMap PropertyMap::with(const std::string& key,
                                PropertyValue value) const {
    PropertyMap copy = *this;
    copy.m_entries[key] = std::move(value);
    return copy;
}

PropertyMap PropertyMap::without(const std::string& key) const {
    PropertyMap copy = *this;
    copy.m_entries.erase(key);
    return copy;
}

PropertyMap PropertyMap::from_string_map(
    const std::map<std::string, std::string>& src) {
    PropertyMap pmap;
    for (const auto& [k, v] : src) {
        pmap.m_entries[k] = v; // string variant
    }
    return pmap;
}

bool PropertyMap::operator==(const PropertyMap& other) const noexcept {
    return m_entries == other.m_entries;
}

bool PropertyMap::operator!=(const PropertyMap& other) const noexcept {
    return !(*this == other);
}

// ---------------------------------------------------------------------------
// Direction
// ---------------------------------------------------------------------------

Direction direction_from_string(const std::string& s) {
    if (s == "INPUT"  || s == "input")  return Direction::Input;
    if (s == "OUTPUT" || s == "output") return Direction::Output;
    if (s == "INOUT"  || s == "inout")  return Direction::InOut;
    return Direction::BiDir;
}

std::string direction_to_string(Direction d) {
    switch (d) {
        case Direction::Input:  return "INPUT";
        case Direction::Output: return "OUTPUT";
        case Direction::InOut:  return "INOUT";
        default:                return "BIDIR";
    }
}

// ---------------------------------------------------------------------------
// DeviceModel builders
// ---------------------------------------------------------------------------

DeviceModel DeviceModel::with_width(double w) const {
    DeviceModel copy = *this;
    copy.properties = copy.properties.with("w", w);
    return copy;
}

DeviceModel DeviceModel::with_length(double l) const {
    DeviceModel copy = *this;
    copy.properties = copy.properties.with("l", l);
    return copy;
}

DeviceModel DeviceModel::with_model(std::string m) const {
    DeviceModel copy = *this;
    copy.properties = copy.properties.with("model", std::move(m));
    return copy;
}

// ---------------------------------------------------------------------------
// Hashing helpers
// ---------------------------------------------------------------------------

namespace {

inline void hash_combine(std::size_t& seed, std::size_t hash) noexcept {
    seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct PropertyValueHasher {
    std::size_t operator()(const PropertyValue& v) const {
        struct Visitor {
            std::size_t operator()(const std::string& s) const {
                return std::hash<std::string>{}(s);
            }
            std::size_t operator()(int i) const {
                return std::hash<int>{}(i);
            }
            std::size_t operator()(double d) const {
                return std::hash<double>{}(d);
            }
            std::size_t operator()(bool b) const {
                return std::hash<bool>{}(b);
            }
        };
        std::size_t seed = std::hash<std::size_t>{}(v.index());
        hash_combine(seed, std::visit(Visitor{}, v));
        return seed;
    }
};

} // anonymous namespace

std::size_t hash_value(const PropertyMap& pmap) {
    std::size_t seed = 0;
    for (const auto& [k, v] : pmap) {
        hash_combine(seed, std::hash<std::string>{}(k));
        hash_combine(seed, PropertyValueHasher{}(v));
    }
    return seed;
}

std::size_t hash_value(const PinModel& pin) {
    std::size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(pin.name));
    hash_combine(seed, std::hash<int>{}(static_cast<int>(pin.direction)));
    hash_combine(seed, std::hash<std::string>{}(pin.net_name));
    if (pin.layer) hash_combine(seed, std::hash<std::string>{}(*pin.layer));
    hash_combine(seed, hash_value(pin.properties));
    return seed;
}

std::size_t hash_value(const NetModel& net) {
    std::size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(net.name));
    for (const auto& p : net.pin_names) {
        hash_combine(seed, std::hash<std::string>{}(p));
    }
    hash_combine(seed, hash_value(net.properties));
    return seed;
}

std::size_t hash_value(const DeviceModel& dev) {
    std::size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(dev.name));
    hash_combine(seed, std::hash<std::string>{}(dev.device_type));
    for (const auto& [k, v] : dev.pins) {
        hash_combine(seed, std::hash<std::string>{}(k));
        hash_combine(seed, std::hash<std::string>{}(v));
    }
    hash_combine(seed, hash_value(dev.properties));
    return seed;
}

std::size_t hash_value(const PortModel& port) {
    std::size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(port.name));
    hash_combine(seed, std::hash<int>{}(static_cast<int>(port.direction)));
    hash_combine(seed, std::hash<std::string>{}(port.net_name));
    if (port.layer) hash_combine(seed, std::hash<std::string>{}(*port.layer));
    hash_combine(seed, hash_value(port.properties));
    return seed;
}

} // namespace aegis::graph
