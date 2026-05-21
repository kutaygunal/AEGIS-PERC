#include "aegis/ui/main_window.hpp"
#include "main_window_utils.hpp"
#include "job_workflow_controller.hpp"
#include "diagnostics_report_controller.hpp"
#include "aegis/ui/activity_log_panel.hpp"
#include "aegis/ui/layer_panel.hpp"
#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/hierarchy_browser_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/report_preview_panel.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/ui/trace_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/current_activity_application.hpp"
#include "aegis/graph/power_intent_application.hpp"
#include "aegis/parsing/current_activity.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/power_intent.hpp"
#include "aegis/orchestration/job_pipeline.hpp"
#include "aegis/reporting/report_generator.hpp"
#include "aegis/rules/electrical_rules.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"
#include "aegis/storage/import_validation.hpp"
#include "aegis/storage/imported_design_session.hpp"
#include "aegis/storage/project_package.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_impl(std::make_unique<Impl>())
    , m_jobs(std::make_unique<JobWorkflowController>(*this, *m_impl, this))
    , m_diagnostics(std::make_unique<DiagnosticsReportController>(*this, *m_impl, this))
{
    setup_ui();
    restore_window_state();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// UI Setup
// ---------------------------------------------------------------------------
void MainWindow::setup_ui()
{
    setWindowTitle("AEGIS-PERC");
    resize(1280, 720);
    setAcceptDrops(true);

    // Central reusable layout canvas. It owns rendering state only; scene data
    // is supplied through the UI scene adapter.
    m_impl->selection_model = new SelectionModel(this);
    m_impl->canvas = configure_accessible_widget(new LayoutCanvas(this),
                                                 "Layout Canvas",
                                                 "Interactive layout canvas for selection, tracing, and viewport navigation",
                                                 "Primary workspace canvas for the active design scene");
    m_impl->canvas->setObjectName("LayoutCanvas");
    m_impl->canvas->setFocusPolicy(Qt::StrongFocus);
    m_impl->canvas->set_selection_model(m_impl->selection_model);
    connect(m_impl->canvas, &LayoutCanvas::cursor_position_changed, this,
            [this](const QPointF& scene_pos, double zoom) {
                statusBar()->showMessage(QString("X: %1  Y: %2  Zoom: %3%")
                                             .arg(scene_pos.x(), 0, 'f', 2)
                                             .arg(scene_pos.y(), 0, 'f', 2)
                                             .arg(zoom * 100.0, 0, 'f', 1));
            });
    connect(m_impl->canvas, &LayoutCanvas::viewport_changed, this,
            [this](double zoom, const QPointF&) {
                statusBar()->showMessage(QString("Zoom: %1%").arg(zoom * 100.0, 0, 'f', 1));
            });
    connect(m_impl->canvas, &LayoutCanvas::performance_metrics_changed, this,
            [this]() {
                if (m_impl->performance_status_label != nullptr) {
                    m_impl->performance_status_label->setText(
                        m_impl->canvas != nullptr ? m_impl->canvas->performance_metrics_text() : QString{});
                }
            });
    setCentralWidget(m_impl->canvas);

    // Status bar
    m_impl->job_progress_label = configure_accessible_widget(new QLabel(this),
                                                             "Job Progress Status",
                                                             "Current local workflow job progress",
                                                             "Status-bar summary for the active local workflow job");
    m_impl->job_progress_label->setObjectName("JobProgressStatusLabel");
    m_impl->job_progress_label->setVisible(false);
    statusBar()->addPermanentWidget(m_impl->job_progress_label);
    m_impl->performance_status_label = configure_accessible_widget(new QLabel(this),
                                                                   "Performance Metrics Status",
                                                                   "Current layout canvas performance metrics",
                                                                   "Status-bar performance metrics for the layout canvas");
    m_impl->performance_status_label->setVisible(false);
    statusBar()->addPermanentWidget(m_impl->performance_status_label);
    show_status_message("Ready");

    setup_actions();

    // Menus
    setup_menus();
    setup_toolbar();

    // Dock panels
    setup_dock_panels();
    update_action_states();
}

void MainWindow::execute_run_checks()
{
    m_jobs->execute_run_checks();
}

bool MainWindow::export_report_preview(bool html_export)
{
    return m_diagnostics->export_report_preview(html_export);
}
bool MainWindow::start_imported_run_checks(bool is_retry)
{
    return m_jobs->start_imported_run_checks(is_retry);
}

bool MainWindow::can_retry_last_job() const
{
    return m_jobs->can_retry_last_job();
}

void MainWindow::update_job_progress_ui(const aegis::orchestration::JobProgressSnapshot& snapshot)
{
    m_jobs->update_job_progress_ui(snapshot);
}

void MainWindow::refresh_job_history_panel()
{
    m_jobs->refresh_job_history_panel();
}

bool MainWindow::open_selected_job_history_report(bool html_report)
{
    return m_jobs->open_selected_job_history_report(html_report);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    save_window_state();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (event == nullptr || event->mimeData() == nullptr || !event->mimeData()->hasUrls()) {
        QMainWindow::dropEvent(event);
        return;
    }

    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile());
        }
    }
    event->acceptProposedAction();
    if (!paths.isEmpty()) {
        Q_UNUSED(open_import_review_dialog(paths, true));
    }
}

void MainWindow::set_scene(UiScene scene)
{
    m_impl->owned_graph.reset();
    m_impl->sample_mode_active = false;
    m_impl->last_coverage_summary.reset();
    set_connectivity_graph(nullptr);

    if (m_impl->layer_panel != nullptr) {
        m_impl->layer_panel->set_layers(scene.layers);
    }
    if (m_impl->properties_panel != nullptr) {
        m_impl->properties_panel->set_scene(scene);
    }
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->set_scene(scene);
    }
    if (m_impl->selection_model != nullptr) {
        m_impl->selection_model->clear();
    }
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_scene(std::move(scene));
        if (m_impl->report_preview != nullptr) {
            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
        }
    }
    refresh_workspace_summary();
    update_action_states();
}

