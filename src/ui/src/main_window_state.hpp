#pragma once

#include "aegis/ui/main_window.hpp"
#include "aegis/ui/violation_filter.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <filesystem>
#include <functional>

// Value-type aegis headers required by Impl members
#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/orchestration/job_pipeline.hpp"
#include "aegis/storage/import_validation.hpp"
#include "aegis/storage/project_package.hpp"
#include "aegis/storage/imported_design_session.hpp"
#include "aegis/rules/violation.hpp"

// Forward declarations for Qt / UI pointers in Impl
class QAction;
class QComboBox;
class QDialog;
class QDockWidget;
class QLabel;
class QListWidget;
class QMenu;
class QMenuBar;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class QToolBar;
class QWidget;

#include <QFileInfo>

namespace aegis::ui {
class ActivityLogPanel;
class GraphExplorerPanel;
class HierarchyBrowserPanel;
class LayerPanel;
class LayoutCanvas;
class PropertiesPanel;
class ReportPreviewPanel;
class SelectionModel;
class TracePanel;
class ViolationExplorerPanel;
} // namespace aegis::ui

namespace aegis::ui {

inline constexpr int kWorkspaceUiStateVersion = 1;
inline constexpr int kRecentProjectsStateVersion = 1;
inline constexpr auto kSettingsMainWindowGroup = "mainWindow";
inline constexpr auto kSettingsWorkspaceUiGroup = "mainWindow/workspaceUi";
inline constexpr auto kSettingsRecentProjectsGroup = "mainWindow/recentProjects";
inline constexpr auto kSettingsOnboardingGroup = "mainWindow/onboarding";

using ActionMap = std::map<QString, QAction*>;

struct SavedFilterPreset {
    QString name;
    ViolationFilterState state;
};

struct SavedWorkspaceView {
    QString name;
    QByteArray dock_state;
    bool grid_visible = true;
    bool overlays_visible = true;
    bool heatmap_visible = false;
    double heatmap_opacity = 0.6;
    bool performance_metrics_visible = false;
};

struct SavedViewportPreset {
    QString name;
    double zoom_level = 1.0;
    double view_center_x = 0.0;
    double view_center_y = 0.0;
};

struct MainWindow::Impl {
    struct DiagnosticEntry {
        QString severity;
        QString source;
        QString summary;
        QString details;
        QString artifact_id;
        QString artifact_path;
        QString violation_id;
        bool blocking = false;
    };

    struct JobHistoryEntry {
        aegis::orchestration::JobId job_id = 0;
        QString action_name;
        QString project_name;
        QString state;
        QString summary;
        QString result_summary;
        QString error_message;
        QStringList progress_history;
        QDateTime started_at;
        QDateTime finished_at;
        std::optional<std::filesystem::path> json_report_path;
        std::optional<std::filesystem::path> html_report_path;
    };

