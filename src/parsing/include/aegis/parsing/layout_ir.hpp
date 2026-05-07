#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace aegis::parsing {

// ---------------------------------------------------------------------------
// Geometric primitives
// ---------------------------------------------------------------------------

struct Point {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const Point& other) const noexcept = default;
};

struct Rectangle {
    double x      = 0.0;
    double y      = 0.0;
    double width  = 0.0;
    double height = 0.0;

    bool operator==(const Rectangle& other) const noexcept = default;
};

struct Polygon {
    std::vector<Point> points;

    bool operator==(const Polygon& other) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Shape variant — serialises via a "type" discriminator
// ---------------------------------------------------------------------------

using Shape = std::variant<Rectangle, Polygon>;

// ---------------------------------------------------------------------------
// Layout building blocks
// ---------------------------------------------------------------------------

struct Layer {
    std::string name;
    std::string purpose;
    int         order = 0;
    std::string color;     // e.g. "#FF0000"

    bool operator==(const Layer& other) const noexcept = default;
};

struct Net {
    std::string                    name;
    std::vector<std::string>      pin_names;
    std::map<std::string, std::string> properties;

    bool operator==(const Net& other) const noexcept = default;
};

struct Pin {
    std::string name;
    std::string net_name;
    std::string direction; // "INPUT", "OUTPUT", "INOUT"

    bool operator==(const Pin& other) const noexcept = default;
};

struct Device {
    std::string                    name;
    std::string                    type;
    std::map<std::string, std::string> pins;        // pin_name → net_name
    std::map<std::string, std::string> properties;

    bool operator==(const Device& other) const noexcept = default;
};

struct Port {
    std::string           name;
    std::string           direction;                 // "INPUT", "OUTPUT", "INOUT"
    std::string           net_name;
    std::optional<std::string> layer;
    std::optional<Point>     location;

    bool operator==(const Port& other) const noexcept = default;
};

struct Geometry {
    std::string layer;
    Shape       shape;

    bool operator==(const Geometry& other) const noexcept = default;
};

struct Annotation {
    std::string              key;
    std::string              value;
    std::optional<std::string> layer;
    std::optional<Point>     position;

    bool operator==(const Annotation& other) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Intermediate Representation — versioned, serialisable top-level document
// ---------------------------------------------------------------------------

struct LayoutIR {
    static constexpr int CURRENT_VERSION = 1;

    int                           version     = CURRENT_VERSION;
    std::string                   design_name;
    std::string                   description;
    std::vector<Layer>           layers;
    std::vector<Geometry>       geometries;
    std::vector<Net>            nets;
    std::vector<Device>         devices;
    std::vector<Port>           ports;
    std::vector<Annotation>     annotations;
    std::map<std::string, std::string> metadata;

    bool operator==(const LayoutIR& other) const noexcept = default;
};

// ---------------------------------------------------------------------------
// JSON conversion (ADL — nlohmann::json)
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& j, const Point& p);
void from_json(const nlohmann::json& j, Point& p);

void to_json(nlohmann::json& j, const Rectangle& r);
void from_json(const nlohmann::json& j, Rectangle& r);

void to_json(nlohmann::json& j, const Polygon& p);
void from_json(const nlohmann::json& j, Polygon& p);

void to_json(nlohmann::json& j, const Shape& s);
void from_json(const nlohmann::json& j, Shape& s);

void to_json(nlohmann::json& j, const Layer& l);
void from_json(const nlohmann::json& j, Layer& l);

void to_json(nlohmann::json& j, const Net& n);
void from_json(const nlohmann::json& j, Net& n);

void to_json(nlohmann::json& j, const Pin& p);
void from_json(const nlohmann::json& j, Pin& p);

void to_json(nlohmann::json& j, const Device& d);
void from_json(const nlohmann::json& j, Device& d);

void to_json(nlohmann::json& j, const Port& p);
void from_json(const nlohmann::json& j, Port& p);

void to_json(nlohmann::json& j, const Geometry& g);
void from_json(const nlohmann::json& j, Geometry& g);

void to_json(nlohmann::json& j, const Annotation& a);
void from_json(const nlohmann::json& j, Annotation& a);

void to_json(nlohmann::json& j, const LayoutIR& ir);
void from_json(const nlohmann::json& j, LayoutIR& ir);

// ---------------------------------------------------------------------------
// Schema validation helpers
// ---------------------------------------------------------------------------

/**
 * Validate a JSON object against the LayoutIR structural schema.
 *
 * Checks:
 *   - "version" exists and is a supported integer.
 *   - Required top-level keys are present.
 *   - All arrays contain objects of the expected shape.
 *
 * @return An error message string if invalid; empty string if valid.
 */
std::string validate_layout_ir_schema(const nlohmann::json& j);

/**
 * Load a LayoutIR from a JSON string with full schema validation.
 *
 * @throws aegis::parsing::LayoutIRError if validation or parsing fails.
 */
LayoutIR layout_ir_from_json_string(const std::string& text);

/**
 * Serialise a LayoutIR to a JSON string (pretty-printed).
 */
std::string layout_ir_to_json_string(const LayoutIR& ir);

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

class LayoutIRError : public std::runtime_error {
public:
    explicit LayoutIRError(const std::string& msg)
        : std::runtime_error("LayoutIR: " + msg) {}
};

} // namespace aegis::parsing