void MainWindow::set_violations(aegis::rules::ViolationCollection violations)
{
    m_diagnostics->set_violations(std::move(violations));
}
void MainWindow::set_connectivity_graph(const aegis::graph::ConnectivityGraph* graph)
{
    m_impl->current_graph = graph;
    m_impl->trace_adapter.set_graph(graph);
    if (m_impl->graph_explorer != nullptr) {
        m_impl->graph_explorer->set_graph(graph);
    }
    refresh_workspace_summary();
    update_action_states();
}

// ---------------------------------------------------------------------------
// Test accessors
// ---------------------------------------------------------------------------
bool MainWindow::has_central_widget() const
{
    return centralWidget() != nullptr;
}

bool MainWindow::has_layout_canvas() const
{
    return m_impl->canvas != nullptr && centralWidget() == m_impl->canvas;
}

bool MainWindow::has_menu_bar() const
{
    return m_impl->menu_bar != nullptr && !m_impl->menu_bar->isHidden();
}

int MainWindow::dock_widget_count() const
{
    return static_cast<int>(m_impl->docks.size());
}

int MainWindow::layer_panel_count() const
{
    return m_impl->layer_panel != nullptr ? m_impl->layer_panel->layer_count() : 0;
}

int MainWindow::selected_item_count() const
{
    return m_impl->selection_model != nullptr
        ? static_cast<int>(m_impl->selection_model->selected_ids().size())
        : 0;
}

void MainWindow::select_scene_item_by_id(const QString& item_id)
{
    if (m_impl->selection_model != nullptr) {
        m_impl->selection_model->select_only(item_id.toStdString());
    }
}

QStringList MainWindow::dock_widget_titles() const
{
    QStringList titles;
    for (const auto* dock : m_impl->docks) {
        if (dock) titles.append(dock->windowTitle());
    }
    return titles;
}

bool MainWindow::is_dock_widget_visible(const QString& title) const
{
    for (const auto* dock : m_impl->docks) {
        if (dock != nullptr && dock->windowTitle() == title) {
            return !dock->isHidden();
        }
    }
    return false;
}

QStringList MainWindow::layer_panel_names() const
{
    return m_impl->layer_panel != nullptr ? m_impl->layer_panel->layer_names() : QStringList{};
}

bool MainWindow::is_layer_visible(const QString& layer_name) const
{
    return m_impl->layer_panel != nullptr && m_impl->layer_panel->layer_visible(layer_name);
}

void MainWindow::set_layer_visible(const QString& layer_name, bool visible)
{
    if (m_impl->layer_panel != nullptr) {
        m_impl->layer_panel->set_layer_visible(layer_name, visible);
    }
}

int MainWindow::unresolved_violation_count() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->unresolved_violation_count() : 0;
}

int MainWindow::violation_explorer_count() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->violation_count() : 0;
}

int MainWindow::selected_violation_count() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->selected_violation_count() : 0;
}

int MainWindow::graph_explorer_count() const
{
    return m_impl->graph_explorer != nullptr ? m_impl->graph_explorer->visible_item_count() : 0;
}

int MainWindow::hierarchy_browser_count() const
{
    return m_impl->hierarchy_browser != nullptr ? m_impl->hierarchy_browser->visible_item_count() : 0;
}

QString MainWindow::hierarchy_browser_status_text() const
{
    return m_impl->hierarchy_browser != nullptr ? m_impl->hierarchy_browser->status_text() : QString{};
}

bool MainWindow::search_hierarchy_browser(const QString& text)
{
    return m_impl->hierarchy_browser != nullptr && m_impl->hierarchy_browser->search_and_select(text);
}

bool MainWindow::select_hierarchy_browser_by_stable_id(const QString& stable_id)
{
    if (m_impl->hierarchy_browser != nullptr) {
        m_impl->hierarchy_browser->select_by_stable_id(stable_id);
        return m_impl->hierarchy_browser->current_stable_id() == stable_id;
    }
    return false;
}

QString MainWindow::current_hierarchy_browser_id() const
{
    return m_impl->hierarchy_browser != nullptr ? m_impl->hierarchy_browser->current_stable_id() : QString{};
}

bool MainWindow::is_hierarchy_browser_visible() const
{
    return m_impl->hierarchy_dock != nullptr && !m_impl->hierarchy_dock->isHidden();
}

void MainWindow::select_violation_row(int row)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->select_row(row);
    }
}

void MainWindow::select_violation_rows(const std::vector<int>& rows)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->select_rows(rows);
    }
}

bool MainWindow::search_graph_node(const QString& text)
{
    return m_impl->graph_explorer != nullptr && m_impl->graph_explorer->search_and_select(text);
}

void MainWindow::set_graph_lod_limit(int value)
{
    if (m_impl->graph_explorer != nullptr) {
        m_impl->graph_explorer->set_lod_limit(value);
    }
}

bool MainWindow::request_trace_by_name(const QString& stable_name)
{
    return m_diagnostics->request_trace_by_name(stable_name);
}
bool MainWindow::request_trace_from_selection()
{
    return m_diagnostics->request_trace_from_selection();
}
bool MainWindow::request_trace_from_current_violation()
{
    return m_diagnostics->request_trace_from_current_violation();
}
void MainWindow::clear_trace()
{
    m_diagnostics->clear_trace();
}
void MainWindow::focus_trace()
{
    m_diagnostics->focus_trace();
}
void MainWindow::set_violation_filter_state(ViolationFilterState state)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->set_filter_state(std::move(state));
    }
}