    LayoutCanvas* canvas = nullptr;
    LayerPanel* layer_panel = nullptr;
    PropertiesPanel* properties_panel = nullptr;
    SelectionModel* selection_model = nullptr;
    ViolationExplorerPanel* violation_explorer = nullptr;
    ReportPreviewPanel* report_preview = nullptr;
    GraphExplorerPanel* graph_explorer = nullptr;
    HierarchyBrowserPanel* hierarchy_browser = nullptr;
    TracePanel* trace_panel = nullptr;
    ActivityLogPanel* activity_log = nullptr;
    QLabel* workspace_summary_label = nullptr;
    QWidget* onboarding_panel = nullptr;
    QLabel* onboarding_label = nullptr;
    QComboBox* diagnostics_severity_filter = nullptr;
    QTableWidget* diagnostics_table = nullptr;
    QPlainTextEdit* diagnostics_details = nullptr;
    QPushButton* diagnostics_related_button = nullptr;
    QListWidget* job_history_list = nullptr;
    QPlainTextEdit* job_history_details = nullptr;
    QPushButton* job_history_open_json_button = nullptr;
    QPushButton* job_history_open_html_button = nullptr;
    std::unique_ptr<aegis::graph::ConnectivityGraph> owned_graph;
    const aegis::graph::ConnectivityGraph* current_graph = nullptr;
    ConnectivityTraceAdapter trace_adapter;
    QDockWidget* graph_dock = nullptr;
    QDockWidget* hierarchy_dock = nullptr;
    QList<QDockWidget*> docks;
    QMenuBar* menu_bar = nullptr;
    QMenu* view_menu = nullptr;
    QMenu* recent_projects_menu = nullptr;
    QMenu* filter_presets_menu = nullptr;
    QMenu* workspace_views_menu = nullptr;
    QMenu* viewport_presets_menu = nullptr;
    QToolBar* workspace_toolbar = nullptr;
    QLabel* performance_status_label = nullptr;
    QLabel* job_progress_label = nullptr;
    QDialog* about_dialog = nullptr;
    QDialog* documentation_dialog = nullptr;
    QDialog* sample_browser_dialog = nullptr;
    QDialog* import_review_dialog = nullptr;
    QPushButton* import_load_button = nullptr;
    QPushButton* import_related_button = nullptr;
    QPlainTextEdit* documentation_text = nullptr;
    QPlainTextEdit* import_review_text = nullptr;
    QTableWidget* import_artifact_table = nullptr;
    QListWidget* sample_browser_list = nullptr;
    aegis::storage::ImportPreflightValidator import_validator;
    aegis::storage::ProjectPackage pending_import_package;
    std::filesystem::path pending_import_base_path;
    aegis::storage::ProjectPackage loaded_import_package;
    std::filesystem::path loaded_import_base_path;
    std::unique_ptr<aegis::storage::ImportedDesignSession> loaded_import_session;
    bool has_loaded_import_package = false;
    bool sample_mode_active = false;
    aegis::orchestration::LocalJobPipeline job_pipeline;
    aegis::orchestration::JobPipelineOptions job_pipeline_options;
    QTimer* job_poll_timer = nullptr;
    std::optional<aegis::orchestration::JobId> active_job_id;
    std::optional<aegis::orchestration::JobProgressSnapshot> last_job_snapshot;
    std::filesystem::path active_job_output_dir;
    QString last_workspace_summary_text;
    std::optional<DesignCoverageSummary> last_coverage_summary;
    std::vector<aegis::rules::Violation> latest_violations;
    std::vector<DiagnosticEntry> diagnostics_entries;
    QStringList recent_project_paths;
    bool reopen_last_session_enabled = false;
    QString last_successful_project_path;
    QString last_job_progress_text;
    bool last_job_retry_available = false;
    QString last_job_retry_reason;
    QString last_status_message;
    bool onboarding_dismissed = false;
    std::vector<SavedFilterPreset> filter_presets;
    std::vector<SavedWorkspaceView> workspace_views;
    std::vector<SavedViewportPreset> viewport_presets;
    ActionMap actions;
    std::vector<JobHistoryEntry> job_history;
    int job_history_max_entries = 12;
    std::function<QStringList(QWidget*)> import_picker;
    std::function<bool(const QString&)> report_opener = [](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    };
    std::function<QString(const QString&)> report_export_path_picker;
};

inline std::filesystem::path resolve_import_artifact_path(const std::filesystem::path& base_path,
                                                   const aegis::storage::SourceArtifact& artifact)
{
    return artifact.path.is_absolute() ? artifact.path : (base_path / artifact.path);
}

inline bool imported_package_inputs_exist(const aegis::storage::ProjectPackage& package,
                                   const std::filesystem::path& base_path)
{
    if (package.artifacts().empty()) {
        return false;
    }
    return std::all_of(package.artifacts().begin(), package.artifacts().end(), [&](const auto& artifact) {
        return std::filesystem::exists(resolve_import_artifact_path(base_path, artifact));
    });
}

inline QString metadata_value(const aegis::rules::Violation& violation, std::initializer_list<const char*> keys)
{
    for (const auto* key : keys) {
        if (const auto value = violation.metadata.get<std::string>(key); value.has_value() && !value->empty()) {
            return QString::fromStdString(*value);
        }
    }
    return {};
}

inline bool violation_matches_artifact(const aegis::rules::Violation& violation, const QString& artifact_id, const QString& artifact_path)
{
    const QString violation_artifact_id = metadata_value(violation, {"artifact_id", "source_artifact_id"});
    if (!artifact_id.trimmed().isEmpty() && !violation_artifact_id.trimmed().isEmpty()
        && violation_artifact_id.compare(artifact_id, Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString violation_artifact_path = metadata_value(violation, {"artifact_path", "source_artifact_path"});
    if (!artifact_path.trimmed().isEmpty() && !violation_artifact_path.trimmed().isEmpty()) {
        return QFileInfo(violation_artifact_path).filePath().compare(QFileInfo(artifact_path).filePath(), Qt::CaseInsensitive) == 0;
    }
    return false;
}

inline QString html_escape(QString text)
{
    text.replace('&', "&amp;");
    text.replace('<', "&lt;");
    text.replace('>', "&gt;");
    text.replace('"', "&quot;");
    return text;
}

} // namespace aegis::ui

namespace aegis::ui {
using MainWindowState = MainWindow::Impl;
} // namespace aegis::ui
