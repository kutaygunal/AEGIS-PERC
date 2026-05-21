#include "aegis/ui/scene_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace aegis::ui {
namespace {

constexpr const char* kFallbackColor = "#808080";

[[nodiscard]] std::string make_id(const char* prefix, std::size_t index)
{
    std::ostringstream out;
    out << prefix << ':' << index;
    return out.str();
}

void expand_bounds(SceneBounds& target, const SceneBounds& source)
{
    if (!source.valid) {
        return;
    }

    if (!target.valid) {
        target = source;
        return;
    }

    target.min_x = std::min(target.min_x, source.min_x);
    target.min_y = std::min(target.min_y, source.min_y);
    target.max_x = std::max(target.max_x, source.max_x);
    target.max_y = std::max(target.max_y, source.max_y);
    target.valid = true;
}

[[nodiscard]] SceneBounds bounds_from_point(const aegis::parsing::Point& p)
{
    return SceneBounds{p.x, p.y, p.x, p.y, true};
}

[[nodiscard]] SceneBounds bounds_from_rectangle(const aegis::parsing::Rectangle& r)
{
    const double x2 = r.x + r.width;
    const double y2 = r.y + r.height;
    return SceneBounds{std::min(r.x, x2), std::min(r.y, y2),
                       std::max(r.x, x2), std::max(r.y, y2), true};
}

[[nodiscard]] SceneBounds bounds_from_polygon(const aegis::parsing::Polygon& p)
{
    SceneBounds bounds;
    for (const auto& point : p.points) {
        expand_bounds(bounds, bounds_from_point(point));
    }
    return bounds;
}

[[nodiscard]] std::vector<ScenePoint> rectangle_points(const aegis::parsing::Rectangle& r)
{
    const double x2 = r.x + r.width;
    const double y2 = r.y + r.height;
    return {
        {r.x, r.y},
        {x2, r.y},
        {x2, y2},
        {r.x, y2},
    };
}

[[nodiscard]] std::vector<ScenePoint> polygon_points(const aegis::parsing::Polygon& p)
{
    std::vector<ScenePoint> points;
    points.reserve(p.points.size());
    for (const auto& point : p.points) {
        points.push_back({point.x, point.y});
    }
    return points;
}

[[nodiscard]] std::string layer_id(const std::string& name, std::size_t index)
{
    std::ostringstream out;
    out << "layer:" << index << ':' << name;
    return out.str();
}

[[nodiscard]] std::string sanitized_identifier(std::string value)
{
    std::replace_if(value.begin(), value.end(), [](char ch) {
        return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-');
    }, '_');
    return value;
}

struct LayerStyle {
    std::string color = kFallbackColor;
    int z_order = 0;
    SceneLayerCategory category = SceneLayerCategory::Unknown;
};


[[nodiscard]] SceneLayerCategory classify_layer(const std::string& name, const std::string& purpose)
{
    auto lower = [](std::string value) {
        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    };

    const std::string n = lower(name);
    const std::string p = lower(purpose);

    if (n.find("via") != std::string::npos || n.find("cut") != std::string::npos) {
        return SceneLayerCategory::CutVia;
    }
    if (n.find("pin") != std::string::npos || n.find("port") != std::string::npos) {
        return SceneLayerCategory::Pin;
    }
    if (n.find("blockage") != std::string::npos || n.find("obs") != std::string::npos) {
        return SceneLayerCategory::Blockage;
    }
    if (n.find("annotation") != std::string::npos || n.find("label") != std::string::npos || n.find("text") != std::string::npos) {
        return SceneLayerCategory::Annotation;
    }
    if (n.find("poly") != std::string::npos || n.find("diff") != std::string::npos ||
        n.find("well") != std::string::npos || n.find("active") != std::string::npos ||
        n.find("implant") != std::string::npos || n.find("contact") != std::string::npos) {
        return SceneLayerCategory::Technology;
    }
    if (n.find("metal") != std::string::npos || n.find("route") != std::string::npos ||
        (n.size() >= 2 && n[0] == 'm' && std::isdigit(static_cast<unsigned char>(n[1])))) {
        return SceneLayerCategory::Routing;
    }

    if (p.find("route") != std::string::npos || p.find("wire") != std::string::npos) {
        return SceneLayerCategory::Routing;
    }
    if (p.find("pin") != std::string::npos || p.find("port") != std::string::npos) {
        return SceneLayerCategory::Pin;
    }
    if (p.find("via") != std::string::npos || p.find("cut") != std::string::npos) {
        return SceneLayerCategory::CutVia;
    }
    if (p.find("block") != std::string::npos || p.find("obs") != std::string::npos) {
        return SceneLayerCategory::Blockage;
    }

    return SceneLayerCategory::Unknown;
}

[[nodiscard]] std::unordered_map<std::string, LayerStyle> build_layer_style_map(
    const aegis::parsing::LayoutIR& ir)
{
    std::unordered_map<std::string, LayerStyle> styles;
    for (std::size_t i = 0; i < ir.layers.size(); ++i) {
        const auto& layer = ir.layers[i];
        const std::string color = layer.color.empty() ? std::string{kFallbackColor} : layer.color;
        styles[layer.name] = LayerStyle{color, layer.order, classify_layer(layer.name, layer.purpose)};
    }
    return styles;
}

[[nodiscard]] LayerStyle style_for_layer(const std::unordered_map<std::string, LayerStyle>& styles,
                                         const std::string& layer_name)
{
    const auto it = styles.find(layer_name);
    if (it != styles.end()) {
        return it->second;
    }
    return LayerStyle{};
}

[[nodiscard]] const aegis::storage::ImportedDesignObject* find_imported_object(
    const aegis::storage::ImportedDesignSession& session,
    aegis::storage::ImportedDesignObjectKind kind,
    const std::string& name)
{
    for (const auto& object : session.objects()) {
        if (object.kind == kind && object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string scene_item_id_from_object(const aegis::storage::ImportedDesignObject* object,
                                                    const std::string& fallback)
{
    return object != nullptr ? object->stable_id : fallback;
}

void add_scene_item(UiScene& scene, SceneItem item)
{
    expand_bounds(scene.bounds, item.bounds);
    scene.items.push_back(std::move(item));
}

[[nodiscard]] std::optional<aegis::parsing::Rectangle> parse_diearea(const aegis::parsing::LayoutIR& ir)
{
    const auto it = ir.metadata.find("diearea");
    if (it == ir.metadata.end()) {
        return std::nullopt;
    }

    std::istringstream input(it->second);
    std::string token;
    std::vector<double> values;
    while (std::getline(input, token, ',')) {
        try {
            values.push_back(std::stod(token));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (values.size() != 4) {
        return std::nullopt;
    }
    return aegis::parsing::Rectangle{values[0], values[1], values[2] - values[0], values[3] - values[1]};
}

[[nodiscard]] std::unordered_map<std::string, std::pair<double, double>> build_macro_size_map(
    const aegis::storage::ImportedDesignSession& session)
{
    std::unordered_map<std::string, std::pair<double, double>> sizes;
    for (const auto& library : session.technology_libraries()) {
        for (const auto& macro : library.macros) {
            sizes.emplace(macro.name, std::pair<double, double>{macro.width, macro.height});
        }
    }
    return sizes;
}

void append_metadata(SceneItem& item,
                     const aegis::storage::ImportedDesignObject* object,
                     const std::map<std::string, std::string>& extra = {})
{
    if (object != nullptr) {
        item.source_metadata["object_id"] = object->stable_id;
        item.source_metadata["object_kind"] = aegis::storage::to_string(object->kind);
        item.source_metadata["name"] = object->name;
        item.source_metadata["display_name"] = object->display_name;
        item.source_metadata["artifact_id"] = object->provenance.artifact_id;
        item.source_metadata["artifact_path"] = object->provenance.artifact_path.generic_string();
        item.source_metadata["parser_name"] = object->provenance.parser_name;
        for (const auto& [key, value] : object->metadata) {
            item.source_metadata.emplace(key, value);
        }
    }
    for (const auto& [key, value] : extra) {
        item.source_metadata[key] = value;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// SceneBounds
// ---------------------------------------------------------------------------

double SceneBounds::width() const noexcept
{
    return valid ? (max_x - min_x) : 0.0;
}

double SceneBounds::height() const noexcept
{
    return valid ? (max_y - min_y) : 0.0;
}

// ---------------------------------------------------------------------------
// UiScene helpers
// ---------------------------------------------------------------------------

std::vector<const SceneItem*> UiScene::items_by_kind(SceneItemKind kind) const
{
    std::vector<const SceneItem*> result;
    for (const auto& item : items) {
        if (item.kind == kind) {
            result.push_back(&item);
        }
    }
    return result;
}

const SceneItem* UiScene::find_item_by_id(const std::string& id) const
{
    const auto it = std::find_if(items.begin(), items.end(), [&id](const SceneItem& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

// ---------------------------------------------------------------------------
// Adapter
// ---------------------------------------------------------------------------

UiScene build_ui_scene(const aegis::parsing::LayoutIR& ir)
{
    UiScene scene;
    scene.design_name = ir.design_name;

    scene.layers.reserve(ir.layers.size());
    for (std::size_t i = 0; i < ir.layers.size(); ++i) {
        const auto& layer = ir.layers[i];
        scene.layers.push_back(SceneLayer{
            layer_id(layer.name, i),
            layer.name,
            layer.purpose,
            layer.color.empty() ? std::string{kFallbackColor} : layer.color,
            layer.order,
            i,
            classify_layer(layer.name, layer.purpose),
        });
    }

    const auto layer_styles = build_layer_style_map(ir);
    scene.items.reserve(ir.geometries.size() + ir.ports.size() + ir.annotations.size());

    for (std::size_t i = 0; i < ir.geometries.size(); ++i) {
        const auto& geometry = ir.geometries[i];
        const auto style = style_for_layer(layer_styles, geometry.layer);

        SceneItem item;
        item.id = make_id("geometry", i);
        item.kind = SceneItemKind::Geometry;
        item.layer_name = geometry.layer;
        item.color = style.color;
        item.z_order = style.z_order;
        item.layer_category = style.category;
        item.source_metadata = {
            {"source", "LayoutIR.geometries"},
            {"source_index", std::to_string(i)},
            {"layer", geometry.layer},
        };

        if (std::holds_alternative<aegis::parsing::Rectangle>(geometry.shape)) {
            const auto& rectangle = std::get<aegis::parsing::Rectangle>(geometry.shape);
            item.shape_kind = SceneShapeKind::Rectangle;
            item.bounds = bounds_from_rectangle(rectangle);
            item.points = rectangle_points(rectangle);
        } else {
            const auto& polygon = std::get<aegis::parsing::Polygon>(geometry.shape);
            item.shape_kind = SceneShapeKind::Polygon;
            item.bounds = bounds_from_polygon(polygon);
            item.points = polygon_points(polygon);
        }

        add_scene_item(scene, std::move(item));
    }

    for (std::size_t i = 0; i < ir.ports.size(); ++i) {
        const auto& port = ir.ports[i];
        const std::string layer_name = port.layer.value_or(std::string{});
        const auto style = style_for_layer(layer_styles, layer_name);

        SceneItem item;
        item.id = make_id("port", i);
        item.kind = SceneItemKind::Port;
        item.shape_kind = SceneShapeKind::Point;
        item.layer_name = layer_name;
        item.color = style.color;
        item.z_order = style.z_order;
        item.layer_category = SceneLayerCategory::Pin;
        item.source_metadata = {
            {"source", "LayoutIR.ports"},
            {"source_index", std::to_string(i)},
            {"name", port.name},
            {"direction", port.direction},
            {"net_name", port.net_name},
        };
        if (port.location.has_value()) {
            item.points.push_back({port.location->x, port.location->y});
            item.bounds = bounds_from_point(*port.location);
        }
        add_scene_item(scene, std::move(item));
    }

    for (std::size_t i = 0; i < ir.annotations.size(); ++i) {
        const auto& annotation = ir.annotations[i];
        const std::string layer_name = annotation.layer.value_or(std::string{});
        const auto style = style_for_layer(layer_styles, layer_name);

        SceneItem item;
        item.id = make_id("annotation", i);
        item.kind = SceneItemKind::Annotation;
        item.shape_kind = SceneShapeKind::Point;
        item.layer_name = layer_name;
        item.color = style.color;
        item.z_order = style.z_order;
        item.layer_category = SceneLayerCategory::Annotation;
        item.source_metadata = {
            {"source", "LayoutIR.annotations"},
            {"source_index", std::to_string(i)},
            {"key", annotation.key},
            {"value", annotation.value},
        };
        if (annotation.position.has_value()) {
            item.points.push_back({annotation.position->x, annotation.position->y});
            item.bounds = bounds_from_point(*annotation.position);
        }
        add_scene_item(scene, std::move(item));
    }

    std::stable_sort(scene.items.begin(), scene.items.end(), [](const SceneItem& a, const SceneItem& b) {
        return a.z_order < b.z_order;
    });

    return scene;
}

ImportedSceneBuildResult build_imported_design_scene(const aegis::storage::ImportedDesignSession& session)
{
    ImportedSceneBuildResult result;
    result.scene = build_ui_scene(session.physical_ir());
    result.scene.design_name = !session.package().project().name.empty()
        ? session.package().project().name
        : session.combined_ir().design_name;

    std::unordered_map<std::string, std::size_t> layer_indices;
    for (std::size_t i = 0; i < result.scene.layers.size(); ++i) {
        layer_indices.emplace(result.scene.layers[i].name, i);
    }

    auto ensure_scene_layer = [&](const std::string& layer_name,
                                  const std::string& purpose,
                                  const std::string& color,
                                  int z_order) {
        if (layer_name.empty() || layer_indices.count(layer_name) != 0) {
            return;
        }
        const std::size_t index = result.scene.layers.size();
        result.scene.layers.push_back(SceneLayer{layer_id(layer_name, index), layer_name, purpose, color, z_order, index, classify_layer(layer_name, purpose)});
        layer_indices.emplace(layer_name, index);
    };

    for (const auto& library : session.technology_libraries()) {
        for (const auto& layer : library.layers) {
            ensure_scene_layer(layer.name,
                               layer.type.empty() ? std::string{"technology"} : layer.type,
                               kFallbackColor,
                               static_cast<int>(result.scene.layers.size()));
        }
    }
    ensure_scene_layer("__instance__", "placement", "#4C78A8", 50);
    ensure_scene_layer("__diearea__", "diearea", "#999999", -100);

    std::unordered_map<std::string, const aegis::storage::ImportedDesignObject*> port_objects;
    std::unordered_map<std::string, const aegis::storage::ImportedDesignObject*> layer_objects;
    for (const auto& object : session.objects()) {
        if (object.kind == aegis::storage::ImportedDesignObjectKind::Port) {
            port_objects.emplace(object.name, &object);
        } else if (object.kind == aegis::storage::ImportedDesignObjectKind::Layer) {
            layer_objects.emplace(object.name, &object);
        }
    }

    for (auto& layer : result.scene.layers) {
        if (const auto it = layer_objects.find(layer.name); it != layer_objects.end()) {
            layer.id = it->second->stable_id;
        }
    }

    for (auto& item : result.scene.items) {
        if (item.kind == SceneItemKind::Port) {
            const auto name_it = item.source_metadata.find("name");
            if (name_it != item.source_metadata.end()) {
                if (const auto obj_it = port_objects.find(name_it->second); obj_it != port_objects.end()) {
                    item.id = obj_it->second->stable_id;
                    append_metadata(item, obj_it->second, {{"scene_origin", "imported_port"}});
                }
            }
        } else if (item.kind == SceneItemKind::Geometry) {
            if (const auto it = layer_objects.find(item.layer_name); it != layer_objects.end()) {
                append_metadata(item, it->second, {{"scene_origin", "imported_geometry"}});
            }
        } else if (item.kind == SceneItemKind::Annotation) {
            item.source_metadata["scene_origin"] = "imported_annotation";
        }
    }

    const auto macro_sizes = build_macro_size_map(session);
    std::size_t placed_instances = 0;
    std::size_t fallback_sized_instances = 0;
    std::size_t skipped_instances = 0;
    const auto diearea = parse_diearea(session.physical_ir());
    const double fallback_instance_size = [&]() {
        if (diearea.has_value()) {
            return std::max(1000.0, std::min(std::abs(diearea->width), std::abs(diearea->height)) * 0.0025);
        }
        return 1000.0;
    }();

    for (const auto& device : session.physical_ir().devices) {
        const auto x_it = device.properties.find("x");
        const auto y_it = device.properties.find("y");
        const auto macro_it = device.properties.find("macro");
        if (x_it == device.properties.end() || y_it == device.properties.end() || macro_it == device.properties.end()) {
            ++skipped_instances;
            continue;
        }

        double x = 0.0;
        double y = 0.0;
        try {
            x = std::stod(x_it->second);
            y = std::stod(y_it->second);
        } catch (...) {
            ++skipped_instances;
            continue;
        }

        double width = fallback_instance_size;
        double height = fallback_instance_size;
        bool used_fallback_size = true;
        if (const auto size_it = macro_sizes.find(macro_it->second); size_it != macro_sizes.end()) {
            width = size_it->second.first;
            height = size_it->second.second;
            used_fallback_size = false;
        }

        const auto* object = find_imported_object(session, aegis::storage::ImportedDesignObjectKind::Instance, device.name);

        SceneItem item;
        item.id = scene_item_id_from_object(object, "instance:" + sanitized_identifier(device.name));
        item.kind = SceneItemKind::Geometry;
        item.shape_kind = SceneShapeKind::Rectangle;
        item.layer_name = "__instance__";
        item.color = "#4C78A8";
        item.z_order = 50;
        item.layer_category = SceneLayerCategory::Instance;
        const aegis::parsing::Rectangle rectangle{x, y, width, height};
        item.bounds = bounds_from_rectangle(rectangle);
        item.points = rectangle_points(rectangle);
        append_metadata(item,
                        object,
                        {{"source", "ImportedDesignSession.instances"},
                         {"macro", macro_it->second},
                         {"x", x_it->second},
                         {"y", y_it->second},
                         {"size_source", used_fallback_size ? "fallback" : "lef_macro"},
                         {"scene_origin", "imported_instance"}});
        add_scene_item(result.scene, std::move(item));
        ++placed_instances;
        if (used_fallback_size) {
            ++fallback_sized_instances;
        }
    }

    if (diearea.has_value()) {
        SceneItem item;
        item.id = "diearea:imported";
        item.kind = SceneItemKind::Geometry;
        item.shape_kind = SceneShapeKind::Rectangle;
        item.layer_name = "__diearea__";
        item.color = "#999999";
        item.z_order = -100;
        item.layer_category = SceneLayerCategory::DieArea;
        item.bounds = bounds_from_rectangle(*diearea);
        item.points = rectangle_points(*diearea);
        item.source_metadata = {
            {"source", "ImportedDesignSession.diearea"},
            {"scene_origin", "imported_diearea"}
        };
        add_scene_item(result.scene, std::move(item));
    }

    const auto add_count_annotation = [&](const std::string& key,
                                          const std::string& label,
                                          double x,
                                          double y) {
        const auto it = session.physical_ir().metadata.find(key);
        if (it == session.physical_ir().metadata.end()) {
            return;
        }
        SceneItem item;
        item.id = "annotation:" + key;
        item.kind = SceneItemKind::Annotation;
        item.shape_kind = SceneShapeKind::Point;
        item.layer_name = "__diearea__";
        item.color = "#999999";
        item.z_order = -90;
        item.layer_category = SceneLayerCategory::Annotation;
        item.points.push_back({x, y});
        item.bounds = SceneBounds{x, y, x, y, true};
        item.source_metadata = {
            {"source", "ImportedDesignSession.metadata"},
            {"key", key},
            {"value", label + ": " + it->second},
            {"scene_origin", "imported_count_annotation"}
        };
        add_scene_item(result.scene, std::move(item));
    };

    if (result.scene.bounds.valid) {
        add_count_annotation("row_count", "Rows", result.scene.bounds.min_x, result.scene.bounds.max_y);
        add_count_annotation("track_count", "Tracks", result.scene.bounds.min_x, result.scene.bounds.max_y - std::max(10.0, result.scene.bounds.height() * 0.02));
    }

    if (placed_instances == 0) {
        result.diagnostics.push_back("Imported DEF produced no placeable instance geometry; the scene may contain only die area, routes, and pin shapes.");
    }
    if (fallback_sized_instances > 0) {
        std::ostringstream message;
        message << "Rendered " << fallback_sized_instances
                << " instance(s) with fallback marker sizes because matching LEF macro dimensions were unavailable.";
        result.diagnostics.push_back(message.str());
    }
    if (skipped_instances > 0) {
        std::ostringstream message;
        message << "Skipped " << skipped_instances
                << " instance(s) because placement coordinates or macro names were unavailable.";
        result.diagnostics.push_back(message.str());
    }
    if (session.physical_ir().metadata.find("row_count") != session.physical_ir().metadata.end()) {
        result.diagnostics.push_back("Row and track counts are surfaced as scene annotations until explicit row-guide geometry is available.");
    }

    std::stable_sort(result.scene.items.begin(), result.scene.items.end(), [](const SceneItem& a, const SceneItem& b) {
        return a.z_order < b.z_order;
    });

    return result;
}

} // namespace aegis::ui