ViolationFilterState MainWindow::violation_filter_state() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->filter_state() : ViolationFilterState{};
}

bool MainWindow::save_violation_filter_preset(const QString& name)
{
    const QString normalized = normalize_saved_name(name);
    if (normalized.isEmpty()) {
        return false;
    }

    const auto it = std::find_if(m_impl->filter_presets.begin(), m_impl->filter_presets.end(), [&normalized](const SavedFilterPreset& preset) {
        return saved_name_matches(preset.name, normalized);
    });
    if (it != m_impl->filter_presets.end()) {
        it->name = normalized;
        it->state = violation_filter_state();
    } else {
        m_impl->filter_presets.push_back(SavedFilterPreset{normalized, violation_filter_state()});
    }
    save_window_state();
    refresh_filter_preset_menu();
    update_action_states();
    return true;
}

bool MainWindow::apply_violation_filter_preset(const QString& name)
{
    const auto it = std::find_if(m_impl->filter_presets.begin(), m_impl->filter_presets.end(), [&name](const SavedFilterPreset& preset) {
        return saved_name_matches(preset.name, name);
    });
    if (it == m_impl->filter_presets.end()) {
        return false;
    }
    set_violation_filter_state(it->state);
    return true;
}

bool MainWindow::rename_violation_filter_preset(const QString& old_name, const QString& new_name)
{
    const QString normalized = normalize_saved_name(new_name);
    if (normalized.isEmpty()) {
        return false;
    }

    const auto it = std::find_if(m_impl->filter_presets.begin(), m_impl->filter_presets.end(), [&old_name](const SavedFilterPreset& preset) {
        return saved_name_matches(preset.name, old_name);
    });
    if (it == m_impl->filter_presets.end()) {
        return false;
    }

    auto duplicate = std::find_if(m_impl->filter_presets.begin(), m_impl->filter_presets.end(), [&normalized, &it](const SavedFilterPreset& preset) {
        return &preset != &(*it) && saved_name_matches(preset.name, normalized);
    });
    if (duplicate != m_impl->filter_presets.end()) {
        duplicate->state = it->state;
        m_impl->filter_presets.erase(it);
    } else {
        it->name = normalized;
    }

    save_window_state();
    refresh_filter_preset_menu();
    update_action_states();
    return true;
}

bool MainWindow::delete_violation_filter_preset(const QString& name)
{
    const auto original_size = m_impl->filter_presets.size();
    std::erase_if(m_impl->filter_presets, [&name](const SavedFilterPreset& preset) {
        return saved_name_matches(preset.name, name);
    });
    if (m_impl->filter_presets.size() == original_size) {
        return false;
    }
    save_window_state();
    refresh_filter_preset_menu();
    update_action_states();
    return true;
}

QStringList MainWindow::violation_filter_preset_names() const
{
    return saved_entry_names(m_impl->filter_presets);
}

bool MainWindow::save_workspace_view(const QString& name)
{
    const QString normalized = normalize_saved_name(name);
    if (normalized.isEmpty()) {
        return false;
    }

    SavedWorkspaceView snapshot;
    snapshot.name = normalized;
    snapshot.dock_state = saveState();
    snapshot.grid_visible = grid_visible();
    snapshot.overlays_visible = violation_overlays_visible();
    snapshot.heatmap_visible = heatmap_visible();
    snapshot.heatmap_opacity = heatmap_opacity();
    snapshot.performance_metrics_visible = performance_metrics_visible();

    const auto it = std::find_if(m_impl->workspace_views.begin(), m_impl->workspace_views.end(), [&normalized](const SavedWorkspaceView& view) {
        return saved_name_matches(view.name, normalized);
    });
    if (it != m_impl->workspace_views.end()) {
        *it = snapshot;
    } else {
        m_impl->workspace_views.push_back(std::move(snapshot));
    }
    save_window_state();
    refresh_workspace_view_menu();
    update_action_states();
    return true;
}

bool MainWindow::apply_workspace_view(const QString& name)
{
    const auto it = std::find_if(m_impl->workspace_views.begin(), m_impl->workspace_views.end(), [&name](const SavedWorkspaceView& view) {
        return saved_name_matches(view.name, name);
    });
    if (it == m_impl->workspace_views.end()) {
        return false;
    }

    if (!it->dock_state.isEmpty()) {
        restoreState(it->dock_state);
    }
    if (m_impl->actions.contains("toggle_grid") && m_impl->actions.at("toggle_grid") != nullptr) {
        m_impl->actions.at("toggle_grid")->setChecked(it->grid_visible);
    }
    if (m_impl->actions.contains("toggle_overlays") && m_impl->actions.at("toggle_overlays") != nullptr) {
        m_impl->actions.at("toggle_overlays")->setChecked(it->overlays_visible);
    }
    set_heatmap_visible(it->heatmap_visible);
    set_heatmap_opacity(it->heatmap_opacity);
    set_performance_metrics_visible(it->performance_metrics_visible);
    update_action_states();
    return true;
}

bool MainWindow::rename_workspace_view(const QString& old_name, const QString& new_name)
{
    const QString normalized = normalize_saved_name(new_name);
    if (normalized.isEmpty()) {
        return false;
    }

    const auto it = std::find_if(m_impl->workspace_views.begin(), m_impl->workspace_views.end(), [&old_name](const SavedWorkspaceView& view) {
        return saved_name_matches(view.name, old_name);
    });
    if (it == m_impl->workspace_views.end()) {
        return false;
    }

    auto duplicate = std::find_if(m_impl->workspace_views.begin(), m_impl->workspace_views.end(), [&normalized, &it](const SavedWorkspaceView& view) {
        return &view != &(*it) && saved_name_matches(view.name, normalized);
    });
    if (duplicate != m_impl->workspace_views.end()) {
        *duplicate = *it;
        duplicate->name = normalized;
        m_impl->workspace_views.erase(it);
    } else {
        it->name = normalized;
    }

    save_window_state();
    refresh_workspace_view_menu();
    update_action_states();
    return true;
}

