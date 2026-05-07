#pragma once

#include <QColor>
#include <QPointF>

#include <optional>
#include <string>
#include <vector>

namespace aegis::ui {

struct ViolationHeatmapSettings {
    bool visible = false;
    double opacity = 0.6;
};

struct ViolationHeatmapBucket {
    QPointF center;
    int count = 0;
    std::optional<std::string> layer_name;
};

[[nodiscard]] QColor violation_heatmap_color(double normalized_count, double opacity);

} // namespace aegis::ui
