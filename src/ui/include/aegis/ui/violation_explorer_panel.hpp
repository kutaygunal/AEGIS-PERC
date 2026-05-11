#pragma once

#include "aegis/rules/violation.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QWidget>

class QAction;
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
    [[nodiscard]] int selected_violation_count() const;
    void select_rows(const std::vector<int>& rows);
    int select_violation_ids(const QStringList& ids);
    QString selected_violations_text() const;
    QString selected_violations_json_text() const;
    QString current_violation_details_for_copy() const;
    QString current_violation_id_for_copy() const;
    bool context_action_enabled(const QString& action_id) const;
    bool trigger_context_action(const QString& action_id);
    void set_heatmap_visible(bool visible);
    void set_heatmap_opacity(double opacity);
    [[nodiscard]] bool heatmap_visible() const;
    [[nodiscard]] double heatmap_opacity() const;

signals:
    void current_violation_changed();
    void filtered_violations_changed(aegis::rules::ViolationCollection violations);
    void heatmap_settings_changed(bool visible, double opacity);
    void trace_current_violation_requested();
    void center_current_violation_requested();
    void copy_current_violation_id_requested();
    void copy_current_violation_details_requested();
    void copy_selected_violations_requested();
    void export_selected_violations_requested();
    void navigate_current_violation_requested();
    // P13-005 cross-probing
    void violation_double_clicked(aegis::rules::Violation violation);
    void cross_probe_violation_requested(QString violation_id);
    void cross_probe_to_graph_requested(QString net_name);

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
    QAction* m_trace_action = nullptr;
    QAction* m_center_action = nullptr;
    QAction* m_copy_id_action = nullptr;
    QAction* m_copy_details_action = nullptr;
    QAction* m_navigate_related_action = nullptr;
    QAction* m_copy_selected_action = nullptr;
    QAction* m_export_selected_action = nullptr;
    QCheckBox* m_heatmap_visible = nullptr;
    QSlider* m_heatmap_opacity = nullptr;
    QLineEdit* m_violation_id = nullptr;
    QTextEdit* m_summary = nullptr;
    QTableWidget* m_metadata = nullptr;

    void apply_filter_widgets(const ViolationFilterState& state);
    void apply_filters_from_widgets();
    void update_filter_summary();
    void update_details();
    void update_context_action_state();
};

} // namespace aegis::ui