bool MainWindow::delete_workspace_view(const QString& name)
{
    const auto original_size = m_impl->workspace_views.size();
    std::erase_if(m_impl->workspace_views, [&name](const SavedWorkspaceView& view) {
        return saved_name_matches(view.name, name);
    });
    if (m_impl->workspace_views.size() == original_size) {
        return false;
    }
    save_window_state();
    refresh_workspace_view_menu();
    update_action_states();
    return true;
}

QStringList MainWindow::workspace_view_names() const
{
    return saved_entry_names(m_impl->workspace_views);
}

bool MainWindow::save_viewport_preset(const QString& name)
{
    const QString normalized = normalize_saved_name(name);
    if (normalized.isEmpty() || m_impl->canvas == nullptr) {
        return false;
    }

    SavedViewportPreset preset;
    preset.name = normalized;
    preset.zoom_level = m_impl->canvas->zoom_level();
    const QPointF center = m_impl->canvas->view_center();
    preset.view_center_x = center.x();
    preset.view_center_y = center.y();

    const auto it = std::find_if(m_impl->viewport_presets.begin(), m_impl->viewport_presets.end(),
                                 [&normalized](const SavedViewportPreset& p) {
                                     return saved_name_matches(p.name, normalized);
                                 });
    if (it != m_impl->viewport_presets.end()) {
        *it = preset;
    } else {
        m_impl->viewport_presets.push_back(std::move(preset));
    }
    save_window_state();
    refresh_viewport_preset_menu();
    update_action_states();
    return true;
}

bool MainWindow::apply_viewport_preset(const QString& name)
{
    const auto it = std::find_if(m_impl->viewport_presets.begin(), m_impl->viewport_presets.end(),
                                 [&name](const SavedViewportPreset& p) {
                                     return saved_name_matches(p.name, name);
                                 });
    if (it == m_impl->viewport_presets.end() || m_impl->canvas == nullptr) {
        return false;
    }

    aegis::ui::ViewportState state;
    state.zoom_level = it->zoom_level;
    state.view_center = QPointF(it->view_center_x, it->view_center_y);
    m_impl->canvas->restore_viewport_state(state);
    return true;
}

bool MainWindow::rename_viewport_preset(const QString& old_name, const QString& new_name)
{
    const QString normalized = normalize_saved_name(new_name);
    if (normalized.isEmpty()) {
        return false;
    }

    const auto it = std::find_if(m_impl->viewport_presets.begin(), m_impl->viewport_presets.end(),
                                 [&old_name](const SavedViewportPreset& p) {
                                     return saved_name_matches(p.name, old_name);
                                 });
    if (it == m_impl->viewport_presets.end()) {
        return false;
    }

    auto duplicate = std::find_if(m_impl->viewport_presets.begin(), m_impl->viewport_presets.end(),
                                  [&normalized, &it](const SavedViewportPreset& p) {
                                      return &p != &(*it) && saved_name_matches(p.name, normalized);
                                  });
    if (duplicate != m_impl->viewport_presets.end()) {
        *duplicate = *it;
        duplicate->name = normalized;
        m_impl->viewport_presets.erase(it);
    } else {
        it->name = normalized;
    }

    save_window_state();
    refresh_viewport_preset_menu();
    update_action_states();
    return true;
}

bool MainWindow::delete_viewport_preset(const QString& name)
{
    const auto original_size = m_impl->viewport_presets.size();
    std::erase_if(m_impl->viewport_presets, [&name](const SavedViewportPreset& p) {
        return saved_name_matches(p.name, name);
    });
    if (m_impl->viewport_presets.size() == original_size) {
        return false;
    }
    save_window_state();
    refresh_viewport_preset_menu();
    update_action_states();
    return true;
}

QStringList MainWindow::viewport_preset_names() const
{
    return saved_entry_names(m_impl->viewport_presets);
}

bool MainWindow::apply_visibility_preset(const QString& preset_name)
{
    if (m_impl->layer_panel == nullptr) {
        return false;
    }
    VisibilityPreset preset = VisibilityPreset::All;
    if (preset_name.compare("Routing Only", Qt::CaseInsensitive) == 0) {
        preset = VisibilityPreset::RoutingOnly;
    } else if (preset_name.compare("Macros Only", Qt::CaseInsensitive) == 0) {
        preset = VisibilityPreset::MacrosOnly;
    } else if (preset_name.compare("Pins & Ports", Qt::CaseInsensitive) == 0) {
        preset = VisibilityPreset::PinsAndPorts;
    } else if (preset_name.compare("Power Focused", Qt::CaseInsensitive) == 0) {
        preset = VisibilityPreset::PowerFocused;
    } else if (preset_name.compare("Violation Review", Qt::CaseInsensitive) == 0) {
        preset = VisibilityPreset::ViolationReview;
    }
    m_impl->layer_panel->apply_visibility_preset(preset);
    return true;
}

QStringList MainWindow::visibility_preset_names() const
{
    return QStringList{
        "All Layers",
        "Routing Only",
        "Macros Only",
        "Pins & Ports",
        "Power Focused",
        "Violation Review",
    };
}

