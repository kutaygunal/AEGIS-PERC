#include "aegis/ui/violation_heatmap.hpp"

#include <algorithm>

namespace aegis::ui {

QColor violation_heatmap_color(double normalized_count, double opacity)
{
    const double t = std::clamp(normalized_count, 0.0, 1.0);
    const double a = std::clamp(opacity, 0.0, 1.0);
    const int red = static_cast<int>(64.0 + t * 191.0);
    const int green = static_cast<int>(200.0 - t * 160.0);
    const int blue = static_cast<int>(255.0 - t * 220.0);
    return QColor(red, green, blue, static_cast<int>(a * 255.0));
}

} // namespace aegis::ui
