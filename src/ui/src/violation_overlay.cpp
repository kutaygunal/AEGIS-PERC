#include "aegis/ui/violation_overlay.hpp"

namespace aegis::ui {

QColor violation_severity_color(aegis::rules::Severity severity)
{
    using aegis::rules::Severity;
    switch (severity) {
        case Severity::Info:    return QColor("#4FC3F7");
        case Severity::Warning: return QColor("#FFD54F");
        case Severity::Error:   return QColor("#FF6E6E");
        case Severity::Fatal:   return QColor("#CE93D8");
    }
    return QColor("#FF6E6E");
}

} // namespace aegis::ui
