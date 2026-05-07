#include "aegis/ui/violation_filter.hpp"

namespace aegis::ui {

QVariantMap ViolationFilterState::to_variant_map() const
{
    QVariantMap map;
    if (severity.has_value()) {
        map.insert("severity", QString::fromStdString(aegis::rules::severity_to_string(*severity)));
    }
    map.insert("rule_id", QString::fromStdString(rule_id));
    map.insert("layer", QString::fromStdString(layer));
    map.insert("net", QString::fromStdString(net));
    map.insert("search_text", QString::fromStdString(search_text));
    return map;
}

ViolationFilterState ViolationFilterState::from_variant_map(const QVariantMap& map)
{
    ViolationFilterState state;
    const QString severity_text = map.value("severity").toString().trimmed();
    if (!severity_text.isEmpty()) {
        state.severity = aegis::rules::severity_from_string(severity_text.toStdString());
    }
    state.rule_id = map.value("rule_id").toString().toStdString();
    state.layer = map.value("layer").toString().toStdString();
    state.net = map.value("net").toString().toStdString();
    state.search_text = map.value("search_text").toString().toStdString();
    return state;
}

} // namespace aegis::ui
