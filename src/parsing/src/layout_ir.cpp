#include "aegis/parsing/layout_ir.hpp"

#include <sstream>

namespace aegis::parsing {

// ===========================================================================
// Geometric primitives
// ===========================================================================

void to_json(nlohmann::json& j, const Point& p) {
    j = nlohmann::json{{"x", p.x}, {"y", p.y}};
}

void from_json(const nlohmann::json& j, Point& p) {
    if (!j.is_object()) throw LayoutIRError("Point must be an object");
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

void to_json(nlohmann::json& j, const Rectangle& r) {
    j = nlohmann::json{
        {"type",     "rectangle"},
        {"x",        r.x},
        {"y",        r.y},
        {"width",    r.width},
        {"height",   r.height}
    };
}

void from_json(const nlohmann::json& j, Rectangle& r) {
    if (!j.is_object()) throw LayoutIRError("Rectangle must be an object");
    j.at("x").get_to(r.x);
    j.at("y").get_to(r.y);
    j.at("width").get_to(r.width);
    j.at("height").get_to(r.height);
}

void to_json(nlohmann::json& j, const Polygon& p) {
    j = nlohmann::json{{"type", "polygon"}, {"points", p.points}};
}

void from_json(const nlohmann::json& j, Polygon& p) {
    if (!j.is_object()) throw LayoutIRError("Polygon must be an object");
    j.at("points").get_to(p.points);
}

// ===========================================================================
// Shape variant (discriminator-based)
// ===========================================================================

void to_json(nlohmann::json& j, const Shape& s) {
    std::visit(
        [&j](const auto& v) { to_json(j, v); },
        s
    );
}

void from_json(const nlohmann::json& j, Shape& s) {
    if (!j.is_object()) throw LayoutIRError("Shape must be an object");
    const std::string type = j.value("type", std::string{});
    if (type == "rectangle") {
        Rectangle r;
        from_json(j, r);
        s = r;
    } else if (type == "polygon") {
        Polygon p;
        from_json(j, p);
        s = p;
    } else {
        throw LayoutIRError("Unknown shape type: " + type);
    }
}

// ===========================================================================
// Layout building blocks
// ===========================================================================

void to_json(nlohmann::json& j, const Layer& l) {
    j = nlohmann::json{
        {"name",    l.name},
        {"purpose", l.purpose},
        {"order",   l.order},
        {"color",   l.color}
    };
}

void from_json(const nlohmann::json& j, Layer& l) {
    if (!j.is_object()) throw LayoutIRError("Layer must be an object");
    j.at("name").get_to(l.name);
    j.at("purpose").get_to(l.purpose);
    j.at("order").get_to(l.order);
    j.at("color").get_to(l.color);
}

void to_json(nlohmann::json& j, const Net& n) {
    j = nlohmann::json{
        {"name",       n.name},
        {"pin_names",  n.pin_names},
        {"properties", n.properties}
    };
}

void from_json(const nlohmann::json& j, Net& n) {
    if (!j.is_object()) throw LayoutIRError("Net must be an object");
    j.at("name").get_to(n.name);
    j.at("pin_names").get_to(n.pin_names);
    if (j.contains("properties")) j.at("properties").get_to(n.properties);
}

void to_json(nlohmann::json& j, const Pin& p) {
    j = nlohmann::json{
        {"name",      p.name},
        {"net_name",  p.net_name},
        {"direction", p.direction}
    };
}

void from_json(const nlohmann::json& j, Pin& p) {
    if (!j.is_object()) throw LayoutIRError("Pin must be an object");
    j.at("name").get_to(p.name);
    j.at("net_name").get_to(p.net_name);
    j.at("direction").get_to(p.direction);
}

void to_json(nlohmann::json& j, const Device& d) {
    j = nlohmann::json{
        {"name",       d.name},
        {"type",       d.type},
        {"pins",       d.pins},
        {"properties", d.properties}
    };
}

void from_json(const nlohmann::json& j, Device& d) {
    if (!j.is_object()) throw LayoutIRError("Device must be an object");
    j.at("name").get_to(d.name);
    j.at("type").get_to(d.type);
    if (j.contains("pins"))       j.at("pins").get_to(d.pins);
    if (j.contains("properties")) j.at("properties").get_to(d.properties);
}

void to_json(nlohmann::json& j, const Port& p) {
    j = nlohmann::json::object();
    j["name"]      = p.name;
    j["direction"] = p.direction;
    j["net_name"]  = p.net_name;
    j["layer"]     = p.layer    ? nlohmann::json(*p.layer)    : nlohmann::json(nullptr);
    j["location"]  = p.location ? nlohmann::json(*p.location) : nlohmann::json(nullptr);
}

void from_json(const nlohmann::json& j, Port& p) {
    if (!j.is_object()) throw LayoutIRError("Port must be an object");
    j.at("name").get_to(p.name);
    j.at("direction").get_to(p.direction);
    j.at("net_name").get_to(p.net_name);
    if (j.contains("layer")    && !j.at("layer").is_null())    p.layer    = j.at("layer").get<std::string>();
    if (j.contains("location") && !j.at("location").is_null()) p.location = j.at("location").get<Point>();
}

void to_json(nlohmann::json& j, const Geometry& g) {
    j = nlohmann::json{
        {"layer", g.layer},
        {"shape", g.shape}
    };
}

void from_json(const nlohmann::json& j, Geometry& g) {
    if (!j.is_object()) throw LayoutIRError("Geometry must be an object");
    j.at("layer").get_to(g.layer);
    j.at("shape").get_to(g.shape);
}

void to_json(nlohmann::json& j, const Annotation& a) {
    j = nlohmann::json::object();
    j["key"]      = a.key;
    j["value"]    = a.value;
    j["layer"]    = a.layer    ? nlohmann::json(*a.layer)    : nlohmann::json(nullptr);
    j["position"] = a.position ? nlohmann::json(*a.position) : nlohmann::json(nullptr);
}

void from_json(const nlohmann::json& j, Annotation& a) {
    if (!j.is_object()) throw LayoutIRError("Annotation must be an object");
    j.at("key").get_to(a.key);
    j.at("value").get_to(a.value);
    if (j.contains("layer")    && !j.at("layer").is_null())    a.layer    = j.at("layer").get<std::string>();
    if (j.contains("position") && !j.at("position").is_null()) a.position = j.at("position").get<Point>();
}

// ===========================================================================
// Top-level LayoutIR
// ===========================================================================

void to_json(nlohmann::json& j, const LayoutIR& ir) {
    j = nlohmann::json{
        {"version",     ir.version},
        {"design_name", ir.design_name},
        {"description", ir.description},
        {"layers",      ir.layers},
        {"geometries",  ir.geometries},
        {"nets",        ir.nets},
        {"devices",     ir.devices},
        {"ports",       ir.ports},
        {"annotations", ir.annotations},
        {"metadata",    ir.metadata}
    };
}

void from_json(const nlohmann::json& j, LayoutIR& ir) {
    if (!j.is_object()) throw LayoutIRError("LayoutIR must be an object");

    j.at("version").get_to(ir.version);
    j.at("design_name").get_to(ir.design_name);
    j.at("description").get_to(ir.description);
    j.at("layers").get_to(ir.layers);
    j.at("geometries").get_to(ir.geometries);
    j.at("nets").get_to(ir.nets);
    j.at("devices").get_to(ir.devices);
    j.at("ports").get_to(ir.ports);
    j.at("annotations").get_to(ir.annotations);
    j.at("metadata").get_to(ir.metadata);
}

// ===========================================================================
// Schema validation
// ===========================================================================

static std::string validate_string_field(const nlohmann::json& j,
                                          const std::string& key) {
    if (!j.contains(key)) return "Missing required field: '" + key + "'";
    if (!j.at(key).is_string()) return "Field '" + key + "' must be a string";
    return {};
}

static std::string validate_int_field(const nlohmann::json& j,
                                       const std::string& key) {
    if (!j.contains(key)) return "Missing required field: '" + key + "'";
    if (!j.at(key).is_number_integer()) return "Field '" + key + "' must be an integer";
    return {};
}

static std::string validate_array_of_objects(const nlohmann::json& j,
                                                const std::string& key) {
    if (!j.contains(key)) return "Missing required field: '" + key + "'";
    if (!j.at(key).is_array()) return "Field '" + key + "' must be an array";
    return {};
}

std::string validate_layout_ir_schema(const nlohmann::json& j) {
    if (!j.is_object()) return "Root must be a JSON object";

    if (auto msg = validate_int_field(j, "version");   !msg.empty()) return msg;
    if (auto msg = validate_string_field(j, "design_name"); !msg.empty()) return msg;
    if (auto msg = validate_string_field(j, "description"); !msg.empty()) return msg;

    if (auto msg = validate_array_of_objects(j, "layers");      !msg.empty()) return msg;
    if (auto msg = validate_array_of_objects(j, "geometries");  !msg.empty()) return msg;
    if (auto msg = validate_array_of_objects(j, "nets");        !msg.empty()) return msg;
    if (auto msg = validate_array_of_objects(j, "devices");     !msg.empty()) return msg;
    if (auto msg = validate_array_of_objects(j, "ports");       !msg.empty()) return msg;
    if (auto msg = validate_array_of_objects(j, "annotations"); !msg.empty()) return msg;

    if (!j.contains("metadata")) return "Missing required field: 'metadata'";
    if (!j.at("metadata").is_object()) return "Field 'metadata' must be an object";

    // Check that all geometry shapes have valid discriminators
    for (const auto& geo : j.at("geometries")) {
        if (!geo.is_object()) return "Each geometry must be an object";
        if (!geo.contains("layer")) return "Geometry missing 'layer' field";
        if (!geo.contains("shape")) return "Geometry missing 'shape' field";
        const auto& shape = geo.at("shape");
        if (!shape.is_object()) return "Geometry 'shape' must be an object";
        const std::string type = shape.value("type", std::string{});
        if (type != "rectangle" && type != "polygon")
            return "Unknown geometry shape type: " + type;
    }

    // Check that all layer entries have required fields
    for (const auto& layer : j.at("layers")) {
        if (!layer.is_object()) return "Each layer must be an object";
        if (!layer.contains("name"))    return "Layer missing 'name' field";
        if (!layer.contains("purpose")) return "Layer missing 'purpose' field";
        if (!layer.contains("order"))   return "Layer missing 'order' field";
        if (!layer.contains("color"))   return "Layer missing 'color' field";
    }

    // Check device entries
    for (const auto& dev : j.at("devices")) {
        if (!dev.is_object()) return "Each device must be an object";
        if (!dev.contains("name")) return "Device missing 'name' field";
        if (!dev.contains("type")) return "Device missing 'type' field";
    }

    // Check net entries
    for (const auto& net : j.at("nets")) {
        if (!net.is_object()) return "Each net must be an object";
        if (!net.contains("name")) return "Net missing 'name' field";
    }

    return {}; // valid
}

// ===========================================================================
// Convenience helpers
// ===========================================================================

LayoutIR layout_ir_from_json_string(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        throw LayoutIRError("Invalid JSON");
    }

    // Schema validation before deserialization
    if (const auto msg = validate_layout_ir_schema(j); !msg.empty()) {
        throw LayoutIRError(msg);
    }

    LayoutIR ir;
    from_json(j, ir);

    if (ir.version != LayoutIR::CURRENT_VERSION) {
        throw LayoutIRError(
            "Unsupported IR version: " + std::to_string(ir.version) +
            " (expected " + std::to_string(LayoutIR::CURRENT_VERSION) + ")"
        );
    }

    return ir;
}

std::string layout_ir_to_json_string(const LayoutIR& ir) {
    nlohmann::json j = ir;
    return j.dump(2);
}

} // namespace aegis::parsing
