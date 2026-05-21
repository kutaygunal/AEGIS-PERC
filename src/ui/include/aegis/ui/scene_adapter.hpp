#pragma once

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/storage/imported_design_session.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// UI scene value model
//
// Read-only presentation records derived from LayoutIR. These records are
// intentionally independent from QWidget/QPainter so rendering, hit testing,
// and future worker/GPU paths can share the same scene data.
// ---------------------------------------------------------------------------

struct ScenePoint {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const ScenePoint& other) const noexcept = default;
};

struct SceneBounds {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
    bool   valid = false;

    [[nodiscard]] double width() const noexcept;
    [[nodiscard]] double height() const noexcept;

    bool operator==(const SceneBounds& other) const noexcept = default;
};

enum class SceneItemKind {
    Geometry,
    Port,
    Annotation,
};

enum class SceneShapeKind {
    Rectangle,
    Polygon,
    Point,
};

enum class SceneLayerCategory {
    Unknown,
    Technology,
    Routing,
    CutVia,
    Pin,
    Blockage,
    Annotation,
    Instance,
    DieArea,
};

enum class ColoringMode {
    LayerColor,
    ObjectType,
    Domain,
    ViolationContext,
};

struct SceneLayer {
    std::string id;
    std::string name;
    std::string purpose;
    std::string color;
    int         z_order = 0;
    std::size_t source_index = 0;
    SceneLayerCategory category = SceneLayerCategory::Unknown;

    bool operator==(const SceneLayer& other) const noexcept = default;
};

struct SceneItem {
    std::string id;
    SceneItemKind kind = SceneItemKind::Geometry;
    SceneShapeKind shape_kind = SceneShapeKind::Rectangle;
    std::string layer_name;
    std::string color;
    int         z_order = 0;
    SceneBounds bounds;
    std::vector<ScenePoint> points;
    std::map<std::string, std::string> source_metadata;
    SceneLayerCategory layer_category = SceneLayerCategory::Unknown;

    bool operator==(const SceneItem& other) const noexcept = default;
};

struct UiScene {
    std::string design_name;
    std::vector<SceneLayer> layers;
    std::vector<SceneItem> items;
    SceneBounds bounds;

    [[nodiscard]] std::vector<const SceneItem*> items_by_kind(SceneItemKind kind) const;
    [[nodiscard]] const SceneItem* find_item_by_id(const std::string& id) const;
};

struct ImportedSceneBuildResult {
    UiScene scene;
    std::vector<std::string> diagnostics;
};

[[nodiscard]] UiScene build_ui_scene(const aegis::parsing::LayoutIR& ir);
[[nodiscard]] ImportedSceneBuildResult build_imported_design_scene(const aegis::storage::ImportedDesignSession& session);


} // namespace aegis::ui
