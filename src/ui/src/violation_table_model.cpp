#include "aegis/ui/violation_table_model.hpp"

#include <QString>

namespace aegis::ui {

ViolationTableModel::ViolationTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void ViolationTableModel::set_violations(aegis::rules::ViolationCollection violations)
{
    beginResetModel();
    m_violations = std::move(violations);
    endResetModel();
}

const aegis::rules::Violation* ViolationTableModel::violation_at(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_violations.size()) {
        return nullptr;
    }
    return &m_violations[static_cast<std::size_t>(row)];
}

int ViolationTableModel::violation_count() const noexcept
{
    return static_cast<int>(m_violations.size());
}

int ViolationTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : violation_count();
}

int ViolationTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ViolationTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto* violation = violation_at(index.row());
    if (violation == nullptr) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case RuleIdColumn:   return QString::fromStdString(violation->rule_id);
            case SeverityColumn: return QString::fromStdString(aegis::rules::severity_to_string(violation->severity));
            case MessageColumn:  return QString::fromStdString(violation->message);
            case LayerColumn:    return QString::fromStdString(violation->location.layer.value_or(""));
            case NetColumn:      return QString::fromStdString(violation->location.net_name.value_or(""));
            case DeviceColumn:   return QString::fromStdString(violation->location.device_name.value_or(""));
            case PinColumn:      return QString::fromStdString(violation->location.pin_name.value_or(""));
            default:             return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        return QString::fromStdString(violation->message);
    }

    return {};
}

QVariant ViolationTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
        case RuleIdColumn:   return "Rule ID";
        case SeverityColumn: return "Severity";
        case MessageColumn:  return "Message";
        case LayerColumn:    return "Layer";
        case NetColumn:      return "Net";
        case DeviceColumn:   return "Device";
        case PinColumn:      return "Pin";
        default:             return {};
    }
}

} // namespace aegis::ui
