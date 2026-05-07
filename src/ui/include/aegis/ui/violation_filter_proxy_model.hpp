#pragma once

#include "aegis/ui/violation_filter.hpp"

#include <QSortFilterProxyModel>

namespace aegis::ui {

class ViolationTableModel;

class ViolationFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ViolationFilterProxyModel(QObject* parent = nullptr);

    void set_filter_state(ViolationFilterState state);
    [[nodiscard]] const ViolationFilterState& filter_state() const noexcept;
    [[nodiscard]] const aegis::rules::Violation* violation_at_proxy_row(int row) const;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    ViolationFilterState m_filter_state;
};

} // namespace aegis::ui
