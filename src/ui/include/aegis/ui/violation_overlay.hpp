#pragma once

#include "aegis/rules/violation.hpp"

#include <QColor>
#include <QRectF>

#include <optional>
#include <string>
#include <vector>

namespace aegis::ui {

struct ViolationOverlayItem {
    std::string violation_id;
    aegis::rules::Severity severity = aegis::rules::Severity::Error;
    std::optional<QPointF> point;
    std::optional<QRectF> bounds;
    bool resolved = false;
};

[[nodiscard]] QColor violation_severity_color(aegis::rules::Severity severity);

} // namespace aegis::ui
