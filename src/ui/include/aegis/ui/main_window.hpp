#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/activity_log_panel.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QMainWindow>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class QDragEnterEvent;
class QDropEvent;

namespace aegis::orchestration {
struct JobProgressSnapshot;
}

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
    bool is_dock_widget_visible(const QString& title) const;
    QStringList layer_panel_names() const;
    bool is_layer_visible(const QString& layer_name) const;
    void set_layer_visible(const QString& layer_name, bool visible);
    int unresolved_violation_count() const;
    int violation_explorer_count() const;
    int selected_violation_count() const;
    int graph_explorer_count() const;
    void select_violation_row(int row);
    void select_violation_rows(const std::vector<int>& rows);
    bool search_graph_node(const QString& text);
    void set_graph_lod_limit(int value);
    bool request_trace_by_name(const QString& stable_name);
    bool request_trace_from_selection();
    bool request_trace_from_current_violation();
    void clear_trace();
    void focus_trace();
    void set_violation_filter_state(ViolationFilterState state);
    [[nodiscard]] ViolationFilterState violation_filter_state() const;
    bool save_violation_filter_preset(const QString& name);
    bool apply_violation_filter_preset(const QString& name);
    bool rename_violation_filter_preset(const QString& old_name, const QString& new_name);
    bool delete_violation_filter_preset(const QString& name);
    [[nodiscard]] QStringList violation_filter_preset_names() const;
    bool save_workspace_view(const QString& name);
    bool apply_workspace_view(const QString& name);
    bool rename_workspace_view(const QString& old_name, const QString& new_name);
    bool delete_workspace_view(const QString& name);
    [[nodiscard]] QStringList workspace_view_names() const;
    void clear_violation_filters();
    QString current_violation_id() const;
    QString violation_details_text() const;
    int violation_metadata_row_count() const;
    bool violation_context_action_enabled(const QString& action_id) const;
    bool trigger_violation_context_action(const QString& action_id);
    QString violation_filter_summary_text() const;
    QString report_preview_summary_text() const;
    QString report_preview_snapshot_status_text() const;
    QString report_preview_last_action_status_text() const;
    bool report_preview_refresh_enabled() const;
    bool report_preview_copy_summary_enabled() const;
    bool report_preview_copy_snapshot_enabled() const;
    bool report_preview_export_json_enabled() const;
    bool report_preview_export_html_enabled() const;
    void trigger_report_preview_refresh();
    void trigger_report_preview_copy_summary();
    void trigger_report_preview_copy_snapshot();
    void trigger_report_preview_export_json();
    void trigger_report_preview_export_html();
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
    QString canvas_empty_state_text() const;
    bool violation_overlays_visible() const;
    std::size_t visible_violation_overlay_count() const;
    bool has_active_trace() const;
    std::size_t traced_item_count() const;
    QString trace_status_text() const;
    QString trace_request_text() const;
    bool trace_request_enabled() const;
    bool trace_clear_enabled() const;
    bool trace_focus_enabled() const;
    QString properties_summary_text() const;
    bool grid_visible() const;
    QStringList workspace_action_ids() const;
    bool workspace_action_enabled(const QString& action_id) const;
    bool workspace_action_checked(const QString& action_id) const;
    QString workspace_action_shortcut_text(const QString& action_id) const;
    QString workspace_action_tooltip(const QString& action_id) const;
    bool trigger_workspace_action(const QString& action_id);
    bool is_about_dialog_visible() const;
    bool is_documentation_dialog_visible() const;
    QString documentation_summary_text() const;
    bool onboarding_visible() const;
    QString onboarding_text() const;
    void dismiss_onboarding();
    bool is_sample_browser_visible() const;
    QStringList bundled_sample_ids() const;
    bool load_bundled_sample(const QString& sample_id);
    QString last_status_message() const;
    int activity_log_entry_count() const;
    QString activity_log_entry_text(int index) const;
    QStringList activity_log_entries() const;
    int activity_log_max_entries() const;
    bool is_import_dialog_visible() const;
    void set_import_picker_for_tests(std::function<QStringList(QWidget*)> picker);
    bool import_project_paths(const QStringList& paths);
    bool override_import_artifact_role(const QString& artifact_path, const QString& role_name);
    QString import_validation_summary_text() const;
    int import_artifact_row_count() const;
    QString import_artifact_role_text(int row) const;
    QString import_artifact_status_text(int row) const;
    bool set_import_artifact_role_from_ui(int row, const QString& role_name);
    bool select_import_artifact_row(int row);
    QString current_import_artifact_path() const;
    bool trigger_import_artifact_show_related_violations();
    bool import_load_action_enabled() const;
    bool trigger_import_load_action();
    bool has_loaded_import_package() const;
    QString loaded_import_project_name() const;
    QString workspace_summary_text() const;
    int diagnostics_entry_count() const;
    QString diagnostics_details_text() const;
    bool set_diagnostics_severity_filter(const QString& severity);
    bool select_diagnostics_row(int row);
    bool trigger_diagnostics_show_related_violations();
    QStringList recent_project_paths() const;
    bool reopen_recent_project(int index);
    bool reopen_last_session_enabled() const;
    void set_reopen_last_session_enabled(bool enabled);
    QStringList import_detected_roles() const;
    int import_diagnostic_count() const;
    bool import_has_blockers() const;
    void set_performance_metrics_visible(bool visible);
    bool performance_metrics_visible() const;
    QString performance_metrics_text() const;
    std::size_t canvas_lod_cache_item_count() const;
    QString job_progress_text() const;
    bool has_active_job() const;
    int job_history_count() const;
    QString job_history_summary_text(int index) const;
    bool select_job_history_row(int row);
    QString job_history_details_text() const;
    bool job_history_open_json_enabled() const;
    bool job_history_open_html_enabled() const;
    bool trigger_job_history_open_json();
    bool trigger_job_history_open_html();
    int job_history_max_entries() const;
    void set_job_pipeline_artificial_delay_for_tests(int milliseconds);
    void set_report_opener_for_tests(std::function<bool(const QString&)> opener);
    void set_report_export_path_picker_for_tests(std::function<QString(const QString&)> picker);

    // Persistence hooks (public for testability)
    void restore_window_state();
    void save_window_state();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void setup_ui();
    void setup_actions();
    void setup_menus();
    void setup_toolbar();
    void setup_dock_panels();
    void update_action_states();
    void update_trace_controls();
    void refresh_workspace_summary();
    void refresh_diagnostics_panel();
    void refresh_onboarding_panel();
    bool navigate_current_violation_relationships();
    bool select_related_violations_for_current_diagnostic();
    bool select_related_violations_for_current_artifact();
    void refresh_recent_project_actions();
    void refresh_filter_preset_menu();
    void refresh_workspace_view_menu();
    bool reopen_project_from_path(const QString& path, bool mark_as_last_session);
    void show_status_message(const QString& message, int timeout_ms = 0);
    void append_activity_log(const QString& message, ActivityLogSeverity severity = ActivityLogSeverity::Info);
    void publish_ui_notification(const QString& message,
                                 ActivityLogSeverity severity = ActivityLogSeverity::Info,
                                 int timeout_ms = 0,
                                 bool update_trace_panel = false);
    void publish_trace_feedback(const QString& message, ActivityLogSeverity severity, int timeout_ms = 0);
    void execute_run_checks();
    bool export_report_preview(bool html_export);
    bool start_imported_run_checks(bool is_retry);
    bool can_retry_last_job() const;
    void update_job_progress_ui(const aegis::orchestration::JobProgressSnapshot& snapshot);
    void refresh_job_history_panel();
    bool open_selected_job_history_report(bool html_report);
    bool open_import_review_dialog(const QStringList& paths, bool from_drop);
    void refresh_import_review();
    void apply_import_package();
    void finalize_active_job();
};

} // namespace aegis::ui