void MainWindow::set_coloring_mode(const QString& mode_name)
{
    if (m_impl->canvas == nullptr) {
        return;
    }
    ColoringMode mode = ColoringMode::LayerColor;
    if (mode_name.compare("Object Type", Qt::CaseInsensitive) == 0) {
        mode = ColoringMode::ObjectType;
    } else if (mode_name.compare("Domain", Qt::CaseInsensitive) == 0) {
        mode = ColoringMode::Domain;
    } else if (mode_name.compare("Violation Context", Qt::CaseInsensitive) == 0) {
        mode = ColoringMode::ViolationContext;
    }
    m_impl->canvas->set_coloring_mode(mode);
}

QString MainWindow::coloring_mode_name() const
{
    if (m_impl->canvas == nullptr) {
        return "Layer Color";
    }
    switch (m_impl->canvas->coloring_mode()) {
        case ColoringMode::ObjectType: return "Object Type";
        case ColoringMode::Domain: return "Domain";
        case ColoringMode::ViolationContext: return "Violation Context";
        default: return "Layer Color";
    }
}

void MainWindow::clear_violation_filters()
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->clear_filters();
    }
}

QString MainWindow::current_violation_id() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->current_violation_id() : QString{};
}

QString MainWindow::violation_details_text() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->details_summary_text() : QString{};
}

int MainWindow::violation_metadata_row_count() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->metadata_row_count() : 0;
}

bool MainWindow::violation_context_action_enabled(const QString& action_id) const
{
    return m_impl->violation_explorer != nullptr && m_impl->violation_explorer->context_action_enabled(action_id);
}

bool MainWindow::trigger_violation_context_action(const QString& action_id)
{
    return m_impl->violation_explorer != nullptr && m_impl->violation_explorer->trigger_context_action(action_id);
}

QString MainWindow::violation_filter_summary_text() const
{
    return m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->filter_summary_text() : QString{};
}

QString MainWindow::report_preview_summary_text() const
{
    return m_impl->report_preview != nullptr ? m_impl->report_preview->summary_text() : QString{};
}

QString MainWindow::report_preview_snapshot_status_text() const
{
    return m_impl->report_preview != nullptr ? m_impl->report_preview->snapshot_status_text() : QString{};
}

QString MainWindow::report_preview_last_action_status_text() const
{
    return m_impl->report_preview != nullptr ? m_impl->report_preview->last_action_status_text() : QString{};
}

bool MainWindow::report_preview_refresh_enabled() const
{
    return m_impl->report_preview != nullptr && m_impl->report_preview->refresh_enabled();
}

bool MainWindow::report_preview_copy_summary_enabled() const
{
    return m_impl->report_preview != nullptr && m_impl->report_preview->copy_summary_enabled();
}

bool MainWindow::report_preview_copy_snapshot_enabled() const
{
    return m_impl->report_preview != nullptr && m_impl->report_preview->copy_snapshot_enabled();
}

bool MainWindow::report_preview_export_json_enabled() const
{
    return m_impl->report_preview != nullptr && m_impl->report_preview->export_json_enabled();
}

bool MainWindow::report_preview_export_html_enabled() const
{
    return m_impl->report_preview != nullptr && m_impl->report_preview->export_html_enabled();
}

void MainWindow::trigger_report_preview_refresh()
{
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->trigger_refresh();
    }
}

void MainWindow::trigger_report_preview_copy_summary()
{
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->trigger_copy_summary();
    }
}

void MainWindow::trigger_report_preview_copy_snapshot()
{
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->trigger_copy_snapshot();
    }
}

void MainWindow::trigger_report_preview_export_json()
{
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->trigger_export_json();
    }
}

void MainWindow::trigger_report_preview_export_html()
{
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->trigger_export_html();
    }
}

void MainWindow::set_heatmap_visible(bool visible)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->set_heatmap_visible(visible);
    } else if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_heatmap_visible(visible);
    }
}

void MainWindow::set_heatmap_opacity(double opacity)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->set_heatmap_opacity(opacity);
    } else if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_heatmap_opacity(opacity);
    }
}

bool MainWindow::heatmap_visible() const
{
    return m_impl->canvas != nullptr && m_impl->canvas->heatmap_visible();
}

double MainWindow::heatmap_opacity() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->heatmap_opacity() : 0.0;
}

std::size_t MainWindow::heatmap_bucket_count() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->heatmap_bucket_count() : 0;
}

QString MainWindow::heatmap_empty_state_text() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->heatmap_empty_state_text() : QString{};
}

QString MainWindow::current_graph_node_name() const
{
    return m_impl->graph_explorer != nullptr ? m_impl->graph_explorer->current_node_name() : QString{};
}

QString MainWindow::graph_explorer_status_text() const
{
    return m_impl->graph_explorer != nullptr ? m_impl->graph_explorer->status_text() : QString{};
}

bool MainWindow::is_graph_explorer_visible() const
{
    return m_impl->graph_dock != nullptr && !m_impl->graph_dock->isHidden();
}

QPointF MainWindow::canvas_view_center() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->view_center() : QPointF{};
}

QString MainWindow::canvas_empty_state_text() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->empty_state_text() : QString{};
}

bool MainWindow::violation_overlays_visible() const
{
    return m_impl->canvas != nullptr && m_impl->canvas->violation_overlays_visible();
}

std::size_t MainWindow::visible_violation_overlay_count() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->violation_count() : 0;
}

bool MainWindow::has_active_trace() const
{
    return m_impl->canvas != nullptr && m_impl->canvas->has_active_trace();
}

std::size_t MainWindow::traced_item_count() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->traced_item_count() : 0;
}

QString MainWindow::trace_status_text() const
{
    return m_impl->trace_panel != nullptr ? m_impl->trace_panel->status_text() : QString{};
}

QString MainWindow::trace_request_text() const
{
    return m_impl->trace_panel != nullptr ? m_impl->trace_panel->request_text() : QString{};
}

