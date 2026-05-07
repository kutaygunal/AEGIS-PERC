#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QMainWindow>
#include <cstddef>
#include <memory>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// Application shell with dockable workspace
//
// P1-007 acceptance:
//   - Qt6 dockable panels around a central placeholder canvas.
//   - Menu bar with File, View, Tools, Help placeholders.
//   - Window geometry and dock state persist across restarts via QSettings.
//   - No analysis logic inside the shell.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Test accessors --------------------------------------------------------
    void set_scene(UiScene scene);
    void set_violations(aegis::rules::ViolationCollection violations);
    void set_connectivity_graph(const aegis::graph::ConnectivityGraph* graph);

    bool has_central_widget() const;
    bool has_layout_canvas() const;
    bool has_menu_bar() const;
    int  dock_widget_count() const;
    int  layer_panel_count() const;
    int  selected_item_count() const;
    void select_scene_item_by_id(const QString& item_id);
    QStringList dock_widget_titles() const;
    QStringList layer_panel_names() const;
    bool is_layer_visible(const QString& layer_name) const;
    void set_layer_visible(const QString& layer_name, bool visible);
    int unresolved_violation_count() const;
    int violation_explorer_count() const;
    int graph_explorer_count() const;
    void select_violation_row(int row);
    bool search_graph_node(const QString& text);
    void set_graph_lod_limit(int value);
    bool request_trace_by_name(const QString& stable_name);
    bool request_trace_from_selection();
    bool request_trace_from_current_violation();
    void clear_trace();
    void focus_trace();
    void set_violation_filter_state(ViolationFilterState state);
    void clear_violation_filters();
    QString current_violation_id() const;
    QString violation_details_text() const;
    int violation_metadata_row_count() const;
    QString violation_filter_summary_text() const;
    QString report_preview_summary_text() const;
    QString report_preview_snapshot_status_text() const;
    void set_heatmap_visible(bool visible);
    void set_heatmap_opacity(double opacity);
    bool heatmap_visible() const;
    double heatmap_opacity() const;
    std::size_t heatmap_bucket_count() const;
    QString heatmap_empty_state_text() const;
    QString current_graph_node_name() const;
    QString graph_explorer_status_text() const;
    bool is_graph_explorer_visible() const;
    QPointF canvas_view_center() const;
    bool violation_overlays_visible() const;
    std::size_t visible_violation_overlay_count() const;
    bool has_active_trace() const;
    std::size_t traced_item_count() const;
    QString trace_status_text() const;
    QString properties_summary_text() const;
    bool grid_visible() const;
    QStringList workspace_action_ids() const;
    bool workspace_action_enabled(const QString& action_id) const;
    bool workspace_action_checked(const QString& action_id) const;
    QString workspace_action_shortcut_text(const QString& action_id) const;
    QString workspace_action_tooltip(const QString& action_id) const;
    bool trigger_workspace_action(const QString& action_id);
    void set_performance_metrics_visible(bool visible);
    bool performance_metrics_visible() const;
    QString performance_metrics_text() const;
    std::size_t canvas_lod_cache_item_count() const;

    // Persistence hooks (public for testability)
    void restore_window_state();
    void save_window_state();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void setup_ui();
    void setup_actions();
    void setup_menus();
    void setup_toolbar();
    void setup_dock_panels();
    void update_action_states();
};

} // namespace aegis::ui
