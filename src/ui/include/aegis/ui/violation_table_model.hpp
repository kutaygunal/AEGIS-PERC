#pragma once

#include "aegis/rules/violation.hpp"

#include <QAbstractTableModel>

namespace aegis::ui {

class ViolationTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        RuleIdColumn = 0,
        SeverityColumn,
        MessageColumn,
        LayerColumn,
        NetColumn,
        DeviceColumn,
        PinColumn,
        ColumnCount
    };

    explicit ViolationTableModel(QObject* parent = nullptr);

    void set_violations(aegis::rules::ViolationCollection violations);
    [[nodiscard]] const aegis::rules::Violation* violation_at(int row) const;
    [[nodiscard]] int violation_count() const noexcept;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    aegis::rules::ViolationCollection m_violations;
};

} // namespace aegis::ui