bool MainWindow::trace_request_enabled() const
{
    return m_impl->trace_panel != nullptr && m_impl->trace_panel->request_enabled();
}

bool MainWindow::trace_clear_enabled() const
{
    return m_impl->trace_panel != nullptr && m_impl->trace_panel->clear_enabled();
}

bool MainWindow::trace_focus_enabled() const
{
    return m_impl->trace_panel != nullptr && m_impl->trace_panel->focus_enabled();
}

QString MainWindow::properties_summary_text() const
{
    return m_impl->properties_panel != nullptr ? m_impl->properties_panel->summary_text() : QString{};
}

bool MainWindow::grid_visible() const
{
    return m_impl->canvas != nullptr && m_impl->canvas->grid_visible();
}

QStringList MainWindow::workspace_action_ids() const
{
    QStringList ids;
    for (const auto& [id, action] : m_impl->actions) {
        Q_UNUSED(action);
        ids.append(id);
    }
    return ids;
}

bool MainWindow::workspace_action_enabled(const QString& action_id) const
{
    const auto it = m_impl->actions.find(action_id);
    return it != m_impl->actions.end() && it->second != nullptr && it->second->isEnabled();
}

bool MainWindow::workspace_action_checked(const QString& action_id) const
{
    const auto it = m_impl->actions.find(action_id);
    return it != m_impl->actions.end() && it->second != nullptr && it->second->isChecked();
}

QString MainWindow::workspace_action_shortcut_text(const QString& action_id) const
{
    const auto it = m_impl->actions.find(action_id);
    return it != m_impl->actions.end() && it->second != nullptr ? it->second->shortcut().toString() : QString{};
}

QString MainWindow::workspace_action_tooltip(const QString& action_id) const
{
    const auto it = m_impl->actions.find(action_id);
    return it != m_impl->actions.end() && it->second != nullptr ? it->second->toolTip() : QString{};
}

bool MainWindow::trigger_workspace_action(const QString& action_id)
{
    const auto it = m_impl->actions.find(action_id);
    if (it == m_impl->actions.end() || it->second == nullptr || !it->second->isEnabled()) {
        return false;
    }
    it->second->trigger();
    return true;
}

bool MainWindow::is_about_dialog_visible() const
{
    return m_impl->about_dialog != nullptr && m_impl->about_dialog->isVisible();
}

bool MainWindow::is_documentation_dialog_visible() const
{
    return m_impl->documentation_dialog != nullptr && m_impl->documentation_dialog->isVisible();
}

QString MainWindow::documentation_summary_text() const
{
    return m_impl->documentation_text != nullptr ? m_impl->documentation_text->toPlainText() : QString{};
}

bool MainWindow::onboarding_visible() const
{
    return m_impl->onboarding_panel != nullptr && !m_impl->onboarding_panel->isHidden();
}

QString MainWindow::onboarding_text() const
{
    return m_impl->onboarding_label != nullptr ? m_impl->onboarding_label->text() : QString{};
}

void MainWindow::dismiss_onboarding()
{
    if (m_impl->onboarding_dismissed) {
        return;
    }
    m_impl->onboarding_dismissed = true;
    refresh_onboarding_panel();
    save_window_state();
    publish_ui_notification("Dismissed onboarding guidance for future sessions", ActivityLogSeverity::Info, 3000);
}

bool MainWindow::is_sample_browser_visible() const
{
    return m_impl->sample_browser_dialog != nullptr && m_impl->sample_browser_dialog->isVisible();
}

QStringList MainWindow::bundled_sample_ids() const
{
    QStringList ids;
    for (const auto& sample : bundled_samples()) {
        ids.push_back(sample.id);
    }
    return ids;
}

QString MainWindow::last_status_message() const
{
    return m_impl->last_status_message;
}

int MainWindow::activity_log_entry_count() const
{
    return m_impl->activity_log != nullptr ? m_impl->activity_log->entry_count() : 0;
}

QString MainWindow::activity_log_entry_text(int index) const
{
    return m_impl->activity_log != nullptr ? m_impl->activity_log->entry_text(index) : QString{};
}

QStringList MainWindow::activity_log_entries() const
{
    return m_impl->activity_log != nullptr ? m_impl->activity_log->all_entry_texts() : QStringList{};
}

int MainWindow::activity_log_max_entries() const
{
    return m_impl->activity_log != nullptr ? m_impl->activity_log->max_entries() : 0;
}

void MainWindow::set_performance_metrics_visible(bool visible)
{
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_performance_metrics_visible(visible);
    }
    if (m_impl->performance_status_label != nullptr) {
        m_impl->performance_status_label->setVisible(visible);
        m_impl->performance_status_label->setText(performance_metrics_text());
    }
}

bool MainWindow::performance_metrics_visible() const
{
    return m_impl->canvas != nullptr && m_impl->canvas->performance_metrics_visible();
}

QString MainWindow::performance_metrics_text() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->performance_metrics_text() : QString{};
}

std::size_t MainWindow::canvas_lod_cache_item_count() const
{
    return m_impl->canvas != nullptr ? m_impl->canvas->lod_cache_item_count() : 0;
}

QString MainWindow::job_progress_text() const
{
    return m_impl->last_job_progress_text;
}

bool MainWindow::has_active_job() const
{
    return m_impl->active_job_id.has_value();
}

int MainWindow::job_history_count() const
{
    return static_cast<int>(m_impl->job_history.size());
}

QString MainWindow::job_history_summary_text(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->job_history.size())) {
        return {};
    }
    return m_impl->job_history.at(static_cast<std::size_t>(index)).summary;
}

