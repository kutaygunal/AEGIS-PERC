#include "aegis/ui/scene_adapter.hpp"

#include <algorithm>
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

struct LayerStyle {
    std::string color = kFallbackColor;
    int z_order = 0;
};

[[nodiscard]] std::unordered_map<std::string, LayerStyle> build_layer_style_map(
    const aegis::parsing::LayoutIR& ir)
{
    std::unordered_map<std::string, LayerStyle> styles;
    for (std::size_t i = 0; i < ir.layers.size(); ++i) {
        const auto& layer = ir.layers[i];
        const std::string color = layer.color.empty() ? std::string{kFallbackColor} : layer.color;
        styles[layer.name] = LayerStyle{color, layer.order};
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

        expand_bounds(scene.bounds, item.bounds);
        scene.items.push_back(std::move(item));
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
            expand_bounds(scene.bounds, item.bounds);
        }
        scene.items.push_back(std::move(item));
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
        item.source_metadata = {
            {"source", "LayoutIR.annotations"},
            {"source_index", std::to_string(i)},
            {"key", annotation.key},
            {"value", annotation.value},
        };
        if (annotation.position.has_value()) {
            item.points.push_back({annotation.position->x, annotation.position->y});
            item.bounds = bounds_from_point(*annotation.position);
            expand_bounds(scene.bounds, item.bounds);
        }
        scene.items.push_back(std::move(item));
    }

    std::stable_sort(scene.items.begin(), scene.items.end(), [](const SceneItem& a, const SceneItem& b) {
        return a.z_order < b.z_order;
    });

    return scene;
}

} // namespace aegis::ui
