#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QSlider;
class QTableView;
class QTableWidget;
class QTextEdit;

namespace aegis::ui {

class ViolationFilterProxyModel;
class ViolationTableModel;

class ViolationExplorerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViolationExplorerPanel(QWidget* parent = nullptr);

    void set_violations(aegis::rules::ViolationCollection violations);
    [[nodiscard]] int violation_count() const noexcept;
    [[nodiscard]] int total_violation_count() const noexcept;
    [[nodiscard]] const aegis::rules::Violation* current_violation() const;
    void select_row(int row);

    void set_filter_state(ViolationFilterState state);
    [[nodiscard]] ViolationFilterState filter_state() const;
    void clear_filters();
    [[nodiscard]] aegis::rules::ViolationCollection filtered_violations() const;
    [[nodiscard]] QString filter_summary_text() const;
    void refresh_preview_state();

    [[nodiscard]] QString current_violation_id() const;
    [[nodiscard]] QString details_summary_text() const;
    [[nodiscard]] int metadata_row_count() const;
    void set_heatmap_visible(bool visible);
    void set_heatmap_opacity(double opacity);
    [[nodiscard]] bool heatmap_visible() const;
    [[nodiscard]] double heatmap_opacity() const;

signals:
    void current_violation_changed();
    void filtered_violations_changed(aegis::rules::ViolationCollection violations);
    void heatmap_settings_changed(bool visible, double opacity);

private:
    aegis::rules::ViolationCollection m_violations;
    ViolationTableModel* m_model = nullptr;
    ViolationFilterProxyModel* m_proxy_model = nullptr;
    QTableView* m_table = nullptr;
    QComboBox* m_severity_filter = nullptr;
    QLineEdit* m_rule_filter = nullptr;
    QLineEdit* m_layer_filter = nullptr;
    QLineEdit* m_net_filter = nullptr;
    QLineEdit* m_search_filter = nullptr;
    QLabel* m_filter_summary = nullptr;
    QCheckBox* m_heatmap_visible = nullptr;
    QSlider* m_heatmap_opacity = nullptr;
    QLineEdit* m_violation_id = nullptr;
    QTextEdit* m_summary = nullptr;
    QTableWidget* m_metadata = nullptr;

    void apply_filter_widgets(const ViolationFilterState& state);
    void apply_filters_from_widgets();
    void update_filter_summary();
    void update_details();
};

} // namespace aegis::ui