bool MainWindow::select_job_history_row(int row)
{
    if (m_impl->job_history_list == nullptr || row < 0 || row >= m_impl->job_history_list->count()) {
        return false;
    }
    m_impl->job_history_list->setCurrentRow(row);
    refresh_job_history_panel();
    return true;
}

QString MainWindow::job_history_details_text() const
{
    return m_impl->job_history_details != nullptr ? m_impl->job_history_details->toPlainText() : QString{};
}

bool MainWindow::job_history_open_json_enabled() const
{
    return m_impl->job_history_open_json_button != nullptr && m_impl->job_history_open_json_button->isEnabled();
}

bool MainWindow::job_history_open_html_enabled() const
{
    return m_impl->job_history_open_html_button != nullptr && m_impl->job_history_open_html_button->isEnabled();
}

bool MainWindow::trigger_job_history_open_json()
{
    if (!job_history_open_json_enabled()) {
        return false;
    }
    m_impl->job_history_open_json_button->click();
    return true;
}

bool MainWindow::trigger_job_history_open_html()
{
    if (!job_history_open_html_enabled()) {
        return false;
    }
    m_impl->job_history_open_html_button->click();
    return true;
}

int MainWindow::job_history_max_entries() const
{
    return m_impl->job_history_max_entries;
}

void MainWindow::set_job_pipeline_artificial_delay_for_tests(int milliseconds)
{
    m_impl->job_pipeline_options.artificial_stage_delay = std::chrono::milliseconds(std::max(milliseconds, 0));
}

void MainWindow::set_report_opener_for_tests(std::function<bool(const QString&)> opener)
{
    m_impl->report_opener = std::move(opener);
}

void MainWindow::set_report_export_path_picker_for_tests(std::function<QString(const QString&)> picker)
{
    m_impl->report_export_path_picker = std::move(picker);
}

bool MainWindow::is_import_dialog_visible() const
{
    return m_impl->import_review_dialog != nullptr && m_impl->import_review_dialog->isVisible();
}

void MainWindow::set_import_picker_for_tests(std::function<QStringList(QWidget*)> picker)
{
    m_impl->import_picker = std::move(picker);
}

bool MainWindow::import_project_paths(const QStringList& paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    return open_import_review_dialog(paths, false);
}

bool MainWindow::override_import_artifact_role(const QString& artifact_path, const QString& role_name)
{
    const auto role = aegis::storage::artifact_role_from_string(role_name.toStdString());
    if (role == aegis::storage::ArtifactRole::Unknown) {
        return false;
    }

    const QString normalized_target = QFileInfo(artifact_path).filePath();
    for (auto& artifact : m_impl->pending_import_package.artifacts()) {
        const QString candidate = QFileInfo(QString::fromStdString(artifact.path.string())).filePath();
        if (candidate == normalized_target || artifact.path.filename() == artifact_path.toStdString()) {
            artifact.role = role;
            artifact.category = aegis::storage::category_for_role(role);
            artifact.optional = artifact.category == aegis::storage::ArtifactCategory::Power ||
                                artifact.category == aegis::storage::ArtifactCategory::Current ||
                                artifact.category == aegis::storage::ArtifactCategory::Waivers ||
                                artifact.category == aegis::storage::ArtifactCategory::ExternalReports;
            refresh_import_review();
            return true;
        }
    }
    return false;
}

QString MainWindow::import_validation_summary_text() const
{
    return m_impl->import_review_text != nullptr ? m_impl->import_review_text->toPlainText() : import_summary_text(m_impl->pending_import_package);
}

int MainWindow::import_artifact_row_count() const
{
    return m_impl->import_artifact_table != nullptr ? m_impl->import_artifact_table->rowCount() : 0;
}

QString MainWindow::import_artifact_role_text(int row) const
{
    if (m_impl->import_artifact_table == nullptr || row < 0 || row >= m_impl->import_artifact_table->rowCount()) {
        return {};
    }
    if (auto* combo = qobject_cast<QComboBox*>(m_impl->import_artifact_table->cellWidget(row, 2))) {
        return combo->currentText();
    }
    return {};
}

QString MainWindow::import_artifact_status_text(int row) const
{
    if (m_impl->import_artifact_table == nullptr || row < 0 || row >= m_impl->import_artifact_table->rowCount()) {
        return {};
    }
    if (auto* item = m_impl->import_artifact_table->item(row, 4)) {
        return item->text();
    }
    return {};
}

bool MainWindow::set_import_artifact_role_from_ui(int row, const QString& role_name)
{
    if (m_impl->import_artifact_table == nullptr || row < 0 || row >= m_impl->import_artifact_table->rowCount()) {
        return false;
    }
    if (auto* combo = qobject_cast<QComboBox*>(m_impl->import_artifact_table->cellWidget(row, 2))) {
        const int index = combo->findText(role_name, Qt::MatchFixedString);
        if (index < 0) {
            return false;
        }
        combo->setCurrentIndex(index);
        return true;
    }
    return false;
}

bool MainWindow::select_import_artifact_row(int row)
{
    if (m_impl->import_artifact_table == nullptr || row < 0 || row >= m_impl->import_artifact_table->rowCount()) {
        return false;
    }
    m_impl->import_artifact_table->selectRow(row);
    if (m_impl->import_related_button != nullptr) {
        m_impl->import_related_button->setEnabled(true);
    }
    return true;
}

QString MainWindow::current_import_artifact_path() const
{
    if (m_impl->import_artifact_table == nullptr || m_impl->import_artifact_table->currentRow() < 0) {
        return {};
    }
    if (auto* item = m_impl->import_artifact_table->item(m_impl->import_artifact_table->currentRow(), 0); item != nullptr) {
        return item->text();
    }
    return {};
}

