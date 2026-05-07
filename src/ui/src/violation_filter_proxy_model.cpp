#include "aegis/ui/violation_filter_proxy_model.hpp"

#include "aegis/ui/violation_table_model.hpp"

#include <QString>

namespace aegis::ui {
namespace {

bool contains_case_insensitive(const std::string& haystack, const std::string& needle)
{
    return QString::fromStdString(haystack).contains(QString::fromStdString(needle), Qt::CaseInsensitive);
}

} // namespace

ViolationFilterProxyModel::ViolationFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void ViolationFilterProxyModel::set_filter_state(ViolationFilterState state)
{
    if (m_filter_state == state) {
        return;
    }
    m_filter_state = std::move(state);
    invalidateRowsFilter();
}

const ViolationFilterState& ViolationFilterProxyModel::filter_state() const noexcept
{
    return m_filter_state;
}

const aegis::rules::Violation* ViolationFilterProxyModel::violation_at_proxy_row(int row) const
{
    const auto* source = qobject_cast<const ViolationTableModel*>(sourceModel());
    if (source == nullptr || row < 0 || row >= rowCount()) {
        return nullptr;
    }
    return source->violation_at(mapToSource(index(row, 0)).row());
}

bool ViolationFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    Q_UNUSED(source_parent);
    const auto* source = qobject_cast<const ViolationTableModel*>(sourceModel());
    const auto* violation = source != nullptr ? source->violation_at(source_row) : nullptr;
    if (violation == nullptr) {
        return false;
    }

    if (m_filter_state.severity.has_value() && violation->severity != *m_filter_state.severity) {
        return false;
    }
    if (!m_filter_state.rule_id.empty() && !contains_case_insensitive(violation->rule_id, m_filter_state.rule_id)) {
        return false;
    }
    if (!m_filter_state.layer.empty() &&
        !contains_case_insensitive(violation->location.layer.value_or(""), m_filter_state.layer)) {
        return false;
    }
    if (!m_filter_state.net.empty() &&
        !contains_case_insensitive(violation->location.net_name.value_or(""), m_filter_state.net)) {
        return false;
    }
    if (!m_filter_state.search_text.empty()) {
        const bool matches =
            contains_case_insensitive(violation->id, m_filter_state.search_text) ||
            contains_case_insensitive(violation->rule_id, m_filter_state.search_text) ||
            contains_case_insensitive(violation->message, m_filter_state.search_text) ||
            contains_case_insensitive(violation->location.layer.value_or(""), m_filter_state.search_text) ||
            contains_case_insensitive(violation->location.net_name.value_or(""), m_filter_state.search_text) ||
            contains_case_insensitive(violation->location.device_name.value_or(""), m_filter_state.search_text) ||
            contains_case_insensitive(violation->location.pin_name.value_or(""), m_filter_state.search_text);
        if (!matches) {
            return false;
        }
    }

    return true;
}

} // namespace aegis::ui
