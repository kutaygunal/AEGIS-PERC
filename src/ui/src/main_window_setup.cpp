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
void MainWindow::setup_actions()
{
    auto register_action = [this](const QString& id,
                                  const QString& text,
                                  const QKeySequence& shortcut,
                                  bool checkable,
                                  const QString& tooltip) -> QAction* {
        auto* action = new QAction(text, this);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        action->setCheckable(checkable);
        configure_action(action, tooltip, QString("WorkspaceAction_%1").arg(id));
        m_impl->actions.emplace(id, action);
        return action;
    };

    auto* import_project = register_action("import_project", "&Import Design Package...", QKeySequence("Ctrl+I"), false,
                                           "Review customer design-package files or dropped project folders before analysis");
    connect(import_project, &QAction::triggered, this, [this]() {
        const auto picker = m_impl->import_picker != nullptr ? m_impl->import_picker : choose_import_paths;
        const QStringList paths = picker(this);
        if (paths.isEmpty()) {
            publish_ui_notification("Import canceled", ActivityLogSeverity::Info, 3000);
            return;
        }
        Q_UNUSED(open_import_review_dialog(paths, false));
    });

    auto* open_sample = register_action("open_sample", "Open Sample: &Inverter", QKeySequence("Ctrl+Shift+O"), false,
                                        "Load the bundled inverter sample design");
    connect(open_sample, &QAction::triggered, this, [this]() {
        Q_UNUSED(load_bundled_sample("inverter"));
    });

    auto* browse_samples = register_action("browse_samples", "Browse &Samples...", QKeySequence(), false,
                                           "Choose from bundled sample designs");
    connect(browse_samples, &QAction::triggered, this, [this]() {
        if (m_impl->sample_browser_dialog == nullptr) {
            auto* dialog = new QDialog(this);
            dialog->setObjectName("BundledSampleBrowserDialog");
            dialog->setWindowTitle("Bundled Samples");
            dialog->setModal(false);
            dialog->resize(460, 320);
            auto* layout = new QVBoxLayout(dialog);
            auto* intro = new QLabel("Choose a bundled sample design to load into the workspace.", dialog);
            intro->setWordWrap(true);
            layout->addWidget(intro);
            auto* list = new QListWidget(dialog);
            for (const auto& sample : bundled_samples()) {
                auto* item = new QListWidgetItem(QString("%1 — %2").arg(sample.display_name, sample.description), list);
                item->setData(Qt::UserRole, sample.id);
            }
            layout->addWidget(list, 1);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            auto* load_button = new QPushButton("Load Selected Sample", dialog);
            buttons->addButton(load_button, QDialogButtonBox::ActionRole);
            connect(load_button, &QPushButton::clicked, this, [this]() {
                if (m_impl->sample_browser_list == nullptr || m_impl->sample_browser_list->currentItem() == nullptr) {
                    const QString message = "No bundled sample selected";
                    publish_ui_notification(message, ActivityLogSeverity::Warning, 3000);
                    return;
                }
                const QString sample_id = m_impl->sample_browser_list->currentItem()->data(Qt::UserRole).toString();
                Q_UNUSED(load_bundled_sample(sample_id));
            });
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
                if (item != nullptr) {
                    Q_UNUSED(load_bundled_sample(item->data(Qt::UserRole).toString()));
                }
            });
            m_impl->sample_browser_dialog = dialog;
            m_impl->sample_browser_list = list;
            layout->addWidget(buttons);
        }
        if (m_impl->sample_browser_list != nullptr && m_impl->sample_browser_list->currentRow() < 0) {
            m_impl->sample_browser_list->setCurrentRow(0);
        }
        m_impl->sample_browser_dialog->show();
        m_impl->sample_browser_dialog->raise();
        m_impl->sample_browser_dialog->activateWindow();
        const QString message = "Opened bundled sample browser";
        publish_ui_notification(message, ActivityLogSeverity::Info, 3000);
    });

    auto* reopen_last_project = register_action("reopen_last_project", "Reopen &Last Imported Project", QKeySequence("Ctrl+Shift+I"), false,
                                                "Reopen the last successful imported project package from disk");
    connect(reopen_last_project, &QAction::triggered, this, [this]() {
        if (m_impl->last_successful_project_path.trimmed().isEmpty()) {
            publish_ui_notification("Reopen unavailable: no successful imported project has been recorded", ActivityLogSeverity::Warning, 4000);
            return;
        }
        Q_UNUSED(reopen_project_from_path(m_impl->last_successful_project_path, true));
    });

    auto* open_sample_nand2 = register_action("open_sample_nand2", "Open Sample: &NAND2", QKeySequence(), false,
                                              "Load the bundled NAND2 sample design");
    connect(open_sample_nand2, &QAction::triggered, this, [this]() {
        Q_UNUSED(load_bundled_sample("nand2"));
    });

    auto* open_sample_ring = register_action("open_sample_ring_oscillator", "Open Sample: &Ring Oscillator", QKeySequence(), false,
                                             "Load the bundled ring oscillator sample design");
    connect(open_sample_ring, &QAction::triggered, this, [this]() {
        Q_UNUSED(load_bundled_sample("ring_oscillator"));
    });

    auto* fit_view = register_action("fit_view", "&Fit View", QKeySequence("F"), false,
                                     "Fit the layout scene to the canvas viewport");
    connect(fit_view, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->fit_to_view();
        }
    });

    auto* reset_view = register_action("reset_view", "&Reset View", QKeySequence("Ctrl+0"), false,
                                       "Reset the canvas zoom and pan to the full layout");
    connect(reset_view, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->reset_view();
        }
    });

    auto* toggle_grid = register_action("toggle_grid", "Toggle &Grid", QKeySequence("G"), true,
                                        "Show or hide the layout grid overlay");
    toggle_grid->setChecked(true);
    connect(toggle_grid, &QAction::toggled, this, [this](bool checked) {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->set_grid_visible(checked);
        }
    });

    auto* toggle_overlays = register_action("toggle_overlays", "Toggle &Overlays", QKeySequence("O"), true,
                                            "Show or hide violation overlays");
    toggle_overlays->setChecked(true);
    connect(toggle_overlays, &QAction::toggled, this, [this](bool checked) {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->set_violation_overlays_visible(checked);
        }
    });

    auto* save_filter_preset = register_action("save_filter_preset", "Save Filter Preset...", QKeySequence(), false,
                                                "Save the current violation filters as a named preset");
    connect(save_filter_preset, &QAction::triggered, this, [this]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(this,
                                                   "Save Filter Preset",
                                                   "Preset name:",
                                                   QLineEdit::Normal,
                                                   QString{},
                                                   &accepted);
        if (!accepted) {
            publish_ui_notification("Filter preset save canceled", ActivityLogSeverity::Info, 3000);
            return;
        }
        if (save_violation_filter_preset(name)) {
            publish_ui_notification(QString("Saved filter preset '%1'").arg(normalize_saved_name(name)), ActivityLogSeverity::Info, 3000);
        } else {
            publish_ui_notification("Filter preset save failed: name is required", ActivityLogSeverity::Warning, 4000);
        }
    });

    auto* manage_filter_presets = register_action("manage_filter_presets", "Manage Filter Presets...", QKeySequence(), false,
                                                  "Apply, rename, or delete saved violation filter presets");
    connect(manage_filter_presets, &QAction::triggered, this, [this]() {
        const QStringList names = violation_filter_preset_names();
        if (names.isEmpty()) {
            publish_ui_notification("No saved filter presets", ActivityLogSeverity::Info, 3000);
            return;
        }

        bool accepted = false;
        const QString choice = QInputDialog::getItem(this,
                                                     "Manage Filter Presets",
                                                     "Choose preset:",
                                                     names,
                                                     0,
                                                     false,
                                                     &accepted);
        if (!accepted || choice.trimmed().isEmpty()) {
            publish_ui_notification("Filter preset management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        const QStringList operations{"Apply", "Rename", "Delete"};
        const QString operation = QInputDialog::getItem(this,
                                                        "Manage Filter Presets",
                                                        "Operation:",
                                                        operations,
                                                        0,
                                                        false,
                                                        &accepted);
        if (!accepted || operation.isEmpty()) {
            publish_ui_notification("Filter preset management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        if (operation == "Apply") {
            if (apply_violation_filter_preset(choice)) {
                publish_ui_notification(QString("Applied filter preset '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
            }
            return;
        }
        if (operation == "Rename") {
            const QString renamed = QInputDialog::getText(this,
                                                          "Rename Filter Preset",
                                                          "New preset name:",
                                                          QLineEdit::Normal,
                                                          choice,
                                                          &accepted);
            if (!accepted) {
                publish_ui_notification("Filter preset rename canceled", ActivityLogSeverity::Info, 3000);
                return;
            }
            if (rename_violation_filter_preset(choice, renamed)) {
                publish_ui_notification(QString("Renamed filter preset to '%1'").arg(normalize_saved_name(renamed)), ActivityLogSeverity::Info, 3000);
            } else {
                publish_ui_notification("Filter preset rename failed", ActivityLogSeverity::Warning, 4000);
            }
            return;
        }
        if (delete_violation_filter_preset(choice)) {
            publish_ui_notification(QString("Deleted filter preset '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
        }
    });

    auto* save_workspace_view_action = register_action("save_workspace_view", "Save Workspace View...", QKeySequence(), false,
                                                       "Save the current dock layout and workspace toggles as a named view");
    connect(save_workspace_view_action, &QAction::triggered, this, [this]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(this,
                                                   "Save Workspace View",
                                                   "View name:",
                                                   QLineEdit::Normal,
                                                   QString{},
                                                   &accepted);
        if (!accepted) {
            publish_ui_notification("Workspace view save canceled", ActivityLogSeverity::Info, 3000);
            return;
        }
        if (this->save_workspace_view(name)) {
            publish_ui_notification(QString("Saved workspace view '%1'").arg(normalize_saved_name(name)), ActivityLogSeverity::Info, 3000);
        } else {
            publish_ui_notification("Workspace view save failed: name is required", ActivityLogSeverity::Warning, 4000);
        }
    });

    auto* manage_workspace_views = register_action("manage_workspace_views", "Manage Workspace Views...", QKeySequence(), false,
                                                   "Apply, rename, or delete saved workspace views");
    connect(manage_workspace_views, &QAction::triggered, this, [this]() {
        const QStringList names = workspace_view_names();
        if (names.isEmpty()) {
            publish_ui_notification("No saved workspace views", ActivityLogSeverity::Info, 3000);
            return;
        }

        bool accepted = false;
        const QString choice = QInputDialog::getItem(this,
                                                     "Manage Workspace Views",
                                                     "Choose view:",
                                                     names,
                                                     0,
                                                     false,
                                                     &accepted);
        if (!accepted || choice.trimmed().isEmpty()) {
            publish_ui_notification("Workspace view management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        const QStringList operations{"Apply", "Rename", "Delete"};
        const QString operation = QInputDialog::getItem(this,
                                                        "Manage Workspace Views",
                                                        "Operation:",
                                                        operations,
                                                        0,
                                                        false,
                                                        &accepted);
        if (!accepted || operation.isEmpty()) {
            publish_ui_notification("Workspace view management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        if (operation == "Apply") {
            if (apply_workspace_view(choice)) {
                publish_ui_notification(QString("Applied workspace view '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
            }
            return;
        }
        if (operation == "Rename") {
            const QString renamed = QInputDialog::getText(this,
                                                          "Rename Workspace View",
                                                          "New view name:",
                                                          QLineEdit::Normal,
                                                          choice,
                                                          &accepted);
            if (!accepted) {
                publish_ui_notification("Workspace view rename canceled", ActivityLogSeverity::Info, 3000);
                return;
            }
            if (rename_workspace_view(choice, renamed)) {
                publish_ui_notification(QString("Renamed workspace view to '%1'").arg(normalize_saved_name(renamed)), ActivityLogSeverity::Info, 3000);
            } else {
                publish_ui_notification("Workspace view rename failed", ActivityLogSeverity::Warning, 4000);
            }
            return;
        }
        if (delete_workspace_view(choice)) {
            publish_ui_notification(QString("Deleted workspace view '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
        }
    });

    auto* save_viewport_preset_action = register_action("save_viewport_preset", "Save Viewport Preset...", QKeySequence(), false,
                                                        "Save the current canvas zoom and center as a named viewport preset");
    connect(save_viewport_preset_action, &QAction::triggered, this, [this]() {
        bool accepted = false;
        const QString name = QInputDialog::getText(this,
                                                   "Save Viewport Preset",
                                                   "Preset name:",
                                                   QLineEdit::Normal,
                                                   QString{},
                                                   &accepted);
        if (!accepted) {
            publish_ui_notification("Viewport preset save canceled", ActivityLogSeverity::Info, 3000);
            return;
        }
        if (save_viewport_preset(name)) {
            publish_ui_notification(QString("Saved viewport preset '%1'").arg(normalize_saved_name(name)), ActivityLogSeverity::Info, 3000);
        } else {
            publish_ui_notification("Viewport preset save failed: name is required", ActivityLogSeverity::Warning, 4000);
        }
    });

    auto* manage_viewport_presets = register_action("manage_viewport_presets", "Manage Viewport Presets...", QKeySequence(), false,
                                                      "Apply, rename, or delete saved viewport presets");
    connect(manage_viewport_presets, &QAction::triggered, this, [this]() {
        const QStringList names = viewport_preset_names();
        if (names.isEmpty()) {
            publish_ui_notification("No saved viewport presets", ActivityLogSeverity::Info, 3000);
            return;
        }

        bool accepted = false;
        const QString choice = QInputDialog::getItem(this,
                                                     "Manage Viewport Presets",
                                                     "Choose preset:",
                                                     names,
                                                     0,
                                                     false,
                                                     &accepted);
        if (!accepted || choice.trimmed().isEmpty()) {
            publish_ui_notification("Viewport preset management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        const QStringList operations{"Apply", "Rename", "Delete"};
        const QString operation = QInputDialog::getItem(this,
                                                        "Manage Viewport Presets",
                                                        "Operation:",
                                                        operations,
                                                        0,
                                                        false,
                                                        &accepted);
        if (!accepted || operation.isEmpty()) {
            publish_ui_notification("Viewport preset management canceled", ActivityLogSeverity::Info, 3000);
            return;
        }

        if (operation == "Apply") {
            if (apply_viewport_preset(choice)) {
                publish_ui_notification(QString("Applied viewport preset '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
            }
            return;
        }
        if (operation == "Rename") {
            const QString renamed = QInputDialog::getText(this,
                                                          "Rename Viewport Preset",
                                                          "New preset name:",
                                                          QLineEdit::Normal,
                                                          choice,
                                                          &accepted);
            if (!accepted) {
                publish_ui_notification("Viewport preset rename canceled", ActivityLogSeverity::Info, 3000);
                return;
            }
            if (rename_viewport_preset(choice, renamed)) {
                publish_ui_notification(QString("Renamed viewport preset to '%1'").arg(normalize_saved_name(renamed)), ActivityLogSeverity::Info, 3000);
            } else {
                publish_ui_notification("Viewport preset rename failed", ActivityLogSeverity::Warning, 4000);
            }
            return;
        }
        if (delete_viewport_preset(choice)) {
            publish_ui_notification(QString("Deleted viewport preset '%1'").arg(choice), ActivityLogSeverity::Info, 3000);
        }
    });

    auto* zoom_to_selection_action = register_action("zoom_to_selection", "Zoom to &Selection", QKeySequence("Ctrl+Shift+F"), false,
                                                       "Fit the viewport to the current selection");
    connect(zoom_to_selection_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->zoom_to_selection();
        }
    });

    auto* zoom_to_violations_action = register_action("zoom_to_violations", "Zoom to &Violations", QKeySequence("Ctrl+Shift+V"), false,
                                                        "Fit the viewport to all violation overlays");
    connect(zoom_to_violations_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->zoom_to_violations();
        }
    });

    auto* zoom_to_trace_action = register_action("zoom_to_trace", "Zoom to &Trace", QKeySequence("Ctrl+Shift+T"), false,
                                                     "Fit the viewport to the active trace result");
    connect(zoom_to_trace_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->zoom_to_trace();
        }
    });

    auto* jump_to_coordinate_action = register_action("jump_to_coordinate", "Jump to &Coordinate...", QKeySequence("Ctrl+J"), false,
                                                        "Center the viewport on a specific scene coordinate");
    connect(jump_to_coordinate_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas == nullptr) {
            return;
        }
        bool accepted = false;
        const QString text = QInputDialog::getText(this,
                                                   "Jump to Coordinate",
                                                   "X, Y (optional zoom):",
                                                   QLineEdit::Normal,
                                                   QString{},
                                                   &accepted);
        if (!accepted || text.trimmed().isEmpty()) {
            return;
        }
        const QStringList parts = text.split(',');
        if (parts.size() < 2) {
            publish_ui_notification("Invalid coordinate format: expected X, Y", ActivityLogSeverity::Warning, 3000);
            return;
        }
        bool ok_x = false, ok_y = false, ok_z = false;
        const double x = parts[0].trimmed().toDouble(&ok_x);
        const double y = parts[1].trimmed().toDouble(&ok_y);
        double zoom = 0.0;
        if (parts.size() > 2) {
            zoom = parts[2].trimmed().toDouble(&ok_z);
        }
        if (!ok_x || !ok_y) {
            publish_ui_notification("Invalid coordinate values", ActivityLogSeverity::Warning, 3000);
            return;
        }
        m_impl->canvas->jump_to_coordinate(QPointF(x, y), zoom > 0.0 && ok_z ? zoom : m_impl->canvas->zoom_level());
    });

    auto* viewport_back_action = register_action("viewport_back", "Viewport &Back", QKeySequence("Alt+Left"), false,
                                                   "Return to the previous viewport state");
    connect(viewport_back_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->viewport_back();
        }
    });

    auto* viewport_forward_action = register_action("viewport_forward", "Viewport &Forward", QKeySequence("Alt+Right"), false,
                                                      "Go forward to the next viewport state");
    connect(viewport_forward_action, &QAction::triggered, this, [this]() {
        if (m_impl->canvas != nullptr) {
            m_impl->canvas->viewport_forward();
        }
    });

    auto* run_checks = register_action("run_checks", "&Run Checks", QKeySequence(Qt::Key_F5), false,
                                       "Run available electrical checks for the active design graph");
    connect(run_checks, &QAction::triggered, this, [this]() {
        execute_run_checks();
    });

    auto* cancel_active_job = register_action("cancel_active_job", "Cancel Active &Job", QKeySequence("Shift+F5"), false,
                                              "Cancel the active local workflow job");
    connect(cancel_active_job, &QAction::triggered, this, [this]() {
        if (!m_impl->active_job_id.has_value()) {
            publish_ui_notification("Cancel unavailable: no active local job", ActivityLogSeverity::Warning, 3000);
            return;
        }
        if (m_impl->job_pipeline.request_cancel(*m_impl->active_job_id)) {
            publish_ui_notification("Cancellation requested for active local job", ActivityLogSeverity::Warning, 4000);
            if (m_impl->last_job_snapshot.has_value()) {
                m_impl->last_job_snapshot->cancel_requested = true;
            }
            update_action_states();
            return;
        }
        publish_ui_notification("Cancellation request failed: active local job no longer exists", ActivityLogSeverity::Error, 4000);
    });

    auto* retry_last_job = register_action("retry_last_job", "&Retry Last Job", QKeySequence("Ctrl+Shift+R"), false,
                                           "Retry the most recent imported-package local workflow job");
    connect(retry_last_job, &QAction::triggered, this, [this]() {
        if (!can_retry_last_job()) {
            const QString reason = m_impl->last_job_retry_reason.trimmed().isEmpty()
                ? QString("Retry unavailable: last job inputs are no longer valid")
                : m_impl->last_job_retry_reason;
            publish_ui_notification(reason, ActivityLogSeverity::Warning, 4000);
            return;
        }
        Q_UNUSED(start_imported_run_checks(true));
    });

    auto* trace_from_selection = register_action("trace_from_selection", "Trace from &Selection", QKeySequence("Ctrl+T"), false,
                                                 "Trace connectivity from the current workspace selection");
    connect(trace_from_selection, &QAction::triggered, this, [this]() {
        Q_UNUSED(request_trace_from_selection());
    });

    auto* trace_from_violation = register_action("trace_from_violation", "Trace from Current &Violation", QKeySequence("Ctrl+Shift+T"), false,
                                                 "Trace connectivity from the selected violation reference");
    connect(trace_from_violation, &QAction::triggered, this, [this]() {
        Q_UNUSED(request_trace_from_current_violation());
    });

    auto* focus_trace_action = register_action("focus_trace", "&Focus Trace", QKeySequence("Shift+F"), false,
                                               "Center the view on the active connectivity trace");
    connect(focus_trace_action, &QAction::triggered, this, &MainWindow::focus_trace);

    auto* clear_trace_action = register_action("clear_trace_action", "Clear &Trace", QKeySequence("Ctrl+Shift+C"), false,
                                               "Clear the active connectivity trace");
    connect(clear_trace_action, &QAction::triggered, this, &MainWindow::clear_trace);

    auto* clear_selection = register_action("clear_selection", "C&lear Selection", QKeySequence(Qt::Key_Escape), false,
                                            "Clear the current canvas selection");
    connect(clear_selection, &QAction::triggered, this, [this]() {
        if (m_impl->selection_model != nullptr) {
            m_impl->selection_model->clear();
        }
    });

    auto* about = register_action("about", "&About AEGIS-PERC", QKeySequence(), false,
                                  "Show product and workspace information");
    connect(about, &QAction::triggered, this, [this]() {
        if (m_impl->about_dialog == nullptr) {
            auto* dialog = new QDialog(this);
            dialog->setObjectName("AboutAegisDialog");
            dialog->setWindowTitle("About AEGIS-PERC");
            dialog->setModal(false);
            dialog->resize(420, 260);
            auto* layout = new QVBoxLayout(dialog);
            auto* summary = new QLabel(
                "<b>AEGIS-PERC</b><br/>"
                "AI-assisted electrical rule verification and root-cause analysis platform.<br/><br/>"
                "Current desktop workspace includes layout visualization, violations, tracing, graph exploration, and report preview.",
                dialog);
            summary->setWordWrap(true);
            layout->addWidget(summary);
            auto* details = new QLabel(
                "Version: 1.0.0<br/>UI stack: Qt 6 Widgets<br/>Workspace: dockable panels with headless-tested actions",
                dialog);
            details->setWordWrap(true);
            layout->addWidget(details);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            layout->addWidget(buttons);
            m_impl->about_dialog = dialog;
        }
        m_impl->about_dialog->show();
        m_impl->about_dialog->raise();
        m_impl->about_dialog->activateWindow();
        const QString message = "Opened About dialog";
        publish_ui_notification(message, ActivityLogSeverity::Info, 3000);
    });

    auto* documentation = register_action("documentation", "&Documentation", QKeySequence::HelpContents, false,
                                          "Open the local project documentation entry points");
    connect(documentation, &QAction::triggered, this, [this]() {
        const auto readme = project_readme_path();
        const auto docs_dir = docs_directory_path();
        const bool has_readme = std::filesystem::exists(readme);
        const bool has_docs = std::filesystem::exists(docs_dir);

        if (m_impl->documentation_dialog == nullptr) {
            auto* dialog = new QDialog(this);
            dialog->setObjectName("DocumentationDialog");
            dialog->setWindowTitle("AEGIS-PERC Documentation");
            dialog->setModal(false);
            dialog->resize(560, 360);
            auto* layout = new QVBoxLayout(dialog);
            auto* intro = new QLabel("Local documentation entry points for this workspace:", dialog);
            intro->setWordWrap(true);
            layout->addWidget(intro);
            auto* text = new QPlainTextEdit(dialog);
            text->setReadOnly(true);
            layout->addWidget(text, 1);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            layout->addWidget(buttons);
            m_impl->documentation_dialog = dialog;
            m_impl->documentation_text = text;
        }

        QStringList lines;
        if (has_readme) {
            lines.append(QString("README: %1").arg(QString::fromStdString(readme.string())));
        } else {
            lines.append("README: not found");
        }
        if (has_docs) {
            lines.append(QString("Docs directory: %1").arg(QString::fromStdString(docs_dir.string())));
        } else {
            lines.append("Docs directory: not found");
        }
        lines.append(QString{});
        lines.append("Use these local entry points for project documentation and build guidance.");
        if (m_impl->documentation_text != nullptr) {
            m_impl->documentation_text->setPlainText(lines.join('\n'));
        }

        m_impl->documentation_dialog->show();
        m_impl->documentation_dialog->raise();
        m_impl->documentation_dialog->activateWindow();
        const QString message = (has_readme || has_docs)
            ? QString("Opened documentation entry points")
            : QString("Documentation entry points unavailable on this machine");
        publish_ui_notification(message,
                                has_readme || has_docs ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                4000);
    });
}

void MainWindow::setup_menus()
{
    m_impl->menu_bar = menuBar();

    // File
    QMenu* fileMenu = m_impl->menu_bar->addMenu("&File");
    fileMenu->addAction(m_impl->actions.at("import_project"));
    fileMenu->addAction(m_impl->actions.at("reopen_last_project"));
    m_impl->recent_projects_menu = fileMenu->addMenu("Recent &Projects");
    refresh_recent_project_actions();
    fileMenu->addSeparator();
    auto* samples_menu = fileMenu->addMenu("Open &Bundled Sample");
    samples_menu->addAction(m_impl->actions.at("open_sample"));
    samples_menu->addAction(m_impl->actions.at("open_sample_nand2"));
    samples_menu->addAction(m_impl->actions.at("open_sample_ring_oscillator"));
    fileMenu->addAction(m_impl->actions.at("browse_samples"));
    fileMenu->addSeparator();
    {
        auto* a = fileMenu->addAction("E&xit");
        a->setShortcuts(QKeySequence::Quit);
        connect(a, &QAction::triggered, qApp, &QApplication::quit);
    }

    // View
    QMenu* viewMenu = m_impl->menu_bar->addMenu("&View");
    m_impl->view_menu = viewMenu;
    viewMenu->addAction(m_impl->actions.at("fit_view"));
    viewMenu->addAction(m_impl->actions.at("reset_view"));
    viewMenu->addSeparator();
    viewMenu->addAction(m_impl->actions.at("zoom_to_selection"));
    viewMenu->addAction(m_impl->actions.at("zoom_to_violations"));
    viewMenu->addAction(m_impl->actions.at("zoom_to_trace"));
    viewMenu->addAction(m_impl->actions.at("jump_to_coordinate"));
    viewMenu->addSeparator();
    viewMenu->addAction(m_impl->actions.at("viewport_back"));
    viewMenu->addAction(m_impl->actions.at("viewport_forward"));
    viewMenu->addSeparator();
    viewMenu->addAction(m_impl->actions.at("toggle_grid"));
    viewMenu->addAction(m_impl->actions.at("toggle_overlays"));
    m_impl->filter_presets_menu = viewMenu->addMenu("Filter &Presets");
    connect(m_impl->filter_presets_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_filter_preset_menu);
    m_impl->workspace_views_menu = viewMenu->addMenu("Workspace &Views");
    connect(m_impl->workspace_views_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_workspace_view_menu);
    m_impl->viewport_presets_menu = viewMenu->addMenu("Viewport &Presets");
    connect(m_impl->viewport_presets_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_viewport_preset_menu);

    // Tools
    QMenu* toolsMenu = m_impl->menu_bar->addMenu("&Tools");
    toolsMenu->addAction(m_impl->actions.at("run_checks"));
    toolsMenu->addAction(m_impl->actions.at("cancel_active_job"));
    toolsMenu->addAction(m_impl->actions.at("retry_last_job"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("trace_from_selection"));
    toolsMenu->addAction(m_impl->actions.at("trace_from_violation"));
    toolsMenu->addAction(m_impl->actions.at("focus_trace"));
    toolsMenu->addAction(m_impl->actions.at("clear_trace_action"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("save_filter_preset"));
    toolsMenu->addAction(m_impl->actions.at("manage_filter_presets"));
    toolsMenu->addAction(m_impl->actions.at("save_workspace_view"));
    toolsMenu->addAction(m_impl->actions.at("manage_workspace_views"));
    toolsMenu->addAction(m_impl->actions.at("save_viewport_preset"));
    toolsMenu->addAction(m_impl->actions.at("manage_viewport_presets"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("zoom_to_selection"));
    toolsMenu->addAction(m_impl->actions.at("zoom_to_violations"));
    toolsMenu->addAction(m_impl->actions.at("zoom_to_trace"));
    toolsMenu->addAction(m_impl->actions.at("jump_to_coordinate"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("viewport_back"));
    toolsMenu->addAction(m_impl->actions.at("viewport_forward"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("clear_selection"));

    // Help
    QMenu* helpMenu = m_impl->menu_bar->addMenu("&Help");
    helpMenu->addAction(m_impl->actions.at("about"));
    helpMenu->addAction(m_impl->actions.at("documentation"));
}

void MainWindow::setup_toolbar()
{
    m_impl->workspace_toolbar = configure_accessible_widget(addToolBar("Workspace"),
                                                            "Workspace Toolbar",
                                                            "Primary desktop actions for import, samples, view control, checks, and tracing",
                                                            "Toolbar containing the main workspace actions");
    m_impl->workspace_toolbar->setObjectName("WorkspaceToolbar");
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("import_project"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("open_sample"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("open_sample_nand2"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("open_sample_ring_oscillator"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("browse_samples"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("fit_view"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("reset_view"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("toggle_grid"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("toggle_overlays"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("zoom_to_selection"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("zoom_to_violations"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("zoom_to_trace"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("jump_to_coordinate"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("viewport_back"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("viewport_forward"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("run_checks"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("cancel_active_job"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("retry_last_job"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("trace_from_selection"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("trace_from_violation"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("focus_trace"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("clear_trace_action"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("clear_selection"));
}

void MainWindow::show_status_message(const QString& message, int timeout_ms)
{
    if (statusBar() != nullptr) {
        if (timeout_ms > 0) {
            statusBar()->showMessage(message, timeout_ms);
        } else {
            statusBar()->showMessage(message);
        }
    }
    m_impl->last_status_message = message;
}

void MainWindow::append_activity_log(const QString& message, ActivityLogSeverity severity)
{
    if (m_impl->activity_log != nullptr && !message.trimmed().isEmpty()) {
        m_impl->activity_log->append_entry(message, severity);
    }
}

void MainWindow::publish_ui_notification(const QString& message,
                                         ActivityLogSeverity severity,
                                         int timeout_ms,
                                         bool update_trace_panel)
{
    if (update_trace_panel && m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_status_text(message);
    }
    show_status_message(message, timeout_ms);
    append_activity_log(message, severity);
}

void MainWindow::publish_trace_feedback(const QString& message, ActivityLogSeverity severity, int timeout_ms)
{
    publish_ui_notification(message, severity, timeout_ms, true);
}

bool MainWindow::open_import_review_dialog(const QStringList& paths, bool from_drop)
{
    if (m_impl->import_review_dialog == nullptr) {
        auto* dialog = new QDialog(this);
        dialog->setObjectName("ImportReviewDialog");
        dialog->setWindowTitle("Import Design Package");
        dialog->setModal(false);
        dialog->resize(640, 420);
        auto* layout = new QVBoxLayout(dialog);
        auto* intro = new QLabel("Review detected file roles, required inputs, optional enrichments, and validation diagnostics before analysis.", dialog);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        auto* artifact_label = new QLabel("Detected &Artifacts", dialog);
        layout->addWidget(artifact_label);
        auto* artifact_table = configure_accessible_widget(new QTableWidget(dialog),
                                                           "Detected Import Artifacts",
                                                           "Detected project artifacts and inferred roles",
                                                           "Review detected import artifacts, inferred roles, requirements, and validation status");
        artifact_table->setObjectName("ImportArtifactTable");
        artifact_table->setColumnCount(6);
        artifact_table->setHorizontalHeaderLabels({"Path", "Category", "Role", "Requirement", "Status", "Origin"});
        artifact_table->horizontalHeader()->setStretchLastSection(true);
        artifact_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        artifact_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        artifact_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        artifact_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        artifact_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        artifact_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        artifact_table->verticalHeader()->setVisible(false);
        artifact_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        artifact_table->setSelectionMode(QAbstractItemView::SingleSelection);
        artifact_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        artifact_table->setContextMenuPolicy(Qt::CustomContextMenu);
        artifact_label->setBuddy(artifact_table);
        layout->addWidget(artifact_table, 2);

        auto* diagnostics_label = new QLabel("Validation &Diagnostics", dialog);
        layout->addWidget(diagnostics_label);
        auto* text = configure_accessible_widget(new QPlainTextEdit(dialog),
                                                 "Import Validation Diagnostics",
                                                 "Validation diagnostics for the pending design package",
                                                 "Read-only validation diagnostics for the pending import package");
        text->setObjectName("ImportDiagnosticsText");
        text->setReadOnly(true);
        diagnostics_label->setBuddy(text);
        layout->addWidget(text, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        auto* validate_button = configure_accessible_widget(new QPushButton("&Validate Files", dialog),
                                                            "Validate Import Files",
                                                            "Re-run design-package validation for the current import selection");
        validate_button->setObjectName("ImportValidateButton");
        auto* related_button = configure_accessible_widget(new QPushButton("Show Related &Violations", dialog),
                                                           "Show Related Violations",
                                                           "Select violations related to the currently selected artifact");
        related_button->setObjectName("ImportRelatedViolationsButton");
        auto* load_button = configure_accessible_widget(new QPushButton("&Load Project", dialog),
                                                        "Load Imported Project",
                                                        "Load the validated project package into the workspace");
        load_button->setObjectName("ImportLoadProjectButton");
        buttons->addButton(validate_button, QDialogButtonBox::ActionRole);
        buttons->addButton(related_button, QDialogButtonBox::ActionRole);
        buttons->addButton(load_button, QDialogButtonBox::AcceptRole);
        connect(validate_button, &QPushButton::clicked, this, [this]() {
            refresh_import_review();
            const bool blocked = import_has_blockers();
            publish_ui_notification(blocked ? "Import validation found blocking issues" : "Import validation completed",
                                    blocked ? ActivityLogSeverity::Warning : ActivityLogSeverity::Info,
                                    4000);
        });
        connect(load_button, &QPushButton::clicked, this, &MainWindow::apply_import_package);
        connect(related_button, &QPushButton::clicked, this, [this]() {
            Q_UNUSED(select_related_violations_for_current_artifact());
        });
        auto* validate_action = new QAction("Validate Files", artifact_table);
        configure_action(validate_action, "Re-run validation for the current import package", "ImportContextValidateFiles");
        connect(validate_action, &QAction::triggered, validate_button, &QPushButton::click);
        auto* related_action = new QAction("Show Related Violations", artifact_table);
        configure_action(related_action, "Select violations related to the current artifact", "ImportContextShowRelatedViolations");
        connect(related_action, &QAction::triggered, related_button, &QPushButton::click);
        auto* load_action = new QAction("Load Project", artifact_table);
        configure_action(load_action, "Load the current import package into the workspace", "ImportContextLoadProject");
        connect(load_action, &QAction::triggered, load_button, &QPushButton::click);
        connect(artifact_table, &QTableWidget::itemSelectionChanged, this, [this, related_button, related_action]() {
            const bool has_selection = m_impl->import_artifact_table != nullptr && m_impl->import_artifact_table->currentRow() >= 0;
            related_button->setEnabled(has_selection);
            related_action->setEnabled(has_selection);
        });
        connect(artifact_table, &QWidget::customContextMenuRequested, this, [artifact_table, related_action, validate_action, load_action](const QPoint& pos) {
            if (artifact_table == nullptr) {
                return;
            }
            if (const QModelIndex index = artifact_table->indexAt(pos); index.isValid()) {
                artifact_table->selectRow(index.row());
            }
            QMenu menu(artifact_table);
            menu.addAction(related_action);
            menu.addSeparator();
            menu.addAction(validate_action);
            menu.addAction(load_action);
            menu.exec(artifact_table->viewport()->mapToGlobal(pos));
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        QWidget::setTabOrder(artifact_table, text);
        QWidget::setTabOrder(text, validate_button);
        QWidget::setTabOrder(validate_button, related_button);
        QWidget::setTabOrder(related_button, load_button);
        m_impl->import_review_dialog = dialog;
        m_impl->import_load_button = load_button;
        m_impl->import_related_button = related_button;
        m_impl->import_related_button->setEnabled(false);
        m_impl->import_review_text = text;
        m_impl->import_artifact_table = artifact_table;
    }

    if (!paths.isEmpty()) {
        m_impl->pending_import_package = {};
        m_impl->pending_import_base_path.clear();
        if (paths.size() == 1 && QFileInfo(paths.front()).isDir()) {
            m_impl->pending_import_base_path = std::filesystem::path(paths.front().toStdString());
            m_impl->pending_import_package = m_impl->import_validator.scan_project_folder(paths.front().toStdString(), QFileInfo(paths.front()).fileName().toStdString());
        } else {
            aegis::storage::ProjectPackage package;
            package.set_manifest_version(1);
            package.project().name = "ImportedProject";
            if (!paths.isEmpty()) {
                m_impl->pending_import_base_path = QFileInfo(paths.front()).absoluteDir().absolutePath().toStdString();
            }
            std::size_t index = 0;
            for (const auto& path : paths) {
                QFileInfo info(path);
                if (!info.exists() || info.isDir()) {
                    continue;
                }
                const auto detection = m_impl->import_validator.detect_file_role(path.toStdString());
                const auto* best = detection.best();
                aegis::storage::SourceArtifact artifact;
                artifact.id = "artifact-" + std::to_string(++index);
                artifact.path = info.filePath().toStdString();
                artifact.origin = from_drop ? "drop" : "selection";
                if (best != nullptr) {
                    artifact.role = best->role;
                    artifact.category = best->category;
                    artifact.optional = artifact.category == aegis::storage::ArtifactCategory::Power ||
                                        artifact.category == aegis::storage::ArtifactCategory::Current ||
                                        artifact.category == aegis::storage::ArtifactCategory::Waivers ||
                                        artifact.category == aegis::storage::ArtifactCategory::ExternalReports;
                }
                package.artifacts().push_back(std::move(artifact));
                if (detection.is_ambiguous() && best != nullptr) {
                    package.diagnostics().push_back({aegis::storage::DiagnosticSeverity::Warning,
                                                     "AMBIGUOUS_ROLE",
                                                     "Multiple role candidates detected; selected '" + aegis::storage::to_string(best->role) + "'",
                                                     package.artifacts().back().id});
                }
            }
            package.rebuild_normalized_view();
            const auto diagnostics = m_impl->import_validator.validate(package);
            package.diagnostics().insert(package.diagnostics().end(), diagnostics.begin(), diagnostics.end());
            package.set_validation_status(m_impl->import_validator.derive_status(package.diagnostics()));
            m_impl->pending_import_package = std::move(package);
        }
        refresh_import_review();
    } else if (m_impl->pending_import_package.artifacts().empty()) {
        refresh_import_review();
    }

    m_impl->import_review_dialog->show();
    m_impl->import_review_dialog->raise();
    m_impl->import_review_dialog->activateWindow();
    publish_ui_notification(from_drop ? "Opened import review for dropped project content" : "Opened import review dialog",
                            ActivityLogSeverity::Info,
                            3000);
    return true;
}

void MainWindow::refresh_import_review()
{
    if (!m_impl->pending_import_package.artifacts().empty()) {
        m_impl->pending_import_package.rebuild_normalized_view();
        const auto diagnostics = m_impl->import_validator.validate(m_impl->pending_import_package);
        auto preserved = m_impl->pending_import_package.diagnostics();
        preserved.erase(std::remove_if(preserved.begin(), preserved.end(), [](const auto& diagnostic) {
            return diagnostic.code == "MISSING_REQUIRED_TECHNOLOGY" ||
                   diagnostic.code == "MISSING_REQUIRED_LAYOUT" ||
                   diagnostic.code == "MISSING_REQUIRED_NETLIST" ||
                   diagnostic.code == "MISSING_REQUIRED_RULES" ||
                   diagnostic.code == "MULTIPLE_TECHNOLOGY_FILES" ||
                   diagnostic.code == "MULTIPLE_LAYOUT_FILES" ||
                   diagnostic.code == "MULTIPLE_NETLIST_FILES" ||
                   diagnostic.code == "MULTIPLE_RULE_PACKS" ||
                   diagnostic.code == "DUPLICATE_ARTIFACT_PATH" ||
                   diagnostic.code == "UNKNOWN_ROLE_ASSIGNMENT";
        }), preserved.end());
        preserved.insert(preserved.end(), diagnostics.begin(), diagnostics.end());
        m_impl->pending_import_package.diagnostics() = std::move(preserved);
        m_impl->pending_import_package.set_validation_status(
            m_impl->import_validator.derive_status(m_impl->pending_import_package.diagnostics()));
    } else {
        m_impl->pending_import_package.set_validation_status(aegis::storage::ValidationStatus::Unknown);
    }

    if (m_impl->import_artifact_table != nullptr) {
        auto* table = m_impl->import_artifact_table;
        table->clearContents();
        table->setRowCount(static_cast<int>(m_impl->pending_import_package.artifacts().size()));
        int row = 0;
        for (const auto& artifact : m_impl->pending_import_package.artifacts()) {
            auto* path_item = new QTableWidgetItem(QString::fromStdString(artifact.path.generic_string()));
            path_item->setData(Qt::UserRole, QString::fromStdString(artifact.id));
            table->setItem(row, 0, path_item);
            table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(aegis::storage::to_string(artifact.category))));

            auto* role_combo = new QComboBox(table);
            role_combo->setObjectName(QString("ImportArtifactRoleCombo_%1").arg(row));
            for (const auto role : supported_import_roles()) {
                role_combo->addItem(QString::fromStdString(aegis::storage::to_string(role)),
                                    QString::fromStdString(aegis::storage::to_string(role)));
            }
            role_combo->setCurrentText(QString::fromStdString(aegis::storage::to_string(artifact.role)));
            const QString artifact_path = QString::fromStdString(artifact.path.generic_string());
            connect(role_combo, &QComboBox::currentTextChanged, this, [this, artifact_path](const QString& text) {
                Q_UNUSED(override_import_artifact_role(artifact_path, text));
            });
            table->setCellWidget(row, 2, role_combo);

            table->setItem(row, 3, new QTableWidgetItem(artifact_requirement_text(artifact)));
            table->setItem(row, 4, new QTableWidgetItem(artifact_status_text(m_impl->pending_import_package, artifact)));
            table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(artifact.origin)));
            ++row;
        }
    }

    if (m_impl->import_review_text != nullptr) {
        m_impl->import_review_text->setPlainText(import_summary_text(m_impl->pending_import_package)
                                                 + "\n\nValidation Diagnostics:\n"
                                                 + import_diagnostics_text(m_impl->pending_import_package));
    }

    if (m_impl->import_load_button != nullptr) {
        const bool enable_load = !m_impl->pending_import_package.artifacts().empty() && !import_has_blockers();
        m_impl->import_load_button->setEnabled(enable_load);
        m_impl->import_load_button->setToolTip(enable_load
            ? "Commit the validated import package into the workspace"
            : "Resolve blocking import diagnostics before loading the project");
    }
    if (m_impl->import_related_button != nullptr) {
        m_impl->import_related_button->setEnabled(m_impl->import_artifact_table != nullptr && m_impl->import_artifact_table->currentRow() >= 0);
    }
    refresh_diagnostics_panel();
}

void MainWindow::apply_import_package()
{
    if (m_impl->pending_import_package.artifacts().empty()) {
        publish_ui_notification("Load Project unavailable: no import package is ready", ActivityLogSeverity::Warning, 4000);
        return;
    }
    if (import_has_blockers()) {
        publish_ui_notification("Load Project blocked: resolve import diagnostics first", ActivityLogSeverity::Warning, 4000);
        refresh_import_review();
        return;
    }

    m_impl->loaded_import_package = m_impl->pending_import_package;
    m_impl->loaded_import_base_path = m_impl->pending_import_base_path;
    m_impl->has_loaded_import_package = true;
    m_impl->sample_mode_active = false;

    aegis::storage::ImportedDesignSessionBuilder session_builder;
    m_impl->loaded_import_session = std::make_unique<aegis::storage::ImportedDesignSession>(
        session_builder.build(m_impl->loaded_import_package, m_impl->loaded_import_base_path));

    const auto imported_scene = build_imported_design_scene(*m_impl->loaded_import_session);
    set_scene(imported_scene.scene);
    set_connectivity_graph(&m_impl->loaded_import_session->graph());
    set_violations({});

    if (m_impl->hierarchy_browser != nullptr) {
        m_impl->hierarchy_browser->set_session(m_impl->loaded_import_session.get());
    }
    if (m_impl->properties_panel != nullptr) {
        m_impl->properties_panel->set_session(m_impl->loaded_import_session.get());
    }

    const QString project_name = QString::fromStdString(m_impl->loaded_import_package.project().name.empty()
        ? std::string{"ImportedProject"}
        : m_impl->loaded_import_package.project().name);
    const QString message = QString("Loaded imported project package metadata: %1 (%2 artifact(s), session %3 with %4 diagnostics, %5 scene item(s))")
                                .arg(project_name)
                                .arg(m_impl->loaded_import_package.artifacts().size())
                                .arg(QString::fromStdString(aegis::storage::to_string(m_impl->loaded_import_session->status())))
                                .arg(m_impl->loaded_import_session->diagnostics().size())
                                .arg(imported_scene.scene.items.size());
    publish_ui_notification(message, ActivityLogSeverity::Info, 5000);
    for (const auto& diagnostic : imported_scene.diagnostics) {
        append_activity_log(QString::fromStdString(diagnostic), ActivityLogSeverity::Warning);
    }
    if (!m_impl->loaded_import_base_path.empty() && std::filesystem::exists(m_impl->loaded_import_base_path)) {
        const QString recent_path = QString::fromStdString(m_impl->loaded_import_base_path.string());
        m_impl->recent_project_paths.removeAll(recent_path);
        m_impl->recent_project_paths.prepend(recent_path);
        m_impl->last_successful_project_path = recent_path;
        refresh_recent_project_actions();
    }
    refresh_workspace_summary();
    refresh_diagnostics_panel();
    update_action_states();

    if (m_impl->import_review_dialog != nullptr) {
        m_impl->import_review_dialog->close();
    }
}

void MainWindow::finalize_active_job()
{
    m_jobs->finalize_active_job();
}

void MainWindow::refresh_workspace_summary()
{
    QString project_name = "(no project loaded)";
    QString mode = "empty workspace";
    QString rule_source = "built-in desktop defaults";
    int artifact_count = 0;
    QString readiness = "Missing design scene and connectivity graph";

    if (m_impl->has_loaded_import_package) {
        mode = "imported customer project";
        project_name = QString::fromStdString(m_impl->loaded_import_package.project().name.empty()
            ? std::string{"ImportedProject"}
            : m_impl->loaded_import_package.project().name);
        artifact_count = static_cast<int>(m_impl->loaded_import_package.artifacts().size());
        rule_source = m_impl->loaded_import_package.normalized().rule_artifact_ids.empty()
            ? QString("imported package without rule pack")
            : QString("imported rule pack");
        if (!imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path)) {
            readiness = "Imported package inputs missing on disk";
        } else if (m_impl->loaded_import_package.validation_status() == aegis::storage::ValidationStatus::Invalid) {
            readiness = "Import package has blocking diagnostics";
        } else if (m_impl->loaded_import_session == nullptr) {
            readiness = "Imported package loaded but imported design session is unavailable";
        } else if (m_impl->loaded_import_session->status() == aegis::storage::SessionBuildStatus::Error) {
            readiness = QString("Imported package loaded with session errors (%1 diagnostics); local job pipeline remains available")
                            .arg(m_impl->loaded_import_session->diagnostics().size());
        } else if (m_impl->current_graph == nullptr) {
            readiness = QString("Ready to run imported package via local job pipeline (session %1, no scene/graph loaded yet)")
                            .arg(QString::fromStdString(aegis::storage::to_string(m_impl->loaded_import_session->status())));
        } else {
            readiness = QString("Imported design scene and graph loaded; ready to run via local job pipeline (session %1)")
                            .arg(QString::fromStdString(aegis::storage::to_string(m_impl->loaded_import_session->status())));
        }
    } else if (m_impl->sample_mode_active) {
        mode = "sample mode";
        project_name = m_impl->canvas != nullptr ? QString::fromStdString(m_impl->canvas->scene().design_name) : QString("sample");
        rule_source = "built-in desktop defaults";
        readiness = m_impl->current_graph != nullptr
            ? QString("Ready to run bundled sample")
            : QString("Missing connectivity graph");
    } else if (m_impl->canvas != nullptr && m_impl->canvas->has_scene()) {
        mode = "manual/custom scene";
        project_name = QString::fromStdString(m_impl->canvas->scene().design_name.empty()
            ? std::string{"(unnamed scene)"}
            : m_impl->canvas->scene().design_name);
        readiness = m_impl->current_graph != nullptr
            ? QString("Ready to run with built-in desktop defaults")
            : QString("Missing connectivity graph");
    }

    const int violation_count = m_impl->violation_explorer != nullptr ? m_impl->violation_explorer->total_violation_count() : 0;
    m_impl->last_workspace_summary_text = QString("Project: %1\nMode: %2\nArtifacts: %3\nRule source: %4\nViolations: %5\nReadiness: %6")
        .arg(project_name, mode)
        .arg(artifact_count)
        .arg(rule_source)
        .arg(violation_count)
        .arg(readiness);
    if (m_impl->workspace_summary_label != nullptr) {
        m_impl->workspace_summary_label->setText(m_impl->last_workspace_summary_text);
    }
    refresh_onboarding_panel();
}

void MainWindow::refresh_onboarding_panel()
{
    if (m_impl->onboarding_panel == nullptr || m_impl->onboarding_label == nullptr) {
        return;
    }

    const bool has_scene = m_impl->canvas != nullptr && m_impl->canvas->has_scene();
    const bool show_onboarding = !m_impl->onboarding_dismissed && !has_scene && !m_impl->has_loaded_import_package;
    m_impl->onboarding_panel->setVisible(show_onboarding);
    if (!show_onboarding) {
        return;
    }

    QStringList lines;
    lines.append("New here? Start from the empty workspace using one of the guided actions below.");
    lines.append("- Browse Samples: load a bundled design and then run checks.");
    lines.append("- Import Design Package: review customer project files before analysis.");
    lines.append("- Run Checks (F5): available after loading a sample or imported project with connectivity data.");
    lines.append("- Documentation: open local README and docs entry points.");
    m_impl->onboarding_label->setText(lines.join('\n'));
}

void MainWindow::refresh_diagnostics_panel()
{
    m_diagnostics->refresh_diagnostics_panel();
}
bool MainWindow::navigate_current_violation_relationships()
{
    return m_diagnostics->navigate_current_violation_relationships();
}
bool MainWindow::select_related_violations_for_current_diagnostic()
{
    return m_diagnostics->select_related_violations_for_current_diagnostic();
}
bool MainWindow::select_related_violations_for_current_artifact()
{
    return m_diagnostics->select_related_violations_for_current_artifact();
}
void MainWindow::setup_dock_panels()
{
    auto make_dock = [this](const QString& title, Qt::DockWidgetArea area, QWidget* widget) -> QDockWidget* {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(title + "Dock");
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        dock->setWidget(widget);
        addDockWidget(area, dock);
        m_impl->docks.append(dock);
        return dock;
    };

    auto* workspace_summary_panel = new QWidget(this);
    auto* workspace_summary_layout = new QVBoxLayout(workspace_summary_panel);
    workspace_summary_layout->setContentsMargins(8, 8, 8, 8);
    auto* workspace_summary_intro = configure_accessible_widget(new QLabel("Current project, rule source, and analysis readiness.", workspace_summary_panel),
                                                                "Workspace Summary Introduction");
    workspace_summary_intro->setWordWrap(true);
    workspace_summary_layout->addWidget(workspace_summary_intro);
    m_impl->workspace_summary_label = configure_accessible_widget(new QLabel(workspace_summary_panel),
                                                                  "Workspace Summary",
                                                                  "Current project mode, rule source, artifact counts, and readiness summary");
    m_impl->workspace_summary_label->setWordWrap(true);
    workspace_summary_layout->addWidget(m_impl->workspace_summary_label);

    m_impl->onboarding_panel = new QWidget(workspace_summary_panel);
    auto* onboarding_layout = new QVBoxLayout(m_impl->onboarding_panel);
    onboarding_layout->setContentsMargins(0, 4, 0, 0);
    m_impl->onboarding_label = configure_accessible_widget(new QLabel(m_impl->onboarding_panel),
                                                           "Onboarding Guidance",
                                                           "First-run guidance with shortcut entry points for import, samples, checks, and documentation");
    m_impl->onboarding_label->setWordWrap(true);
    onboarding_layout->addWidget(m_impl->onboarding_label);
    auto* onboarding_actions = new QHBoxLayout();
    auto* import_button = configure_accessible_widget(new QToolButton(m_impl->onboarding_panel),
                                                      "Import Design Package",
                                                      m_impl->actions.at("import_project")->toolTip());
    import_button->setObjectName("OnboardingImportButton");
    import_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    import_button->setDefaultAction(m_impl->actions.at("import_project"));
    onboarding_actions->addWidget(import_button);
    auto* samples_button = configure_accessible_widget(new QToolButton(m_impl->onboarding_panel),
                                                       "Browse Samples",
                                                       m_impl->actions.at("browse_samples")->toolTip());
    samples_button->setObjectName("OnboardingBrowseSamplesButton");
    samples_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    samples_button->setDefaultAction(m_impl->actions.at("browse_samples"));
    onboarding_actions->addWidget(samples_button);
    auto* run_checks_button = configure_accessible_widget(new QToolButton(m_impl->onboarding_panel),
                                                          "Run Checks",
                                                          m_impl->actions.at("run_checks")->toolTip());
    run_checks_button->setObjectName("OnboardingRunChecksButton");
    run_checks_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    run_checks_button->setDefaultAction(m_impl->actions.at("run_checks"));
    onboarding_actions->addWidget(run_checks_button);
    auto* docs_button = configure_accessible_widget(new QToolButton(m_impl->onboarding_panel),
                                                    "Documentation",
                                                    m_impl->actions.at("documentation")->toolTip());
    docs_button->setObjectName("OnboardingDocumentationButton");
    docs_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    docs_button->setDefaultAction(m_impl->actions.at("documentation"));
    onboarding_actions->addWidget(docs_button);
    onboarding_actions->addStretch(1);
    auto* dismiss_button = configure_accessible_widget(new QPushButton("&Dismiss", m_impl->onboarding_panel),
                                                       "Dismiss Onboarding",
                                                       "Hide the first-run onboarding guidance panel");
    dismiss_button->setObjectName("OnboardingDismissButton");
    connect(dismiss_button, &QPushButton::clicked, this, &MainWindow::dismiss_onboarding);
    onboarding_actions->addWidget(dismiss_button);
    onboarding_layout->addLayout(onboarding_actions);
    workspace_summary_layout->addWidget(m_impl->onboarding_panel);

    auto* workspace_summary_dock = make_dock("Workspace Summary", Qt::LeftDockWidgetArea, workspace_summary_panel);

    auto* diagnostics_panel = new QWidget(this);
    auto* diagnostics_layout = new QVBoxLayout(diagnostics_panel);
    diagnostics_layout->setContentsMargins(8, 8, 8, 8);
    auto* diagnostics_intro = configure_accessible_widget(new QLabel("Import and run diagnostics with severity filtering and quick navigation.", diagnostics_panel),
                                                          "Diagnostics Introduction");
    diagnostics_intro->setWordWrap(true);
    diagnostics_layout->addWidget(diagnostics_intro);
    auto* diagnostics_filter_row = new QHBoxLayout();
    auto* diagnostics_severity_label = new QLabel("&Severity:", diagnostics_panel);
    diagnostics_filter_row->addWidget(diagnostics_severity_label);
    m_impl->diagnostics_severity_filter = configure_accessible_widget(new QComboBox(diagnostics_panel),
                                                                      "Diagnostics Severity Filter",
                                                                      "Filter import and run diagnostics by severity");
    m_impl->diagnostics_severity_filter->setObjectName("DiagnosticsSeverityFilter");
    m_impl->diagnostics_severity_filter->addItems({"All", "info", "warning", "error"});
    diagnostics_filter_row->addWidget(m_impl->diagnostics_severity_filter);
    diagnostics_filter_row->addStretch(1);
    diagnostics_layout->addLayout(diagnostics_filter_row);
    diagnostics_severity_label->setBuddy(m_impl->diagnostics_severity_filter);
    m_impl->diagnostics_table = configure_accessible_widget(new QTableWidget(diagnostics_panel),
                                                            "Diagnostics Table",
                                                            "Visible import and run diagnostics",
                                                            "Diagnostics table with severity, source, and summary columns");
    m_impl->diagnostics_table->setObjectName("DiagnosticsTable");
    m_impl->diagnostics_table->setColumnCount(3);
    m_impl->diagnostics_table->setHorizontalHeaderLabels({"Severity", "Source", "Summary"});
    m_impl->diagnostics_table->horizontalHeader()->setStretchLastSection(true);
    m_impl->diagnostics_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_impl->diagnostics_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_impl->diagnostics_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->diagnostics_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->diagnostics_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_impl->diagnostics_table->setContextMenuPolicy(Qt::CustomContextMenu);
    diagnostics_layout->addWidget(m_impl->diagnostics_table, 1);
    m_impl->diagnostics_details = configure_accessible_widget(new QPlainTextEdit(diagnostics_panel),
                                                              "Diagnostic Details",
                                                              "Details for the selected diagnostic entry",
                                                              "Read-only details for the selected diagnostic entry");
    m_impl->diagnostics_details->setObjectName("DiagnosticsDetails");
    m_impl->diagnostics_details->setReadOnly(true);
    diagnostics_layout->addWidget(m_impl->diagnostics_details, 1);
    m_impl->diagnostics_related_button = configure_accessible_widget(new QPushButton("Show Related &Violations", diagnostics_panel),
                                                                     "Diagnostics Related Violations",
                                                                     "Select violations related to the selected diagnostic entry");
    m_impl->diagnostics_related_button->setObjectName("DiagnosticsRelatedViolationsButton");
    m_impl->diagnostics_related_button->setEnabled(false);
    diagnostics_layout->addWidget(m_impl->diagnostics_related_button);
    connect(m_impl->diagnostics_severity_filter, &QComboBox::currentTextChanged, this, [this](const QString&) {
        refresh_diagnostics_panel();
    });
    connect(m_impl->diagnostics_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        refresh_diagnostics_panel();
    });
    connect(m_impl->diagnostics_related_button, &QPushButton::clicked, this, [this]() {
        Q_UNUSED(select_related_violations_for_current_diagnostic());
    });
    auto* diagnostics_related_action = new QAction("Show Related Violations", m_impl->diagnostics_table);
    configure_action(diagnostics_related_action,
                     "Select violations related to the current diagnostic entry",
                     "DiagnosticsContextShowRelatedViolations");
    connect(diagnostics_related_action, &QAction::triggered, m_impl->diagnostics_related_button, &QPushButton::click);
    auto* diagnostics_copy_action = new QAction("Copy Diagnostic Details", m_impl->diagnostics_table);
    configure_action(diagnostics_copy_action,
                     "Copy the selected diagnostic details to the clipboard",
                     "DiagnosticsContextCopyDetails");
    connect(diagnostics_copy_action, &QAction::triggered, this, [this]() {
        if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr && m_impl->diagnostics_details != nullptr) {
            clipboard->setText(m_impl->diagnostics_details->toPlainText());
            publish_ui_notification("Diagnostics context action: copied diagnostic details", ActivityLogSeverity::Info, 3000);
        }
    });
    connect(m_impl->diagnostics_table, &QWidget::customContextMenuRequested, this, [this, diagnostics_related_action, diagnostics_copy_action](const QPoint& pos) {
        if (m_impl->diagnostics_table == nullptr) {
            return;
        }
        if (const QModelIndex index = m_impl->diagnostics_table->indexAt(pos); index.isValid()) {
            m_impl->diagnostics_table->selectRow(index.row());
        }
        const bool has_selection = m_impl->diagnostics_table->currentRow() >= 0;
        diagnostics_related_action->setEnabled(has_selection);
        diagnostics_copy_action->setEnabled(has_selection);
        QMenu menu(m_impl->diagnostics_table);
        menu.addAction(diagnostics_related_action);
        menu.addSeparator();
        menu.addAction(diagnostics_copy_action);
        menu.exec(m_impl->diagnostics_table->viewport()->mapToGlobal(pos));
    });
    QWidget::setTabOrder(m_impl->diagnostics_severity_filter, m_impl->diagnostics_table);
    QWidget::setTabOrder(m_impl->diagnostics_table, m_impl->diagnostics_details);
    QWidget::setTabOrder(m_impl->diagnostics_details, m_impl->diagnostics_related_button);
    auto* diagnostics_dock = make_dock("Diagnostics", Qt::BottomDockWidgetArea, diagnostics_panel);

    m_impl->layer_panel = new LayerPanel(this);
    connect(m_impl->layer_panel, &LayerPanel::layer_visibility_changed, this,
            [this](const QString& layer_name, bool visible) {
                if (m_impl->canvas != nullptr) {
                    m_impl->canvas->set_layer_visibility(layer_name.toStdString(), visible);
                }
            });
    auto* layers_dock = make_dock("Layers", Qt::LeftDockWidgetArea, m_impl->layer_panel);

    m_impl->properties_panel = new PropertiesPanel(this);
    if (m_impl->selection_model != nullptr) {
        connect(m_impl->selection_model, &SelectionModel::selection_changed,
                m_impl->properties_panel, &PropertiesPanel::set_selected_ids);
        connect(m_impl->selection_model, &SelectionModel::selection_changed,
                this, [this](const QStringList& ids) {
                    if (m_impl->hierarchy_browser != nullptr) {
                        if (ids.isEmpty()) {
                            m_impl->hierarchy_browser->clear_selection();
                        } else {
                            m_impl->hierarchy_browser->select_by_stable_id(ids.first());
                        }
                    }
                    if (m_impl->report_preview != nullptr) {
                        m_impl->report_preview->set_selected_item_count(static_cast<int>(ids.size()));
                        if (m_impl->canvas != nullptr) {
                            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
                        }
                    }
                    update_action_states();
                });
    }
    auto* properties_dock = make_dock("Properties", Qt::RightDockWidgetArea, m_impl->properties_panel);

    m_impl->violation_explorer = new ViolationExplorerPanel(this);
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::current_violation_changed,
            this, [this]() {
                if (m_impl->violation_explorer == nullptr || m_impl->canvas == nullptr) {
                    update_action_states();
                    return;
                }
                const auto* violation = m_impl->violation_explorer->current_violation();
                if (m_impl->report_preview != nullptr) {
                    m_impl->report_preview->set_current_violation(violation);
                    m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
                }
                update_action_states();
                if (violation == nullptr) {
                    return;
                }
                if (violation->location.point.has_value()) {
                    m_impl->canvas->center_on_scene_point(
                        QPointF(violation->location.point->x, violation->location.point->y));
                    return;
                }
                const bool centered = m_impl->canvas->center_on_violation(violation->id);
                Q_UNUSED(centered);
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::filtered_violations_changed,
            this, [this](aegis::rules::ViolationCollection violations) {
                if (m_impl->canvas != nullptr) {
                    m_impl->canvas->set_violations(violations);
                }
                if (m_impl->report_preview != nullptr) {
                    m_impl->report_preview->set_violations(violations);
                    if (m_impl->canvas != nullptr) {
                        m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
                    }
                }
                const QString message = m_impl->violation_explorer != nullptr
                    ? m_impl->violation_explorer->filter_summary_text()
                    : QString("Violations updated");
                publish_ui_notification(message, ActivityLogSeverity::Info);
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::heatmap_settings_changed,
            this, [this](bool visible, double opacity) {
                if (m_impl->canvas != nullptr) {
                    m_impl->canvas->set_heatmap_visible(visible);
                    m_impl->canvas->set_heatmap_opacity(opacity);
                }
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::trace_current_violation_requested,
            this, [this]() {
                if (request_trace_from_current_violation()) {
                    append_activity_log("Violation context action: trace from violation", ActivityLogSeverity::Info);
                }
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::center_current_violation_requested,
            this, [this]() {
                if (m_impl->violation_explorer == nullptr || m_impl->canvas == nullptr) {
                    return;
                }
                const auto* violation = m_impl->violation_explorer->current_violation();
                if (violation == nullptr) {
                    return;
                }
                if (violation->location.point.has_value()) {
                    m_impl->canvas->center_on_scene_point(QPointF(violation->location.point->x, violation->location.point->y));
                } else {
                    Q_UNUSED(m_impl->canvas->center_on_violation(violation->id));
                }
                publish_ui_notification("Violation context action: centered current violation on canvas", ActivityLogSeverity::Info, 3000);
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::copy_current_violation_id_requested,
            this, [this]() {
                if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr && m_impl->violation_explorer != nullptr) {
                    clipboard->setText(m_impl->violation_explorer->current_violation_id_for_copy());
                    publish_ui_notification("Violation context action: copied violation ID", ActivityLogSeverity::Info, 3000);
                }
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::copy_current_violation_details_requested,
            this, [this]() {
                if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr && m_impl->violation_explorer != nullptr) {
                    clipboard->setText(m_impl->violation_explorer->current_violation_details_for_copy());
                    publish_ui_notification("Violation context action: copied violation details", ActivityLogSeverity::Info, 3000);
                }
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::navigate_current_violation_requested,
            this, [this]() {
                Q_UNUSED(navigate_current_violation_relationships());
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::copy_selected_violations_requested,
            this, [this]() {
                if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr && m_impl->violation_explorer != nullptr) {
                    clipboard->setText(m_impl->violation_explorer->selected_violations_text());
                    publish_ui_notification(QString("Violation bulk action: copied %1 selected row(s)")
                                                .arg(m_impl->violation_explorer->selected_violation_count()),
                                            ActivityLogSeverity::Info,
                                            3000);
                }
            });
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::export_selected_violations_requested,
            this, [this]() {
                if (m_impl->violation_explorer == nullptr) {
                    return;
                }
                QString target_path;
                if (m_impl->report_export_path_picker) {
                    target_path = m_impl->report_export_path_picker("violation_json");
                } else {
                    target_path = QFileDialog::getSaveFileName(this,
                                                               "Export Selected Violations",
                                                               "selected_violations.json",
                                                               "JSON Files (*.json)");
                }
                if (target_path.trimmed().isEmpty()) {
                    publish_ui_notification("Violation export canceled", ActivityLogSeverity::Info, 3000);
                    return;
                }
                std::ofstream out(target_path.toStdString(), std::ios::binary);
                out << m_impl->violation_explorer->selected_violations_json_text().toStdString();
                publish_ui_notification(QString("Violation bulk action: exported selected rows to %1").arg(target_path),
                                        ActivityLogSeverity::Info,
                                        4000);
            });
    auto* violations_dock = make_dock("Violations", Qt::BottomDockWidgetArea, m_impl->violation_explorer);

    m_impl->report_preview = new ReportPreviewPanel(this);
    connect(m_impl->report_preview, &ReportPreviewPanel::refresh_requested, this, [this]() {
        if (m_impl->report_preview != nullptr && m_impl->canvas != nullptr) {
            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
        }
    });
    connect(m_impl->report_preview, &ReportPreviewPanel::export_json_requested, this, [this]() {
        Q_UNUSED(export_report_preview(false));
    });
    connect(m_impl->report_preview, &ReportPreviewPanel::export_html_requested, this, [this]() {
        Q_UNUSED(export_report_preview(true));
    });
    auto* report_dock = make_dock("Report Preview", Qt::RightDockWidgetArea, m_impl->report_preview);

    m_impl->graph_explorer = new GraphExplorerPanel(this);
    connect(m_impl->graph_explorer, &GraphExplorerPanel::graph_node_selected, this,
            [this](const QString& stable_name) {
                Q_UNUSED(request_trace_by_name(stable_name));
                if (m_impl->canvas == nullptr) {
                    return;
                }
                for (const auto& item : m_impl->canvas->scene().items) {
                    const auto name_it = item.source_metadata.find("name");
                    const auto net_it = item.source_metadata.find("net_name");
                    const bool matches_name = name_it != item.source_metadata.end() && stable_name == QString::fromStdString(name_it->second);
                    const bool matches_net = net_it != item.source_metadata.end() && stable_name == QString::fromStdString(net_it->second);
                    if (matches_name || matches_net) {
                        select_scene_item_by_id(QString::fromStdString(item.id));
                        break;
                    }
                }
            });
    m_impl->graph_dock = make_dock("Graph Explorer", Qt::LeftDockWidgetArea, m_impl->graph_explorer);

    m_impl->hierarchy_browser = new HierarchyBrowserPanel(this);
    connect(m_impl->hierarchy_browser, &HierarchyBrowserPanel::object_selected, this,
            [this](const QString& stable_id) {
                if (m_impl->canvas == nullptr) {
                    return;
                }
                select_scene_item_by_id(stable_id);
            });
    m_impl->hierarchy_dock = make_dock("Hierarchy", Qt::LeftDockWidgetArea, m_impl->hierarchy_browser);

    m_impl->trace_panel = new TracePanel(this);
    connect(m_impl->trace_panel, &TracePanel::trace_requested, this,
            [this](const QString& stable_name) {
                Q_UNUSED(request_trace_by_name(stable_name));
            });
    connect(m_impl->trace_panel, &TracePanel::clear_trace_requested, this, &MainWindow::clear_trace);
    connect(m_impl->trace_panel, &TracePanel::focus_trace_requested, this, &MainWindow::focus_trace);
    auto* trace_dock = make_dock("Trace", Qt::RightDockWidgetArea, m_impl->trace_panel);

    auto* job_history_panel = new QWidget(this);
    job_history_panel->setObjectName("JobHistoryPanel");
    auto* job_history_layout = new QVBoxLayout(job_history_panel);
    auto* job_history_intro = configure_accessible_widget(new QLabel("Recent local workflow jobs with progress history and generated report links.", job_history_panel),
                                                          "Job History Introduction");
    job_history_intro->setWordWrap(true);
    job_history_layout->addWidget(job_history_intro);
    m_impl->job_history_list = configure_accessible_widget(new QListWidget(job_history_panel),
                                                           "Job History",
                                                           "Recent local workflow jobs",
                                                           "List of recent local workflow jobs and their completion states");
    m_impl->job_history_list->setObjectName("JobHistoryList");
    m_impl->job_history_list->setContextMenuPolicy(Qt::CustomContextMenu);
    job_history_layout->addWidget(m_impl->job_history_list, 1);
    m_impl->job_history_details = configure_accessible_widget(new QPlainTextEdit(job_history_panel),
                                                              "Job History Details",
                                                              "Progress history and report paths for the selected local workflow job",
                                                              "Read-only details for the selected local workflow job");
    m_impl->job_history_details->setObjectName("JobHistoryDetails");
    m_impl->job_history_details->setReadOnly(true);
    job_history_layout->addWidget(m_impl->job_history_details, 1);
    auto* job_history_buttons = new QHBoxLayout();
    m_impl->job_history_open_json_button = configure_accessible_widget(new QPushButton("Open &JSON Report", job_history_panel),
                                                                       "Open JSON Report",
                                                                       "Open the JSON report for the selected local workflow job");
    m_impl->job_history_open_html_button = configure_accessible_widget(new QPushButton("Open &HTML Report", job_history_panel),
                                                                       "Open HTML Report",
                                                                       "Open the HTML report for the selected local workflow job");
    m_impl->job_history_open_json_button->setObjectName("JobHistoryOpenJsonButton");
    m_impl->job_history_open_html_button->setObjectName("JobHistoryOpenHtmlButton");
    job_history_buttons->addWidget(m_impl->job_history_open_json_button);
    job_history_buttons->addWidget(m_impl->job_history_open_html_button);
    job_history_layout->addLayout(job_history_buttons);
    connect(m_impl->job_history_list, &QListWidget::currentRowChanged, this, [this](int) {
        refresh_job_history_panel();
    });
    connect(m_impl->job_history_open_json_button, &QPushButton::clicked, this, [this]() {
        Q_UNUSED(open_selected_job_history_report(false));
    });
    connect(m_impl->job_history_open_html_button, &QPushButton::clicked, this, [this]() {
        Q_UNUSED(open_selected_job_history_report(true));
    });
    auto* job_history_open_json_action = new QAction("Open JSON Report", m_impl->job_history_list);
    configure_action(job_history_open_json_action,
                     "Open the JSON report for the selected local workflow job",
                     "JobHistoryContextOpenJsonReport");
    connect(job_history_open_json_action, &QAction::triggered, m_impl->job_history_open_json_button, &QPushButton::click);
    auto* job_history_open_html_action = new QAction("Open HTML Report", m_impl->job_history_list);
    configure_action(job_history_open_html_action,
                     "Open the HTML report for the selected local workflow job",
                     "JobHistoryContextOpenHtmlReport");
    connect(job_history_open_html_action, &QAction::triggered, m_impl->job_history_open_html_button, &QPushButton::click);
    connect(m_impl->job_history_list, &QWidget::customContextMenuRequested, this, [this, job_history_open_json_action, job_history_open_html_action](const QPoint& pos) {
        if (m_impl->job_history_list == nullptr) {
            return;
        }
        if (QListWidgetItem* item = m_impl->job_history_list->itemAt(pos); item != nullptr) {
            m_impl->job_history_list->setCurrentItem(item);
        }
        job_history_open_json_action->setEnabled(m_impl->job_history_open_json_button != nullptr && m_impl->job_history_open_json_button->isEnabled());
        job_history_open_html_action->setEnabled(m_impl->job_history_open_html_button != nullptr && m_impl->job_history_open_html_button->isEnabled());
        QMenu menu(m_impl->job_history_list);
        menu.addAction(job_history_open_json_action);
        menu.addAction(job_history_open_html_action);
        menu.exec(m_impl->job_history_list->viewport()->mapToGlobal(pos));
    });
    QWidget::setTabOrder(m_impl->job_history_list, m_impl->job_history_details);
    QWidget::setTabOrder(m_impl->job_history_details, m_impl->job_history_open_json_button);
    QWidget::setTabOrder(m_impl->job_history_open_json_button, m_impl->job_history_open_html_button);
    auto* job_history_dock = make_dock("Jobs", Qt::BottomDockWidgetArea, job_history_panel);

    m_impl->activity_log = new ActivityLogPanel(this);
    append_activity_log("Workspace initialized", ActivityLogSeverity::Info);
    auto* log_dock = make_dock("Log", Qt::BottomDockWidgetArea, m_impl->activity_log);
    refresh_job_history_panel();

    if (m_impl->view_menu != nullptr) {
        m_impl->view_menu->addSeparator();
        m_impl->view_menu->addAction(workspace_summary_dock->toggleViewAction());
        m_impl->view_menu->addAction(diagnostics_dock->toggleViewAction());
        m_impl->view_menu->addAction(layers_dock->toggleViewAction());
        m_impl->view_menu->addAction(properties_dock->toggleViewAction());
        m_impl->view_menu->addAction(violations_dock->toggleViewAction());
        m_impl->view_menu->addAction(report_dock->toggleViewAction());
        if (m_impl->graph_dock != nullptr) {
            m_impl->view_menu->addAction(m_impl->graph_dock->toggleViewAction());
        }
        if (m_impl->hierarchy_dock != nullptr) {
            m_impl->view_menu->addAction(m_impl->hierarchy_dock->toggleViewAction());
        }
        m_impl->view_menu->addAction(trace_dock->toggleViewAction());
        m_impl->view_menu->addAction(job_history_dock->toggleViewAction());
        m_impl->view_menu->addAction(log_dock->toggleViewAction());
    }
    QWidget::setTabOrder(m_impl->canvas, m_impl->layer_panel);
    QWidget::setTabOrder(m_impl->layer_panel, m_impl->violation_explorer);
    QWidget::setTabOrder(m_impl->violation_explorer, m_impl->graph_explorer);
    QWidget::setTabOrder(m_impl->graph_explorer, m_impl->trace_panel);
    QWidget::setTabOrder(m_impl->trace_panel, m_impl->diagnostics_severity_filter);
    QWidget::setTabOrder(m_impl->diagnostics_related_button, m_impl->job_history_list);
    refresh_workspace_summary();
    refresh_diagnostics_panel();
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------
void MainWindow::update_action_states()
{
    const bool has_scene = m_impl->canvas != nullptr && m_impl->canvas->has_scene();
    const bool has_graph = m_impl->current_graph != nullptr;
    const bool job_active = m_impl->active_job_id.has_value();
    const bool can_run_imported = m_impl->has_loaded_import_package
        && m_impl->loaded_import_package.validation_status() != aegis::storage::ValidationStatus::Invalid
        && imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path);
    const bool cancel_requested = m_impl->last_job_snapshot.has_value() && m_impl->last_job_snapshot->cancel_requested;
    const bool has_violations = m_impl->canvas != nullptr && m_impl->canvas->violation_count() > 0;
    const bool has_selection = m_impl->selection_model != nullptr && !m_impl->selection_model->empty();
    const bool has_active_trace = m_impl->canvas != nullptr && m_impl->canvas->has_active_trace();
    const bool has_current_violation = m_impl->violation_explorer != nullptr && m_impl->violation_explorer->current_violation() != nullptr;

    m_impl->actions.at("fit_view")->setEnabled(has_scene);
    m_impl->actions.at("reset_view")->setEnabled(has_scene);
    m_impl->actions.at("toggle_grid")->setEnabled(has_scene);
    m_impl->actions.at("run_checks")->setEnabled((has_graph || can_run_imported) && !job_active);
    m_impl->actions.at("cancel_active_job")->setEnabled(job_active && !cancel_requested);
    m_impl->actions.at("retry_last_job")->setEnabled(can_retry_last_job());
    m_impl->actions.at("trace_from_selection")->setEnabled(has_graph && has_selection);
    m_impl->actions.at("trace_from_violation")->setEnabled(has_graph && has_current_violation);
    m_impl->actions.at("focus_trace")->setEnabled(has_active_trace);
    m_impl->actions.at("clear_trace_action")->setEnabled(has_active_trace);
    m_impl->actions.at("clear_selection")->setEnabled(has_selection);
    m_impl->actions.at("toggle_overlays")->setEnabled(has_violations);
    m_impl->actions.at("save_filter_preset")->setEnabled(m_impl->violation_explorer != nullptr);
    m_impl->actions.at("manage_filter_presets")->setEnabled(!m_impl->filter_presets.empty());
    m_impl->actions.at("save_workspace_view")->setEnabled(true);
    m_impl->actions.at("manage_workspace_views")->setEnabled(!m_impl->workspace_views.empty());
    m_impl->actions.at("save_viewport_preset")->setEnabled(has_scene);
    m_impl->actions.at("manage_viewport_presets")->setEnabled(!m_impl->viewport_presets.empty());
    m_impl->actions.at("zoom_to_selection")->setEnabled(has_scene && has_selection);
    m_impl->actions.at("zoom_to_violations")->setEnabled(has_scene && has_violations);
    m_impl->actions.at("zoom_to_trace")->setEnabled(has_scene && has_active_trace);
    m_impl->actions.at("jump_to_coordinate")->setEnabled(has_scene);
    m_impl->actions.at("viewport_back")->setEnabled(has_scene && m_impl->canvas->can_viewport_back());
    m_impl->actions.at("viewport_forward")->setEnabled(has_scene && m_impl->canvas->can_viewport_forward());

    if (m_impl->canvas != nullptr) {
        m_impl->actions.at("toggle_grid")->setChecked(m_impl->canvas->grid_visible());
        m_impl->actions.at("toggle_overlays")->setChecked(m_impl->canvas->violation_overlays_visible());
    }

    update_trace_controls();
}

void MainWindow::refresh_recent_project_actions()
{
    if (m_impl->recent_projects_menu == nullptr) {
        return;
    }

    QStringList filtered;
    for (const auto& path : m_impl->recent_project_paths) {
        if (path.trimmed().isEmpty()) {
            continue;
        }
        if (!std::filesystem::exists(path.toStdString())) {
            continue;
        }
        if (!filtered.contains(path)) {
            filtered.push_back(path);
        }
    }
    m_impl->recent_project_paths = filtered;
    if (!m_impl->recent_project_paths.contains(m_impl->last_successful_project_path)
        && !m_impl->last_successful_project_path.trimmed().isEmpty()
        && std::filesystem::exists(m_impl->last_successful_project_path.toStdString())) {
        m_impl->recent_project_paths.prepend(m_impl->last_successful_project_path);
    }
    while (m_impl->recent_project_paths.size() > 8) {
        m_impl->recent_project_paths.removeLast();
    }

    m_impl->recent_projects_menu->clear();
    if (m_impl->recent_project_paths.isEmpty()) {
        auto* action = m_impl->recent_projects_menu->addAction("No recent imported projects");
        action->setEnabled(false);
    } else {
        for (int i = 0; i < m_impl->recent_project_paths.size(); ++i) {
            const QString path = m_impl->recent_project_paths.at(i);
            auto* action = m_impl->recent_projects_menu->addAction(QString("%1. %2").arg(i + 1).arg(path));
            connect(action, &QAction::triggered, this, [this, path]() {
                Q_UNUSED(reopen_project_from_path(path, true));
            });
        }
    }
    if (m_impl->actions.contains("reopen_last_project") && m_impl->actions.at("reopen_last_project") != nullptr) {
        m_impl->actions.at("reopen_last_project")->setEnabled(!m_impl->last_successful_project_path.trimmed().isEmpty());
    }
}

void MainWindow::refresh_filter_preset_menu()
{
    if (m_impl->filter_presets_menu == nullptr) {
        return;
    }

    m_impl->filter_presets_menu->clear();
    m_impl->filter_presets_menu->addAction(m_impl->actions.at("save_filter_preset"));
    m_impl->filter_presets_menu->addAction(m_impl->actions.at("manage_filter_presets"));
    if (m_impl->filter_presets.empty()) {
        auto* empty = m_impl->filter_presets_menu->addAction("No saved filter presets");
        empty->setEnabled(false);
        return;
    }

    m_impl->filter_presets_menu->addSeparator();
    for (const auto& preset : m_impl->filter_presets) {
        auto* action = m_impl->filter_presets_menu->addAction(preset.name);
        connect(action, &QAction::triggered, this, [this, name = preset.name]() {
            Q_UNUSED(apply_violation_filter_preset(name));
        });
    }
}

void MainWindow::refresh_workspace_view_menu()
{
    if (m_impl->workspace_views_menu == nullptr) {
        return;
    }

    m_impl->workspace_views_menu->clear();
    m_impl->workspace_views_menu->addAction(m_impl->actions.at("save_workspace_view"));
    m_impl->workspace_views_menu->addAction(m_impl->actions.at("manage_workspace_views"));
    if (m_impl->workspace_views.empty()) {
        auto* empty = m_impl->workspace_views_menu->addAction("No saved workspace views");
        empty->setEnabled(false);
        return;
    }

    m_impl->workspace_views_menu->addSeparator();
    for (const auto& view : m_impl->workspace_views) {
        auto* action = m_impl->workspace_views_menu->addAction(view.name);
        connect(action, &QAction::triggered, this, [this, name = view.name]() {
            Q_UNUSED(apply_workspace_view(name));
        });
    }
}

void MainWindow::refresh_viewport_preset_menu()
{
    if (m_impl->viewport_presets_menu == nullptr) {
        return;
    }

    m_impl->viewport_presets_menu->clear();
    m_impl->viewport_presets_menu->addAction(m_impl->actions.at("save_viewport_preset"));
    m_impl->viewport_presets_menu->addAction(m_impl->actions.at("manage_viewport_presets"));
    if (m_impl->viewport_presets.empty()) {
        auto* empty = m_impl->viewport_presets_menu->addAction("No saved viewport presets");
        empty->setEnabled(false);
        return;
    }

    m_impl->viewport_presets_menu->addSeparator();
    for (const auto& preset : m_impl->viewport_presets) {
        auto* action = m_impl->viewport_presets_menu->addAction(preset.name);
        connect(action, &QAction::triggered, this, [this, name = preset.name]() {
            Q_UNUSED(apply_viewport_preset(name));
        });
    }
}

bool MainWindow::reopen_project_from_path(const QString& path, bool mark_as_last_session)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    if (normalized.trimmed().isEmpty() || !QFileInfo(normalized).exists() || !QFileInfo(normalized).isDir()) {
        m_impl->recent_project_paths.removeAll(path);
        m_impl->recent_project_paths.removeAll(normalized);
        if (m_impl->last_successful_project_path == path || m_impl->last_successful_project_path == normalized) {
            m_impl->last_successful_project_path.clear();
        }
        refresh_recent_project_actions();
        publish_ui_notification(QString("Recent project path is unavailable and was removed: %1").arg(path), ActivityLogSeverity::Warning, 5000);
        return false;
    }

    m_impl->pending_import_base_path = normalized.toStdString();
    m_impl->pending_import_package = m_impl->import_validator.scan_project_folder(normalized.toStdString(), QFileInfo(normalized).fileName().toStdString());
    refresh_import_review();
    if (import_has_blockers()) {
        if (mark_as_last_session) {
            publish_ui_notification(QString("Reopened recent project with blocking diagnostics: %1").arg(normalized), ActivityLogSeverity::Warning, 5000);
        } else {
            publish_ui_notification(QString("Last session project reopened with blocking diagnostics: %1").arg(normalized), ActivityLogSeverity::Warning, 5000);
        }
        open_import_review_dialog({}, false);
        return false;
    }

    apply_import_package();
    m_impl->recent_project_paths.removeAll(normalized);
    m_impl->recent_project_paths.prepend(normalized);
    m_impl->last_successful_project_path = normalized;
    refresh_recent_project_actions();
    publish_ui_notification(mark_as_last_session
                                ? QString("Reopened imported project: %1").arg(normalized)
                                : QString("Reopened last session project: %1").arg(normalized),
                            ActivityLogSeverity::Info,
                            5000);
    return true;
}

void MainWindow::update_trace_controls()
{
    m_diagnostics->update_trace_controls();
}
void MainWindow::restore_window_state()
{
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    if (settings.contains(QString("%1/geometry").arg(kSettingsMainWindowGroup))) {
        restoreGeometry(settings.value(QString("%1/geometry").arg(kSettingsMainWindowGroup)).toByteArray());
    }
    if (settings.contains(QString("%1/state").arg(kSettingsMainWindowGroup))) {
        restoreState(settings.value(QString("%1/state").arg(kSettingsMainWindowGroup)).toByteArray());
    }

    const int version = settings.value(QString("%1/version").arg(kSettingsWorkspaceUiGroup), 0).toInt();
    const bool has_ui_state = version >= 1;
    const int recent_projects_version = settings.value(QString("%1/version").arg(kSettingsRecentProjectsGroup), 0).toInt();
    if (recent_projects_version >= 1) {
        m_impl->recent_project_paths = settings.value(QString("%1/paths").arg(kSettingsRecentProjectsGroup)).toStringList();
        m_impl->reopen_last_session_enabled = settings.value(QString("%1/reopenLastSession").arg(kSettingsRecentProjectsGroup), false).toBool();
        m_impl->last_successful_project_path = settings.value(QString("%1/lastSuccessfulPath").arg(kSettingsRecentProjectsGroup)).toString();
    }
    m_impl->onboarding_dismissed = settings.value(QString("%1/dismissed").arg(kSettingsOnboardingGroup), false).toBool();

    m_impl->filter_presets.clear();
    for (const auto& value : settings.value(QString("%1/filterPresets").arg(kSettingsWorkspaceUiGroup)).toList()) {
        if (const auto preset = saved_filter_preset_from_variant(value); preset.has_value()) {
            m_impl->filter_presets.push_back(*preset);
        }
    }

    m_impl->workspace_views.clear();
    for (const auto& value : settings.value(QString("%1/workspaceViews").arg(kSettingsWorkspaceUiGroup)).toList()) {
        if (const auto view = saved_workspace_view_from_variant(value); view.has_value()) {
            m_impl->workspace_views.push_back(*view);
        }
    }

    m_impl->viewport_presets.clear();
    for (const auto& value : settings.value(QString("%1/viewportPresets").arg(kSettingsWorkspaceUiGroup)).toList()) {
        if (const auto preset = saved_viewport_preset_from_variant(value); preset.has_value()) {
            m_impl->viewport_presets.push_back(*preset);
        }
    }

    const bool grid_visible = settings.value(QString("%1/gridVisible").arg(kSettingsWorkspaceUiGroup), true).toBool();
    if (m_impl->actions.contains("toggle_grid") && m_impl->actions.at("toggle_grid") != nullptr) {
        m_impl->actions.at("toggle_grid")->setChecked(grid_visible);
    } else if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_grid_visible(grid_visible);
    }

    const bool overlays_visible = settings.value(QString("%1/overlaysVisible").arg(kSettingsWorkspaceUiGroup), true).toBool();
    if (m_impl->actions.contains("toggle_overlays") && m_impl->actions.at("toggle_overlays") != nullptr) {
        m_impl->actions.at("toggle_overlays")->setChecked(overlays_visible);
    } else if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_violation_overlays_visible(overlays_visible);
    }

    set_heatmap_visible(settings.value(QString("%1/heatmapVisible").arg(kSettingsWorkspaceUiGroup), false).toBool());
    set_heatmap_opacity(settings.value(QString("%1/heatmapOpacity").arg(kSettingsWorkspaceUiGroup), 0.6).toDouble());
    set_performance_metrics_visible(settings.value(QString("%1/performanceMetricsVisible").arg(kSettingsWorkspaceUiGroup), false).toBool());

    if (has_ui_state) {
        const auto filter_map = settings.value(QString("%1/violationFilter").arg(kSettingsWorkspaceUiGroup)).toMap();
        if (!filter_map.isEmpty()) {
            set_violation_filter_state(ViolationFilterState::from_variant_map(filter_map));
        } else {
            clear_violation_filters();
        }
    } else {
        clear_violation_filters();
    }

    refresh_recent_project_actions();
    refresh_filter_preset_menu();
    refresh_workspace_view_menu();
    refresh_viewport_preset_menu();
    refresh_onboarding_panel();
    update_action_states();
    if (m_impl->reopen_last_session_enabled && !m_impl->last_successful_project_path.trimmed().isEmpty()) {
        Q_UNUSED(reopen_project_from_path(m_impl->last_successful_project_path, false));
    }
}

bool MainWindow::load_bundled_sample(const QString& sample_id)
{
    const auto* sample = bundled_sample_by_id(sample_id);
    if (sample == nullptr) {
        const QString message = QString("Unknown bundled sample: %1").arg(sample_id);
        publish_ui_notification(message, ActivityLogSeverity::Error);
        return false;
    }

    try {
        const auto ir = load_sample_design_ir(sample->file_name.toStdString().c_str());
        if (!ir.has_value()) {
            const QString message = QString("Bundled sample file not found: %1").arg(sample->file_name);
            publish_ui_notification(message, ActivityLogSeverity::Error);
            return false;
        }
        std::vector<std::string> unresolved;
        auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(*ir, unresolved);
        set_scene(build_ui_scene(*ir));
        m_impl->sample_mode_active = true;
        m_impl->owned_graph = std::make_unique<aegis::graph::ConnectivityGraph>(std::move(graph));
        set_connectivity_graph(m_impl->owned_graph.get());
        set_violations({});
        if (m_impl->hierarchy_browser != nullptr) {
            m_impl->hierarchy_browser->set_session(nullptr);
        }
        if (m_impl->properties_panel != nullptr) {
            m_impl->properties_panel->set_session(nullptr);
        }
        refresh_workspace_summary();
        const QString message = QString("Loaded sample: %1").arg(QString::fromStdString(ir->design_name));
        publish_ui_notification(message, ActivityLogSeverity::Info);
        if (!unresolved.empty()) {
            append_activity_log(QString("Sample graph resolved %1 missing reference(s) during import").arg(unresolved.size()),
                                ActivityLogSeverity::Warning);
        }
        if (m_impl->sample_browser_dialog != nullptr && m_impl->sample_browser_dialog->isVisible()) {
            m_impl->sample_browser_dialog->close();
        }
        return true;
    } catch (const std::exception& error) {
        const QString message = QString("Failed to load sample %1: %2").arg(sample->display_name, error.what());
        publish_ui_notification(message, ActivityLogSeverity::Error);
        return false;
    }
}

void MainWindow::save_window_state()
{
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    settings.setValue(QString("%1/geometry").arg(kSettingsMainWindowGroup), saveGeometry());
    settings.setValue(QString("%1/state").arg(kSettingsMainWindowGroup), saveState());
    settings.setValue(QString("%1/version").arg(kSettingsWorkspaceUiGroup), kWorkspaceUiStateVersion);
    settings.setValue(QString("%1/gridVisible").arg(kSettingsWorkspaceUiGroup), grid_visible());
    settings.setValue(QString("%1/overlaysVisible").arg(kSettingsWorkspaceUiGroup), violation_overlays_visible());
    settings.setValue(QString("%1/heatmapVisible").arg(kSettingsWorkspaceUiGroup), heatmap_visible());
    settings.setValue(QString("%1/heatmapOpacity").arg(kSettingsWorkspaceUiGroup), heatmap_opacity());
    settings.setValue(QString("%1/performanceMetricsVisible").arg(kSettingsWorkspaceUiGroup), performance_metrics_visible());
    settings.setValue(QString("%1/violationFilter").arg(kSettingsWorkspaceUiGroup), violation_filter_state().to_variant_map());
    QVariantList filter_presets;
    for (const auto& preset : m_impl->filter_presets) {
        filter_presets.push_back(to_variant_map(preset));
    }
    settings.setValue(QString("%1/filterPresets").arg(kSettingsWorkspaceUiGroup), filter_presets);
    QVariantList workspace_views;
    for (const auto& view : m_impl->workspace_views) {
        workspace_views.push_back(to_variant_map(view));
    }
    settings.setValue(QString("%1/workspaceViews").arg(kSettingsWorkspaceUiGroup), workspace_views);

    QVariantList viewport_presets;
    for (const auto& preset : m_impl->viewport_presets) {
        viewport_presets.push_back(to_variant_map(preset));
    }
    settings.setValue(QString("%1/viewportPresets").arg(kSettingsWorkspaceUiGroup), viewport_presets);
    refresh_recent_project_actions();
    settings.setValue(QString("%1/version").arg(kSettingsRecentProjectsGroup), kRecentProjectsStateVersion);
    settings.setValue(QString("%1/paths").arg(kSettingsRecentProjectsGroup), m_impl->recent_project_paths);
    settings.setValue(QString("%1/reopenLastSession").arg(kSettingsRecentProjectsGroup), m_impl->reopen_last_session_enabled);
    settings.setValue(QString("%1/lastSuccessfulPath").arg(kSettingsRecentProjectsGroup), m_impl->last_successful_project_path);
    settings.setValue(QString("%1/dismissed").arg(kSettingsOnboardingGroup), m_impl->onboarding_dismissed);
}

} // namespace aegis::ui