bool MainWindow::trigger_import_artifact_show_related_violations()
{
    if (m_impl->import_related_button == nullptr || !m_impl->import_related_button->isEnabled()) {
        return false;
    }
    m_impl->import_related_button->click();
    return true;
}

bool MainWindow::import_load_action_enabled() const
{
    return m_impl->import_load_button != nullptr && m_impl->import_load_button->isEnabled();
}

bool MainWindow::trigger_import_load_action()
{
    if (!import_load_action_enabled()) {
        return false;
    }
    m_impl->import_load_button->click();
    return true;
}

bool MainWindow::has_loaded_import_package() const
{
    return m_impl->has_loaded_import_package;
}

bool MainWindow::has_loaded_import_design_session() const
{
    return m_impl->loaded_import_session != nullptr;
}

QString MainWindow::loaded_import_project_name() const
{
    if (!m_impl->has_loaded_import_package) {
        return {};
    }
    return QString::fromStdString(m_impl->loaded_import_package.project().name);
}

QString MainWindow::loaded_import_design_session_summary_text() const
{
    return imported_design_session_summary_text(m_impl->loaded_import_session.get());
}

QString MainWindow::workspace_summary_text() const
{
    return m_impl->last_workspace_summary_text;
}

bool MainWindow::has_coverage_summary() const
{
    return m_impl->last_coverage_summary.has_value();
}

QString MainWindow::coverage_summary_text() const
{
    if (!m_impl->last_coverage_summary.has_value()) {
        return {};
    }
    const auto& cov = *m_impl->last_coverage_summary;
    QStringList lines;
    lines.append(QString("Coverage: %1 / %2 objects rendered (%3%)")
        .arg(cov.rendered_objects)
        .arg(cov.total_objects)
        .arg(QString::number(cov.coverage_ratio() * 100.0, 'f', 1)));
    if (cov.skipped_instances > 0) {
        lines.append(QString("Skipped instances: %1").arg(cov.skipped_instances));
    }
    if (cov.fallback_sized_instances > 0) {
        lines.append(QString("Fallback-sized instances: %1").arg(cov.fallback_sized_instances));
    }
    if (cov.session_errors > 0) {
        lines.append(QString("Session errors: %1").arg(cov.session_errors));
    }
    if (cov.unresolved_references > 0) {
        lines.append(QString("Unresolved references: %1").arg(cov.unresolved_references));
    }
    if (!cov.coverage_diagnostics.empty()) {
        lines.append("Diagnostics:");
        for (const auto& diag : cov.coverage_diagnostics) {
            lines.append(QString("  - %1").arg(QString::fromStdString(diag)));
        }
    }
    return lines.join("\n");
}

int MainWindow::coverage_skipped_count() const
{
    return m_impl->last_coverage_summary.has_value()
        ? static_cast<int>(m_impl->last_coverage_summary->skipped_instances)
        : 0;
}

int MainWindow::coverage_fallback_count() const
{
    return m_impl->last_coverage_summary.has_value()
        ? static_cast<int>(m_impl->last_coverage_summary->fallback_sized_instances)
        : 0;
}

bool MainWindow::coverage_has_issues() const
{
    return m_impl->last_coverage_summary.has_value()
        ? m_impl->last_coverage_summary->has_issues()
        : false;
}

int MainWindow::diagnostics_entry_count() const
{
    return m_impl->diagnostics_table != nullptr ? m_impl->diagnostics_table->rowCount() : 0;
}

QString MainWindow::diagnostics_details_text() const
{
    return m_impl->diagnostics_details != nullptr ? m_impl->diagnostics_details->toPlainText() : QString{};
}

bool MainWindow::set_diagnostics_severity_filter(const QString& severity)
{
    if (m_impl->diagnostics_severity_filter == nullptr) {
        return false;
    }
    const int index = m_impl->diagnostics_severity_filter->findText(severity, Qt::MatchFixedString);
    if (index < 0) {
        return false;
    }
    m_impl->diagnostics_severity_filter->setCurrentIndex(index);
    refresh_diagnostics_panel();
    return true;
}

bool MainWindow::select_diagnostics_row(int row)
{
    if (m_impl->diagnostics_table == nullptr || row < 0 || row >= m_impl->diagnostics_table->rowCount()) {
        return false;
    }
    m_impl->diagnostics_table->selectRow(row);
    refresh_diagnostics_panel();
    return true;
}

bool MainWindow::trigger_diagnostics_show_related_violations()
{
    if (m_impl->diagnostics_related_button == nullptr || !m_impl->diagnostics_related_button->isEnabled()) {
        return false;
    }
    m_impl->diagnostics_related_button->click();
    return true;
}

QStringList MainWindow::recent_project_paths() const
{
    return m_impl->recent_project_paths;
}

bool MainWindow::reopen_recent_project(int index)
{
    if (index < 0 || index >= m_impl->recent_project_paths.size()) {
        return false;
    }
    return reopen_project_from_path(m_impl->recent_project_paths.at(index), true);
}

bool MainWindow::reopen_last_session_enabled() const
{
    return m_impl->reopen_last_session_enabled;
}

void MainWindow::set_reopen_last_session_enabled(bool enabled)
{
    m_impl->reopen_last_session_enabled = enabled;
}

QStringList MainWindow::import_detected_roles() const
{
    QStringList roles;
    for (const auto& artifact : m_impl->pending_import_package.artifacts()) {
        roles.append(QString::fromStdString(aegis::storage::to_string(artifact.role)));
    }
    return roles;
}

int MainWindow::import_diagnostic_count() const
{
    return static_cast<int>(m_impl->pending_import_package.diagnostics().size());
}

bool MainWindow::import_has_blockers() const
{
    return m_impl->pending_import_package.validation_status() == aegis::storage::ValidationStatus::Invalid;
}

} // namespace aegis::ui
