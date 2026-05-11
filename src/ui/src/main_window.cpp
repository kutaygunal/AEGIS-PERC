#include "aegis/ui/main_window.hpp"
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
namespace {

using ActionMap = std::map<QString, QAction*>;

constexpr int kWorkspaceUiStateVersion = 1;
constexpr int kRecentProjectsStateVersion = 1;
constexpr auto kSettingsMainWindowGroup = "mainWindow";
constexpr auto kSettingsWorkspaceUiGroup = "mainWindow/workspaceUi";
constexpr auto kSettingsRecentProjectsGroup = "mainWindow/recentProjects";
constexpr auto kSettingsOnboardingGroup = "mainWindow/onboarding";

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

QString normalize_saved_name(const QString& name)
{
    return name.trimmed();
}

QString metadata_value(const aegis::rules::Violation& violation, std::initializer_list<const char*> keys)
{
    for (const auto* key : keys) {
        if (const auto value = violation.metadata.get<std::string>(key); value.has_value() && !value->empty()) {
            return QString::fromStdString(*value);
        }
    }
    return {};
}

bool violation_matches_artifact(const aegis::rules::Violation& violation, const QString& artifact_id, const QString& artifact_path)
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

bool saved_name_matches(const QString& lhs, const QString& rhs)
{
    return normalize_saved_name(lhs).compare(normalize_saved_name(rhs), Qt::CaseInsensitive) == 0;
}

QVariantMap to_variant_map(const SavedFilterPreset& preset)
{
    QVariantMap map;
    map.insert("name", preset.name);
    map.insert("state", preset.state.to_variant_map());
    return map;
}

std::optional<SavedFilterPreset> saved_filter_preset_from_variant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    const QString name = normalize_saved_name(map.value("name").toString());
    if (name.isEmpty()) {
        return std::nullopt;
    }

    SavedFilterPreset preset;
    preset.name = name;
    preset.state = ViolationFilterState::from_variant_map(map.value("state").toMap());
    return preset;
}

QVariantMap to_variant_map(const SavedWorkspaceView& view)
{
    QVariantMap map;
    map.insert("name", view.name);
    map.insert("dockState", view.dock_state);
    map.insert("gridVisible", view.grid_visible);
    map.insert("overlaysVisible", view.overlays_visible);
    map.insert("heatmapVisible", view.heatmap_visible);
    map.insert("heatmapOpacity", view.heatmap_opacity);
    map.insert("performanceMetricsVisible", view.performance_metrics_visible);
    return map;
}

std::optional<SavedWorkspaceView> saved_workspace_view_from_variant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    const QString name = normalize_saved_name(map.value("name").toString());
    if (name.isEmpty()) {
        return std::nullopt;
    }

    SavedWorkspaceView view;
    view.name = name;
    view.dock_state = map.value("dockState").toByteArray();
    view.grid_visible = map.value("gridVisible", true).toBool();
    view.overlays_visible = map.value("overlaysVisible", true).toBool();
    view.heatmap_visible = map.value("heatmapVisible", false).toBool();
    view.heatmap_opacity = map.value("heatmapOpacity", 0.6).toDouble();
    view.performance_metrics_visible = map.value("performanceMetricsVisible", false).toBool();
    return view;
}

template <typename Entry>
QStringList saved_entry_names(const std::vector<Entry>& entries)
{
    QStringList names;
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    return names;
}

struct BundledSampleInfo {
    QString id;
    QString file_name;
    QString display_name;
    QString description;
};

const std::array<BundledSampleInfo, 3>& bundled_samples()
{
    static const std::array<BundledSampleInfo, 3> samples{{
        {"inverter", "inverter.json", "Inverter", "Minimal inverter sample"},
        {"nand2", "nand2.json", "NAND2", "Two-input NAND gate sample"},
        {"ring_oscillator", "ring_oscillator.json", "Ring Oscillator", "Five-stage ring oscillator sample"},
    }};
    return samples;
}

const BundledSampleInfo* bundled_sample_by_id(const QString& sample_id)
{
    const auto& samples = bundled_samples();
    const auto it = std::find_if(samples.begin(), samples.end(), [&sample_id](const BundledSampleInfo& sample) {
        return sample.id.compare(sample_id.trimmed(), Qt::CaseInsensitive) == 0;
    });
    return it != samples.end() ? &(*it) : nullptr;
}

std::filesystem::path sample_design_path(const char* name)
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "data" / "sample_designs" / name;
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("data") / "sample_designs" / name;
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "data" / "sample_designs" / name;
}

std::filesystem::path project_readme_path()
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "README.md";
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("README.md");
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "README.md";
}

std::filesystem::path docs_directory_path()
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "docs";
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("docs");
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "docs";
}

std::optional<aegis::parsing::LayoutIR> load_sample_design_ir(const char* name)
{
    std::ifstream input(sample_design_path(name), std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    nlohmann::json j;
    input >> j;

    aegis::parsing::LayoutIR ir;
    ir.version = aegis::parsing::LayoutIR::CURRENT_VERSION;
    ir.design_name = j.at("design").at("name").get<std::string>();
    ir.description = j.at("design").at("description").get<std::string>();

    static const std::vector<std::string> colors{"#6AA84F", "#CC0000", "#3C78D8", "#F1C232"};
    const auto& layers = j.at("layout").at("layers");
    for (std::size_t i = 0; i < layers.size(); ++i) {
        ir.layers.push_back(aegis::parsing::Layer{
            layers.at(i).get<std::string>(),
            "sample",
            static_cast<int>(i),
            colors.at(i % colors.size())
        });
    }

    for (const auto& shape : j.at("layout").at("shapes")) {
        if (!shape.contains("bbox") || shape.at("bbox").size() != 4) {
            continue;
        }
        const auto& bbox = shape.at("bbox");
        const double x1 = bbox.at(0).get<double>();
        const double y1 = bbox.at(1).get<double>();
        const double x2 = bbox.at(2).get<double>();
        const double y2 = bbox.at(3).get<double>();
        ir.geometries.push_back(aegis::parsing::Geometry{
            shape.at("layer").get<std::string>(),
            aegis::parsing::Rectangle{x1, y1, x2 - x1, y2 - y1}
        });
    }

    for (const auto& net : j.at("netlist").at("nets")) {
        aegis::parsing::Net ir_net;
        ir_net.name = net.at("name").get<std::string>();
        if (net.contains("type")) {
            ir_net.properties["type"] = net.at("type").get<std::string>();
        }
        if (net.contains("connections")) {
            for (const auto& connection : net.at("connections")) {
                const std::string device = connection.at("device").get<std::string>();
                const std::string pin = connection.at("pin").get<std::string>();
                ir_net.pin_names.push_back(device + "." + pin);
            }
        }
        ir.nets.push_back(std::move(ir_net));
    }

    for (const auto& device : j.at("netlist").at("devices")) {
        aegis::parsing::Device ir_device;
        ir_device.name = device.at("name").get<std::string>();
        ir_device.type = device.at("type").get<std::string>();
        if (device.contains("pins")) {
            for (auto it = device.at("pins").begin(); it != device.at("pins").end(); ++it) {
                ir_device.pins[it.key()] = it.value().get<std::string>();
            }
        }
        if (device.contains("properties")) {
            for (auto it = device.at("properties").begin(); it != device.at("properties").end(); ++it) {
                ir_device.properties[it.key()] = it.value().dump();
            }
        }
        ir.devices.push_back(std::move(ir_device));
    }

    for (const auto& port : j.at("netlist").at("ports")) {
        std::string direction = port.at("direction").get<std::string>();
        std::transform(direction.begin(), direction.end(), direction.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        ir.ports.push_back(aegis::parsing::Port{
            port.at("name").get<std::string>(),
            direction,
            port.at("net").get<std::string>(),
            std::nullopt,
            std::nullopt
        });
    }

    return ir;
}

void configure_action(QAction* action, const QString& tooltip, const QString& object_name = {})
{
    if (action == nullptr) {
        return;
    }
    action->setToolTip(tooltip);
    action->setStatusTip(tooltip);
    action->setShortcutVisibleInContextMenu(true);
    if (!object_name.trimmed().isEmpty()) {
        action->setObjectName(object_name);
    }
}

template <typename Widget>
Widget* configure_accessible_widget(Widget* widget,
                                    const QString& accessible_name,
                                    const QString& tooltip = {},
                                    const QString& accessible_description = {})
{
    if (widget == nullptr) {
        return nullptr;
    }
    widget->setAccessibleName(accessible_name);
    if (!accessible_description.trimmed().isEmpty()) {
        widget->setAccessibleDescription(accessible_description);
    }
    if (!tooltip.trimmed().isEmpty()) {
        widget->setToolTip(tooltip);
        widget->setStatusTip(tooltip);
    }
    return widget;
}

QString import_summary_text(const aegis::storage::ProjectPackage& package)
{
    QStringList lines;
    lines.append(QString("Project: %1").arg(QString::fromStdString(package.project().name)));
    lines.append(QString("Validation: %1").arg(QString::fromStdString(aegis::storage::to_string(package.validation_status()))));
    lines.append(QString("Artifacts: %1").arg(package.artifacts().size()));
    lines.append(QString("Diagnostics: %1").arg(package.diagnostics().size()));
    lines.append(QString{});
    lines.append("Detected artifacts:");
    for (const auto& artifact : package.artifacts()) {
        lines.append(QString("- %1 [%2/%3]%4")
                         .arg(QString::fromStdString(artifact.path.generic_string()))
                         .arg(QString::fromStdString(aegis::storage::to_string(artifact.category)))
                         .arg(QString::fromStdString(aegis::storage::to_string(artifact.role)))
                         .arg(artifact.optional ? " optional" : " required-candidate"));
    }
    if (package.artifacts().empty()) {
        lines.append("- No recognized artifacts");
    }
    lines.append(QString{});
    lines.append("Diagnostics:");
    for (const auto& diagnostic : package.diagnostics()) {
        lines.append(QString("- [%1] %2")
                         .arg(QString::fromStdString(aegis::storage::to_string(diagnostic.severity)))
                         .arg(QString::fromStdString(diagnostic.message)));
    }
    if (package.diagnostics().empty()) {
        lines.append("- No diagnostics");
    }
    return lines.join('\n');
}

QString imported_design_session_summary_text(const aegis::storage::ImportedDesignSession* session)
{
    if (session == nullptr) {
        return QString("Imported design session: unavailable");
    }

    return QString("Imported design session\n"
                   "Status: %1\n"
                   "Technology libraries: %2\n"
                   "Design layers: %3\n"
                   "Instances: %4\n"
                   "Ports: %5\n"
                   "Nets: %6\n"
                   "Devices: %7\n"
                   "Rule packs: %8\n"
                   "Graph nodes: %9\n"
                   "Session diagnostics: %10")
        .arg(QString::fromStdString(aegis::storage::to_string(session->status())))
        .arg(session->technology_libraries().size())
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Layer))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Instance))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Port))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Net))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Device))
        .arg(session->rule_artifact_ids().size())
        .arg(session->graph().node_count())
        .arg(session->diagnostics().size());
}

QStringList choose_import_paths(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Import Design Package");
    dialog.setModal(true);
    dialog.resize(420, 160);

    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel("Choose a customer project folder or select individual design-package files.", &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* folder_button = buttons->addButton("Choose Project Folder...", QDialogButtonBox::ActionRole);
    auto* files_button = buttons->addButton("Choose Files...", QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    QStringList selected_paths;
    QObject::connect(folder_button, &QPushButton::clicked, &dialog, [&dialog, parent, &selected_paths]() {
        const QString folder = QFileDialog::getExistingDirectory(parent,
                                                                 "Select Project Folder",
                                                                 QString{},
                                                                 QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (folder.trimmed().isEmpty()) {
            return;
        }
        selected_paths = {folder};
        dialog.accept();
    });
    QObject::connect(files_button, &QPushButton::clicked, &dialog, [&dialog, parent, &selected_paths]() {
        const QStringList files = QFileDialog::getOpenFileNames(parent,
                                                                "Select Design Package Files",
                                                                QString{},
                                                                "All Files (*.*)");
        if (files.isEmpty()) {
            return;
        }
        selected_paths = files;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return selected_paths;
}

QString artifact_requirement_text(const aegis::storage::SourceArtifact& artifact)
{
    return artifact.optional ? "Optional" : "Required candidate";
}

QString artifact_status_text(const aegis::storage::ProjectPackage& package,
                             const aegis::storage::SourceArtifact& artifact)
{
    bool has_warning = false;
    int related_count = 0;
    for (const auto& diagnostic : package.diagnostics()) {
        if (!diagnostic.artifact_id.has_value() || *diagnostic.artifact_id != artifact.id) {
            continue;
        }
        ++related_count;
        if (diagnostic.severity == aegis::storage::DiagnosticSeverity::Error) {
            return QString("error (%1)").arg(related_count);
        }
        if (diagnostic.severity == aegis::storage::DiagnosticSeverity::Warning) {
            has_warning = true;
        }
    }
    if (has_warning) {
        return QString("warning (%1)").arg(related_count);
    }

    const bool package_missing_required_inputs = std::any_of(package.diagnostics().begin(), package.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.code == "MISSING_REQUIRED_TECHNOLOGY" ||
               diagnostic.code == "MISSING_REQUIRED_LAYOUT" ||
               diagnostic.code == "MISSING_REQUIRED_NETLIST" ||
               diagnostic.code == "MISSING_REQUIRED_RULES";
    });
    if (package_missing_required_inputs
        && (artifact.category == aegis::storage::ArtifactCategory::ExternalReports
            || artifact.category == aegis::storage::ArtifactCategory::Unknown)) {
        return "warning (review role)";
    }

    return related_count > 0 ? QString("info (%1)").arg(related_count) : QString("ok");
}

QString import_diagnostics_text(const aegis::storage::ProjectPackage& package)
{
    QStringList lines;
    for (const auto& diagnostic : package.diagnostics()) {
        lines.append(QString("- [%1] %2")
                         .arg(QString::fromStdString(aegis::storage::to_string(diagnostic.severity)))
                         .arg(QString::fromStdString(diagnostic.message)));
    }
    if (lines.isEmpty()) {
        lines.append("- No diagnostics");
    }
    return lines.join('\n');
}

std::filesystem::path resolve_import_artifact_path(const std::filesystem::path& base_path,
                                                   const aegis::storage::SourceArtifact& artifact)
{
    return artifact.path.is_absolute() ? artifact.path : (base_path / artifact.path);
}

std::vector<aegis::storage::ArtifactRole> supported_import_roles()
{
    using aegis::storage::ArtifactRole;
    return {
        ArtifactRole::Lef,
        ArtifactRole::Def,
        ArtifactRole::Verilog,
        ArtifactRole::SystemVerilog,
        ArtifactRole::Spice,
        ArtifactRole::Spi,
        ArtifactRole::Cdl,
        ArtifactRole::AegisRulePack,
        ArtifactRole::PowerDomainsCsv,
        ArtifactRole::CurrentCsv,
        ArtifactRole::WaiverCsv,
        ArtifactRole::WaiverYaml,
        ArtifactRole::WaiverJson,
        ArtifactRole::ImportedReport,
        ArtifactRole::Unknown,
    };
}

bool imported_package_inputs_exist(const aegis::storage::ProjectPackage& package,
                                   const std::filesystem::path& base_path)
{
    if (package.artifacts().empty()) {
        return false;
    }
    return std::all_of(package.artifacts().begin(), package.artifacts().end(), [&](const auto& artifact) {
        return std::filesystem::exists(resolve_import_artifact_path(base_path, artifact));
    });
}

QString html_escape(QString text)
{
    text.replace('&', "&amp;");
    text.replace('<', "&lt;");
    text.replace('>', "&gt;");
    text.replace('"', "&quot;");
    return text;
}

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
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
    ActionMap actions;
    std::vector<JobHistoryEntry> job_history;
    int job_history_max_entries = 12;
    std::function<QStringList(QWidget*)> import_picker;
    std::function<bool(const QString&)> report_opener = [](const QString& path) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    };
    std::function<QString(const QString&)> report_export_path_picker;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_impl(std::make_unique<Impl>())
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
    viewMenu->addAction(m_impl->actions.at("toggle_grid"));
    viewMenu->addAction(m_impl->actions.at("toggle_overlays"));
    m_impl->filter_presets_menu = viewMenu->addMenu("Filter &Presets");
    connect(m_impl->filter_presets_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_filter_preset_menu);
    m_impl->workspace_views_menu = viewMenu->addMenu("Workspace &Views");
    connect(m_impl->workspace_views_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_workspace_view_menu);

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

void MainWindow::execute_run_checks()
{
    if (m_impl->has_loaded_import_package) {
        Q_UNUSED(start_imported_run_checks(false));
        return;
    }

    if (m_impl->current_graph == nullptr) {
        const QString message = "Run Checks unavailable: no connectivity graph available";
        publish_ui_notification(message, ActivityLogSeverity::Error, 4000);
        return;
    }

    publish_ui_notification("Run Checks started using built-in desktop defaults", ActivityLogSeverity::Info, 2000);

    try {
        aegis::rules::RuleEngine engine;
        engine.register_rule(std::make_unique<aegis::rules::FloatingNetRule>());
        engine.register_rule(std::make_unique<aegis::rules::OpenCircuitRule>());
        engine.register_rule(std::make_unique<aegis::rules::ShortCircuitRule>());
        engine.register_rule(std::make_unique<aegis::rules::DomainTaggingRule>());

        aegis::rules::RuleContext context{
            *m_impl->current_graph,
            aegis::graph::PropertyMap{},
            m_impl->canvas != nullptr ? m_impl->canvas->scene().design_name : std::string{}
        };

        auto violations = engine.run_all(context);
        aegis::rules::ViolationCollection collection{std::move(violations)};
        set_violations(collection);

        const QString message = QString("Run Checks completed: %1 violation(s) using built-in desktop defaults")
                                    .arg(collection.size());
        publish_ui_notification(message,
                                collection.empty() ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                5000);
    } catch (const std::exception& error) {
        const QString message = QString("Run Checks failed: %1").arg(error.what());
        publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
    }
}

bool MainWindow::export_report_preview(bool html_export)
{
    if (m_impl->report_preview == nullptr) {
        return false;
    }
    const QString format = html_export ? QString("html") : QString("json");
    QString target_path;
    if (m_impl->report_export_path_picker) {
        target_path = m_impl->report_export_path_picker(format);
    } else {
        target_path = QFileDialog::getSaveFileName(this,
                                                   html_export ? "Export HTML Report" : "Export JSON Report",
                                                   html_export ? "aegis_report.html" : "aegis_report.json",
                                                   html_export ? "HTML Files (*.html)" : "JSON Files (*.json)");
    }
    if (target_path.trimmed().isEmpty()) {
        m_impl->report_preview->set_action_status_for_host(html_export
            ? "HTML export canceled"
            : "JSON export canceled");
        publish_ui_notification(html_export ? "HTML export canceled" : "JSON export canceled", ActivityLogSeverity::Info, 3000);
        return false;
    }

    const auto filtered = m_impl->violation_explorer != nullptr
        ? m_impl->violation_explorer->filtered_violations()
        : aegis::rules::ViolationCollection{};
    std::vector<aegis::rules::Violation> violations;
    violations.reserve(filtered.size());
    for (const auto& violation : filtered.violations()) {
        violations.push_back(violation);
    }

    nlohmann::json import_diagnostics = nlohmann::json::array();
    if (m_impl->has_loaded_import_package) {
        for (const auto& diagnostic : m_impl->loaded_import_package.diagnostics()) {
            import_diagnostics.push_back({
                {"severity", aegis::storage::to_string(diagnostic.severity)},
                {"code", diagnostic.code},
                {"message", diagnostic.message},
                {"artifact_id", diagnostic.artifact_id.has_value() ? nlohmann::json(*diagnostic.artifact_id) : nlohmann::json(nullptr)}
            });
        }
    }

    nlohmann::json runtime_diagnostics = nlohmann::json::array();
    for (auto it = m_impl->job_history.rbegin(); it != m_impl->job_history.rend(); ++it) {
        if (it->json_report_path.has_value() && std::filesystem::exists(*it->json_report_path)) {
            try {
                std::ifstream input(*it->json_report_path);
                nlohmann::json existing;
                input >> existing;
                if (existing.contains("runtime_diagnostics") && existing["runtime_diagnostics"].is_array()) {
                    runtime_diagnostics = existing["runtime_diagnostics"];
                }
            } catch (...) {
            }
            break;
        }
    }
    if (runtime_diagnostics.empty() && !m_impl->job_history.empty()) {
        const auto& latest = m_impl->job_history.back();
        if (!latest.error_message.trimmed().isEmpty()) {
            runtime_diagnostics.push_back({{"severity", "error"}, {"message", latest.error_message.toStdString()}});
        }
        for (const auto& line : latest.progress_history) {
            runtime_diagnostics.push_back({{"severity", "info"}, {"message", line.toStdString()}});
        }
    }

    nlohmann::json export_json{
        {"project", {
            {"design_name", m_impl->canvas != nullptr ? m_impl->canvas->scene().design_name : std::string{}},
            {"imported_project_name", m_impl->has_loaded_import_package ? m_impl->loaded_import_package.project().name : std::string{}},
            {"validation_status", m_impl->has_loaded_import_package ? aegis::storage::to_string(m_impl->loaded_import_package.validation_status()) : std::string{"not_imported"}}
        }},
        {"summary", {
            {"text", m_impl->report_preview->summary_text().toStdString()},
            {"snapshot_status", m_impl->report_preview->snapshot_status_text().toStdString()},
            {"activity_log", activity_log_entries().join('\n').toStdString()},
            {"violation_count", violations.size()}
        }},
        {"import_diagnostics", import_diagnostics},
        {"runtime_diagnostics", runtime_diagnostics},
        {"violations", violations}
    };

    try {
        std::filesystem::create_directories(std::filesystem::path(target_path.toStdString()).parent_path());
        if (html_export) {
            QStringList lines;
            lines.append("<!doctype html><html><head><meta charset=\"utf-8\"><title>AEGIS-PERC Report Preview Export</title></head><body>");
            lines.append("<h1>AEGIS-PERC Report Preview Export</h1>");
            lines.append(QString("<p><strong>Design:</strong> %1</p>").arg(html_escape(m_impl->canvas != nullptr
                ? QString::fromStdString(m_impl->canvas->scene().design_name)
                : QString{})));
            if (m_impl->has_loaded_import_package) {
                lines.append(QString("<p><strong>Imported project:</strong> %1</p>").arg(html_escape(QString::fromStdString(m_impl->loaded_import_package.project().name))));
            }
            lines.append(QString("<p><strong>Snapshot:</strong> %1</p>").arg(html_escape(m_impl->report_preview->snapshot_status_text())));
            lines.append(QString("<h2>Summary</h2><pre>%1</pre>").arg(html_escape(m_impl->report_preview->summary_text())));
            lines.append("<h2>Violations</h2><ul>");
            for (const auto& violation : violations) {
                lines.append(QString("<li>[%1] %2: %3</li>")
                                 .arg(html_escape(QString::fromStdString(aegis::rules::severity_to_string(violation.severity))))
                                 .arg(html_escape(QString::fromStdString(violation.rule_id)))
                                 .arg(html_escape(QString::fromStdString(violation.message))));
            }
            if (violations.empty()) {
                lines.append("<li>No violations in current preview</li>");
            }
            lines.append("</ul><h2>Import Diagnostics</h2><ul>");
            if (import_diagnostics.empty()) {
                lines.append("<li>No import diagnostics</li>");
            } else {
                for (const auto& diagnostic : import_diagnostics) {
                    lines.append(QString("<li>[%1] %2</li>")
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("severity", "info"))))
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("message", "")))));
                }
            }
            lines.append("</ul><h2>Runtime Diagnostics</h2><ul>");
            if (runtime_diagnostics.empty()) {
                lines.append("<li>No runtime diagnostics</li>");
            } else {
                for (const auto& diagnostic : runtime_diagnostics) {
                    lines.append(QString("<li>[%1] %2</li>")
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("severity", "info"))))
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("message", "")))));
                }
            }
            lines.append("</ul></body></html>");
            std::ofstream out(target_path.toStdString(), std::ios::binary);
            out << lines.join('\n').toStdString();
        } else {
            std::ofstream out(target_path.toStdString(), std::ios::binary);
            out << export_json.dump(2);
        }
    } catch (const std::exception& error) {
        const QString message = QString("Report export failed: %1").arg(error.what());
        m_impl->report_preview->set_action_status_for_host(message);
        publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
        return false;
    }

    const QString message = QString("Exported report preview to %1").arg(target_path);
    m_impl->report_preview->set_action_status_for_host(message);
    publish_ui_notification(message, ActivityLogSeverity::Info, 5000);
    return true;
}

bool MainWindow::start_imported_run_checks(bool is_retry)
{
    if (m_impl->active_job_id.has_value()) {
        publish_ui_notification("Run Checks already in progress through the local job pipeline", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    if (!m_impl->has_loaded_import_package) {
        publish_ui_notification("Run Checks unavailable: no imported package is loaded", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    if (!imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path)) {
        m_impl->last_job_retry_available = false;
        m_impl->last_job_retry_reason = "Retry unavailable: imported package inputs are missing on disk";
        publish_ui_notification(m_impl->last_job_retry_reason, ActivityLogSeverity::Error, 5000);
        update_action_states();
        return false;
    }

    aegis::orchestration::JobRequest request;
    request.package = m_impl->loaded_import_package;
    request.base_path = m_impl->loaded_import_base_path;
    request.options = m_impl->job_pipeline_options;
    m_impl->active_job_output_dir = std::filesystem::temp_directory_path() /
                                    ("aegis_ui_run_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    request.output_dir = m_impl->active_job_output_dir;
    request.progress_callback = [this](const aegis::orchestration::JobProgressSnapshot& snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot]() {
            update_job_progress_ui(snapshot);
            const QString message = QString("Run Checks pipeline: %1 (%2/%3) — %4")
                                        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
                                        .arg(snapshot.completed_stages)
                                        .arg(snapshot.total_stages)
                                        .arg(QString::fromStdString(snapshot.message));
            publish_ui_notification(message, ActivityLogSeverity::Info, 1500);
        }, Qt::QueuedConnection);
    };

    const auto job_id = m_impl->job_pipeline.submit(std::move(request));
    m_impl->active_job_id = job_id;
    m_impl->last_job_retry_available = false;
    m_impl->last_job_retry_reason = "Retry unavailable while a local job is active";
    m_impl->job_history.push_back({job_id,
                                   "Run Checks",
                                   QString::fromStdString(m_impl->loaded_import_package.project().name),
                                   "queued",
                                   QString("Queued imported Run Checks job"),
                                   QString{},
                                   QString{},
                                   {},
                                   QDateTime::currentDateTime(),
                                   {},
                                   std::nullopt,
                                   std::nullopt});
    if (static_cast<int>(m_impl->job_history.size()) > m_impl->job_history_max_entries) {
        const auto excess = static_cast<int>(m_impl->job_history.size()) - m_impl->job_history_max_entries;
        m_impl->job_history.erase(m_impl->job_history.begin(),
                                  m_impl->job_history.begin() + excess);
    }
    update_job_progress_ui({job_id,
                            aegis::orchestration::JobState::Queued,
                            aegis::orchestration::JobStage::None,
                            0,
                            5,
                            false,
                            m_impl->loaded_import_package.project().name,
                            "Queued imported Run Checks job",
                            std::nullopt,
                            std::nullopt});
    if (m_impl->job_poll_timer == nullptr) {
        m_impl->job_poll_timer = new QTimer(this);
        m_impl->job_poll_timer->setInterval(25);
        connect(m_impl->job_poll_timer, &QTimer::timeout, this, &MainWindow::finalize_active_job);
    }
    m_impl->job_poll_timer->start();
    publish_ui_notification(is_retry
                                ? "Retrying Run Checks using imported package content via local job pipeline"
                                : "Run Checks started using imported package content via local job pipeline",
                            ActivityLogSeverity::Info,
                            2000);
    update_action_states();
    return true;
}

bool MainWindow::can_retry_last_job() const
{
    return !m_impl->active_job_id.has_value()
        && m_impl->has_loaded_import_package
        && m_impl->last_job_retry_available
        && imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path);
}

void MainWindow::update_job_progress_ui(const aegis::orchestration::JobProgressSnapshot& snapshot)
{
    m_impl->last_job_snapshot = snapshot;
    m_impl->last_job_progress_text = QString("Job: %1 (%2/%3) — %4")
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
        .arg(snapshot.completed_stages)
        .arg(snapshot.total_stages)
        .arg(QString::fromStdString(snapshot.message));
    if (m_impl->job_progress_label != nullptr) {
        m_impl->job_progress_label->setText(m_impl->last_job_progress_text);
        m_impl->job_progress_label->setVisible(snapshot.state == aegis::orchestration::JobState::Running
                                               || snapshot.state == aegis::orchestration::JobState::Queued
                                               || snapshot.state == aegis::orchestration::JobState::Cancelling);
    }

    const QString progress_line = QString("%1 | %2 (%3/%4) | %5")
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.state)))
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
        .arg(snapshot.completed_stages)
        .arg(snapshot.total_stages)
        .arg(QString::fromStdString(snapshot.message));
    for (auto& entry : m_impl->job_history) {
        if (entry.job_id != snapshot.job_id) {
            continue;
        }
        entry.state = QString::fromStdString(aegis::orchestration::to_string(snapshot.state));
        entry.summary = QString("%1 — %2").arg(entry.action_name, progress_line);
        if (entry.progress_history.isEmpty() || entry.progress_history.back() != progress_line) {
            entry.progress_history.push_back(progress_line);
        }
        entry.json_report_path = snapshot.json_report_path;
        entry.html_report_path = snapshot.html_report_path;
        break;
    }
    refresh_job_history_panel();
    update_action_states();
}

void MainWindow::refresh_job_history_panel()
{
    if (m_impl->job_history_list == nullptr || m_impl->job_history_details == nullptr
        || m_impl->job_history_open_json_button == nullptr || m_impl->job_history_open_html_button == nullptr) {
        return;
    }

    const int current_row = m_impl->job_history_list->currentRow();
    m_impl->job_history_list->blockSignals(true);
    m_impl->job_history_list->clear();
    for (const auto& entry : m_impl->job_history) {
        const QString started = entry.started_at.isValid()
            ? entry.started_at.toString(Qt::ISODate)
            : QString("unknown-start");
        m_impl->job_history_list->addItem(QString("[%1] %2 — %3 — %4")
                                              .arg(entry.state, entry.action_name, entry.project_name, started));
    }
    m_impl->job_history_list->blockSignals(false);

    if (m_impl->job_history.empty()) {
        m_impl->job_history_details->setPlainText("No local workflow jobs yet.");
        m_impl->job_history_open_json_button->setEnabled(false);
        m_impl->job_history_open_html_button->setEnabled(false);
        return;
    }

    const int bounded_row = std::clamp(current_row < 0 ? static_cast<int>(m_impl->job_history.size()) - 1 : current_row,
                                       0,
                                       static_cast<int>(m_impl->job_history.size()) - 1);
    const bool restore_signals = m_impl->job_history_list->blockSignals(true);
    m_impl->job_history_list->setCurrentRow(bounded_row);
    m_impl->job_history_list->blockSignals(restore_signals);

    const auto& entry = m_impl->job_history.at(static_cast<std::size_t>(bounded_row));
    QStringList lines;
    lines.append(QString("Action: %1").arg(entry.action_name));
    lines.append(QString("Project: %1").arg(entry.project_name));
    lines.append(QString("State: %1").arg(entry.state));
    lines.append(QString("Started: %1").arg(entry.started_at.isValid() ? entry.started_at.toString(Qt::ISODate) : QString("n/a")));
    lines.append(QString("Finished: %1").arg(entry.finished_at.isValid() ? entry.finished_at.toString(Qt::ISODate) : QString("in progress")));
    if (!entry.result_summary.trimmed().isEmpty()) {
        lines.append(QString("Result: %1").arg(entry.result_summary));
    }
    if (!entry.error_message.trimmed().isEmpty()) {
        lines.append(QString("Error: %1").arg(entry.error_message));
    }
    if (entry.json_report_path.has_value()) {
        lines.append(QString("JSON report: %1").arg(QString::fromStdString(entry.json_report_path->string())));
    }
    if (entry.html_report_path.has_value()) {
        lines.append(QString("HTML report: %1").arg(QString::fromStdString(entry.html_report_path->string())));
    }
    lines.append(QString{});
    lines.append("Progress history:");
    if (entry.progress_history.isEmpty()) {
        lines.append("- No progress updates recorded");
    } else {
        for (const auto& progress : entry.progress_history) {
            lines.append(QString("- %1").arg(progress));
        }
    }
    m_impl->job_history_details->setPlainText(lines.join('\n'));
    m_impl->job_history_open_json_button->setEnabled(entry.json_report_path.has_value() && std::filesystem::exists(*entry.json_report_path));
    m_impl->job_history_open_html_button->setEnabled(entry.html_report_path.has_value() && std::filesystem::exists(*entry.html_report_path));
}

bool MainWindow::open_selected_job_history_report(bool html_report)
{
    if (m_impl->job_history_list == nullptr) {
        return false;
    }
    const int row = m_impl->job_history_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_impl->job_history.size())) {
        publish_ui_notification("Job report open unavailable: no recent job selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const auto& entry = m_impl->job_history.at(static_cast<std::size_t>(row));
    const auto& path = html_report ? entry.html_report_path : entry.json_report_path;
    if (!path.has_value() || !std::filesystem::exists(*path)) {
        publish_ui_notification(html_report ? "HTML report unavailable for selected job" : "JSON report unavailable for selected job",
                                ActivityLogSeverity::Warning,
                                4000);
        refresh_job_history_panel();
        return false;
    }

    const QString report_path = QString::fromStdString(path->string());
    if (m_impl->report_opener && !m_impl->report_opener(report_path)) {
        publish_ui_notification(QString("Failed to open job report: %1").arg(report_path), ActivityLogSeverity::Error, 5000);
        return false;
    }

    publish_ui_notification(QString("Opened job report: %1").arg(report_path), ActivityLogSeverity::Info, 4000);
    return true;
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
    if (!m_impl->active_job_id.has_value()) {
        if (m_impl->job_poll_timer != nullptr) {
            m_impl->job_poll_timer->stop();
        }
        return;
    }

    if (const auto snapshot = m_impl->job_pipeline.snapshot(*m_impl->active_job_id); snapshot.has_value()) {
        update_job_progress_ui(*snapshot);
    }

    const auto result = m_impl->job_pipeline.result(*m_impl->active_job_id);
    if (!result.has_value() || (result->state != aegis::orchestration::JobState::Completed
                                && result->state != aegis::orchestration::JobState::Failed
                                && result->state != aegis::orchestration::JobState::Cancelled)) {
        return;
    }

    if (m_impl->job_poll_timer != nullptr) {
        m_impl->job_poll_timer->stop();
    }

    if (m_impl->job_progress_label != nullptr) {
        m_impl->job_progress_label->setVisible(false);
    }

    QString history_result_summary;
    QString history_error_message;
    if (result->state == aegis::orchestration::JobState::Completed) {
        aegis::rules::ViolationCollection collection{result->violations};
        set_violations(collection);
        m_impl->last_job_retry_available = false;
        m_impl->last_job_retry_reason = "Retry unavailable: last local job completed successfully";
        history_result_summary = QString("Completed with %1 violation(s)").arg(collection.size());
        const QString message = QString("Run Checks completed: %1 violation(s) using imported package content via local job pipeline")
                                    .arg(collection.size());
        publish_ui_notification(message,
                                collection.empty() ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                5000);
    } else if (result->state == aegis::orchestration::JobState::Cancelled) {
        m_impl->last_job_retry_available = imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path);
        m_impl->last_job_retry_reason = m_impl->last_job_retry_available
            ? QString("Retry available for canceled local job")
            : QString("Retry unavailable: imported package inputs are missing on disk");
        history_result_summary = "Cancelled";
        publish_ui_notification("Run Checks canceled in local job pipeline", ActivityLogSeverity::Warning, 5000);
    } else {
        m_impl->last_job_retry_available = imported_package_inputs_exist(m_impl->loaded_import_package, m_impl->loaded_import_base_path);
        m_impl->last_job_retry_reason = m_impl->last_job_retry_available
            ? QString("Retry available for failed local job")
            : QString("Retry unavailable: imported package inputs are missing on disk");
        history_result_summary = "Failed";
        history_error_message = QString::fromStdString(result->error_message);
        const QString message = QString("Run Checks failed: %1").arg(QString::fromStdString(result->error_message));
        publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
        if (m_impl->last_job_retry_available) {
            append_activity_log("Retry available for failed local job", ActivityLogSeverity::Info);
        }
    }

    for (auto& entry : m_impl->job_history) {
        if (entry.job_id != result->job_id) {
            continue;
        }
        entry.state = QString::fromStdString(aegis::orchestration::to_string(result->state));
        entry.result_summary = history_result_summary;
        entry.error_message = history_error_message;
        entry.finished_at = QDateTime::currentDateTime();
        entry.json_report_path = result->json_report_path;
        entry.html_report_path = result->html_report_path;
        entry.summary = QString("%1 — %2").arg(entry.action_name, history_result_summary);
        break;
    }

    m_impl->active_job_id.reset();
    refresh_job_history_panel();
    update_action_states();
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
    m_impl->diagnostics_entries.clear();

    const auto* package = !m_impl->pending_import_package.artifacts().empty()
        ? &m_impl->pending_import_package
        : (m_impl->has_loaded_import_package ? &m_impl->loaded_import_package : nullptr);
    if (package != nullptr) {
        for (const auto& diagnostic : package->diagnostics()) {
            MainWindow::Impl::DiagnosticEntry entry;
            entry.severity = QString::fromStdString(aegis::storage::to_string(diagnostic.severity));
            entry.source = "import";
            entry.summary = QString::fromStdString(diagnostic.message);
            entry.details = QString("Import diagnostic\nSeverity: %1\nCode: %2\nMessage: %3")
                .arg(entry.severity,
                     QString::fromStdString(diagnostic.code),
                     QString::fromStdString(diagnostic.message));
            entry.blocking = diagnostic.severity == aegis::storage::DiagnosticSeverity::Error;
            if (diagnostic.artifact_id.has_value()) {
                entry.artifact_id = QString::fromStdString(*diagnostic.artifact_id);
                if (const auto* artifact = package->find_artifact_by_id(*diagnostic.artifact_id)) {
                    entry.artifact_path = QString::fromStdString(artifact->path.generic_string());
                    entry.details += QString("\nArtifact: %1").arg(entry.artifact_path);
                }
            }
            m_impl->diagnostics_entries.push_back(std::move(entry));
        }
    }

    for (const auto& violation : m_impl->latest_violations) {
        MainWindow::Impl::DiagnosticEntry entry;
        entry.severity = QString::fromStdString(aegis::rules::severity_to_string(violation.severity));
        entry.source = "run";
        entry.summary = QString::fromStdString(violation.message);
        entry.violation_id = QString::fromStdString(violation.id);
        entry.details = QString("Run diagnostic\nViolation: %1\nRule: %2\nSeverity: %3\nMessage: %4")
            .arg(entry.violation_id,
                 QString::fromStdString(violation.rule_id),
                 entry.severity,
                 QString::fromStdString(violation.message));
        entry.blocking = violation.severity == aegis::rules::Severity::Error || violation.severity == aegis::rules::Severity::Fatal;
        m_impl->diagnostics_entries.push_back(std::move(entry));
    }

    if (m_impl->diagnostics_table == nullptr || m_impl->diagnostics_details == nullptr || m_impl->diagnostics_severity_filter == nullptr) {
        return;
    }

    const QString severity_filter = m_impl->diagnostics_severity_filter->currentText().trimmed().toLower();
    std::vector<int> visible_indexes;
    for (int i = 0; i < static_cast<int>(m_impl->diagnostics_entries.size()); ++i) {
        const auto& entry = m_impl->diagnostics_entries[static_cast<std::size_t>(i)];
        if (severity_filter != "all" && !severity_filter.isEmpty() && entry.severity.compare(severity_filter, Qt::CaseInsensitive) != 0) {
            continue;
        }
        visible_indexes.push_back(i);
    }

    const QSignalBlocker blocker(m_impl->diagnostics_table);
    m_impl->diagnostics_table->clearContents();
    m_impl->diagnostics_table->setRowCount(static_cast<int>(visible_indexes.size()));
    for (int row = 0; row < static_cast<int>(visible_indexes.size()); ++row) {
        const auto& entry = m_impl->diagnostics_entries[static_cast<std::size_t>(visible_indexes[static_cast<std::size_t>(row)])];
        auto* severity_item = new QTableWidgetItem(entry.severity);
        severity_item->setData(Qt::UserRole, visible_indexes[static_cast<std::size_t>(row)]);
        if (entry.blocking) {
            severity_item->setBackground(QBrush(QColor(255, 225, 225)));
        }
        m_impl->diagnostics_table->setItem(row, 0, severity_item);
        m_impl->diagnostics_table->setItem(row, 1, new QTableWidgetItem(entry.source));
        m_impl->diagnostics_table->setItem(row, 2, new QTableWidgetItem(entry.summary));
    }

    if (visible_indexes.empty()) {
        m_impl->diagnostics_details->setPlainText("No diagnostics match the current filters. Clear filters or adjust the severity selection.");
        if (m_impl->diagnostics_related_button != nullptr) {
            m_impl->diagnostics_related_button->setEnabled(false);
        }
        return;
    }

    int row = m_impl->diagnostics_table->currentRow();
    if (row < 0 || row >= m_impl->diagnostics_table->rowCount()) {
        row = 0;
        m_impl->diagnostics_table->selectRow(row);
    }
    const auto* severity_item = m_impl->diagnostics_table->item(row, 0);
    if (severity_item == nullptr) {
        m_impl->diagnostics_details->setPlainText("Select a diagnostic to inspect its details.");
        if (m_impl->diagnostics_related_button != nullptr) {
            m_impl->diagnostics_related_button->setEnabled(false);
        }
        return;
    }
    const int entry_index = severity_item->data(Qt::UserRole).toInt();
    const auto& entry = m_impl->diagnostics_entries[static_cast<std::size_t>(entry_index)];
    m_impl->diagnostics_details->setPlainText(entry.details);

    if (entry.source == "import" && !entry.artifact_path.trimmed().isEmpty() && m_impl->import_artifact_table != nullptr) {
        for (int artifact_row = 0; artifact_row < m_impl->import_artifact_table->rowCount(); ++artifact_row) {
            if (auto* item = m_impl->import_artifact_table->item(artifact_row, 0); item != nullptr
                && item->text() == entry.artifact_path) {
                m_impl->import_artifact_table->selectRow(artifact_row);
                break;
            }
        }
    } else if (entry.source == "run" && !entry.violation_id.trimmed().isEmpty() && m_impl->violation_explorer != nullptr) {
        for (int violation_row = 0; violation_row < m_impl->violation_explorer->violation_count(); ++violation_row) {
            m_impl->violation_explorer->select_row(violation_row);
            if (m_impl->violation_explorer->current_violation() != nullptr
                && QString::fromStdString(m_impl->violation_explorer->current_violation()->id) == entry.violation_id) {
                break;
            }
        }
    }

    if (m_impl->diagnostics_related_button != nullptr) {
        m_impl->diagnostics_related_button->setEnabled(true);
    }
}

bool MainWindow::navigate_current_violation_relationships()
{
    if (m_impl->violation_explorer == nullptr || m_impl->violation_explorer->current_violation() == nullptr) {
        publish_ui_notification("Navigation unavailable: no current violation selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const auto& violation = *m_impl->violation_explorer->current_violation();
    bool opened_any = false;

    const QStringList graph_targets{
        violation.location.pin_name.has_value() ? QString::fromStdString(*violation.location.pin_name) : QString{},
        violation.location.net_name.has_value() ? QString::fromStdString(*violation.location.net_name) : QString{},
        violation.location.device_name.has_value() ? QString::fromStdString(*violation.location.device_name) : QString{},
        metadata_value(violation, {"pin_name", "graph_pin"}),
        metadata_value(violation, {"net_name", "graph_net"}),
        metadata_value(violation, {"device", "device_name", "graph_node"})
    };
    for (const auto& target : graph_targets) {
        if (!target.trimmed().isEmpty() && search_graph_node(target)) {
            opened_any = true;
            break;
        }
    }

    const QString artifact_id = metadata_value(violation, {"artifact_id", "source_artifact_id"});
    const QString artifact_path = metadata_value(violation, {"artifact_path", "source_artifact_path"});
    if ((!artifact_id.trimmed().isEmpty() || !artifact_path.trimmed().isEmpty()) && m_impl->import_artifact_table != nullptr) {
        for (int row = 0; row < m_impl->import_artifact_table->rowCount(); ++row) {
            auto* item = m_impl->import_artifact_table->item(row, 0);
            if (item == nullptr) {
                continue;
            }
            const QString row_artifact_id = item->data(Qt::UserRole).toString();
            const QString row_artifact_path = item->text();
            if ((!artifact_id.trimmed().isEmpty() && row_artifact_id.compare(artifact_id, Qt::CaseInsensitive) == 0)
                || (!artifact_path.trimmed().isEmpty()
                    && QFileInfo(row_artifact_path).filePath().compare(QFileInfo(artifact_path).filePath(), Qt::CaseInsensitive) == 0)) {
                m_impl->import_artifact_table->selectRow(row);
                opened_any = true;
                break;
            }
        }
    }

    if (!opened_any) {
        publish_ui_notification("No related graph or import metadata link could be resolved for the current violation", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    publish_ui_notification("Opened related graph/import context for current violation", ActivityLogSeverity::Info, 4000);
    return true;
}

bool MainWindow::select_related_violations_for_current_diagnostic()
{
    if (m_impl->diagnostics_table == nullptr || m_impl->violation_explorer == nullptr) {
        return false;
    }
    const int row = m_impl->diagnostics_table->currentRow();
    if (row < 0) {
        publish_ui_notification("Related violation navigation unavailable: no diagnostic selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto* severity_item = m_impl->diagnostics_table->item(row, 0);
    if (severity_item == nullptr) {
        publish_ui_notification("Related violation navigation unavailable: diagnostic details are incomplete", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto& entry = m_impl->diagnostics_entries.at(static_cast<std::size_t>(severity_item->data(Qt::UserRole).toInt()));

    QStringList related_ids;
    if (!entry.violation_id.trimmed().isEmpty()) {
        related_ids.append(entry.violation_id);
    } else {
        for (const auto& violation : m_impl->latest_violations) {
            if (violation_matches_artifact(violation, entry.artifact_id, entry.artifact_path)) {
                related_ids.append(QString::fromStdString(violation.id));
            }
        }
    }

    const int selected = m_impl->violation_explorer->select_violation_ids(related_ids);
    if (selected <= 0) {
        publish_ui_notification("No related visible violations were found for the selected diagnostic", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    publish_ui_notification(QString("Opened %1 related violation(s) from selected diagnostic").arg(selected), ActivityLogSeverity::Info, 4000);
    return true;
}

bool MainWindow::select_related_violations_for_current_artifact()
{
    if (m_impl->import_artifact_table == nullptr || m_impl->violation_explorer == nullptr) {
        return false;
    }
    const int row = m_impl->import_artifact_table->currentRow();
    if (row < 0) {
        publish_ui_notification("Related violation navigation unavailable: no import artifact selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto* item = m_impl->import_artifact_table->item(row, 0);
    if (item == nullptr) {
        publish_ui_notification("Related violation navigation unavailable: import artifact details are incomplete", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const QString artifact_id = item->data(Qt::UserRole).toString();
    const QString artifact_path = item->text();
    QStringList related_ids;
    for (const auto& violation : m_impl->latest_violations) {
        if (violation_matches_artifact(violation, artifact_id, artifact_path)) {
            related_ids.append(QString::fromStdString(violation.id));
        }
    }

    const int selected = m_impl->violation_explorer->select_violation_ids(related_ids);
    if (selected <= 0) {
        publish_ui_notification("No related visible violations were found for the selected import artifact", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    publish_ui_notification(QString("Opened %1 related violation(s) from selected import artifact").arg(selected), ActivityLogSeverity::Info, 4000);
    return true;
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
    if (m_impl->trace_panel == nullptr) {
        return;
    }
    const bool has_graph = m_impl->current_graph != nullptr;
    const bool has_active_trace = m_impl->canvas != nullptr && m_impl->canvas->has_active_trace();
    m_impl->trace_panel->set_request_enabled(has_graph);
    m_impl->trace_panel->set_clear_enabled(has_active_trace);
    m_impl->trace_panel->set_focus_enabled(has_active_trace);

    const QString current_status = m_impl->trace_panel->status_text();
    if (!has_graph) {
        if (current_status.trimmed().isEmpty() || current_status == state_text::trace_idle()) {
            m_impl->trace_panel->set_status_text(state_text::trace_no_graph());
        }
    } else if (!has_active_trace
               && (current_status.trimmed().isEmpty() || current_status == state_text::trace_no_graph())) {
        m_impl->trace_panel->set_status_text(state_text::trace_idle());
    }
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
    refresh_onboarding_panel();
    update_action_states();
    if (m_impl->reopen_last_session_enabled && !m_impl->last_successful_project_path.trimmed().isEmpty()) {
        Q_UNUSED(reopen_project_from_path(m_impl->last_successful_project_path, false));
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
    refresh_recent_project_actions();
    settings.setValue(QString("%1/version").arg(kSettingsRecentProjectsGroup), kRecentProjectsStateVersion);
    settings.setValue(QString("%1/paths").arg(kSettingsRecentProjectsGroup), m_impl->recent_project_paths);
    settings.setValue(QString("%1/reopenLastSession").arg(kSettingsRecentProjectsGroup), m_impl->reopen_last_session_enabled);
    settings.setValue(QString("%1/lastSuccessfulPath").arg(kSettingsRecentProjectsGroup), m_impl->last_successful_project_path);
    settings.setValue(QString("%1/dismissed").arg(kSettingsOnboardingGroup), m_impl->onboarding_dismissed);
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

void MainWindow::set_scene(UiScene scene)
{
    m_impl->owned_graph.reset();
    m_impl->sample_mode_active = false;
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
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->set_violations(violations);
    }
    if (m_impl->report_preview != nullptr) {
        m_impl->report_preview->set_violations(violations);
    }
    m_impl->latest_violations = violations.violations();
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_violations(std::move(violations));
        if (m_impl->report_preview != nullptr) {
            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
        }
    }
    refresh_workspace_summary();
    refresh_diagnostics_panel();
    update_action_states();
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
    if (m_impl->canvas == nullptr) {
        return false;
    }

    const QString trimmed_name = stable_name.trimmed();
    if (m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_request_text(trimmed_name);
    }

    if (trimmed_name.isEmpty()) {
        publish_trace_feedback("Enter a net, port, or device.pin to trace", ActivityLogSeverity::Warning, 4000);
        update_action_states();
        return false;
    }

    if (m_impl->current_graph == nullptr) {
        publish_trace_feedback("Trace unavailable: no connectivity graph available", ActivityLogSeverity::Warning, 4000);
        update_action_states();
        return false;
    }

    const bool replaced_previous_trace = m_impl->canvas->has_active_trace();
    const auto result = m_impl->trace_adapter.trace_by_name(trimmed_name.toStdString(), m_impl->canvas->scene());
    m_impl->canvas->set_trace_result(result);

    QString message = QString::fromStdString(result.message);
    if (result.resolved && replaced_previous_trace) {
        message.append(" (replaced previous trace)");
    }
    publish_trace_feedback(message, result.resolved ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                           result.resolved ? 3000 : 4000);
    update_action_states();
    return result.resolved;
}

bool MainWindow::request_trace_from_selection()
{
    if (m_impl->selection_model == nullptr || m_impl->canvas == nullptr) {
        return false;
    }
    const auto& ids = m_impl->selection_model->selected_ids();
    if (ids.empty()) {
        publish_trace_feedback("No selection available for trace", ActivityLogSeverity::Warning, 4000);
        update_action_states();
        return false;
    }
    const auto* item = m_impl->canvas->scene().find_item_by_id(ids.front());
    if (item == nullptr) {
        publish_trace_feedback("Selected item is no longer available for trace", ActivityLogSeverity::Warning, 4000);
        update_action_states();
        return false;
    }
    const auto net_it = item->source_metadata.find("net_name");
    if (net_it != item->source_metadata.end()) {
        return request_trace_by_name(QString::fromStdString(net_it->second));
    }
    const auto name_it = item->source_metadata.find("name");
    if (name_it != item->source_metadata.end()) {
        return request_trace_by_name(QString::fromStdString(name_it->second));
    }
    publish_trace_feedback("Selected item has no traceable stable name", ActivityLogSeverity::Warning, 4000);
    update_action_states();
    return false;
}

bool MainWindow::request_trace_from_current_violation()
{
    if (m_impl->violation_explorer == nullptr) {
        return false;
    }
    const auto* violation = m_impl->violation_explorer->current_violation();
    if (violation == nullptr) {
        publish_trace_feedback("No current violation selected for trace", ActivityLogSeverity::Warning, 4000);
        update_action_states();
        return false;
    }
    if (violation->location.net_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.net_name));
    }
    if (violation->location.pin_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.pin_name));
    }
    publish_trace_feedback("Violation does not contain a traceable graph reference", ActivityLogSeverity::Warning, 4000);
    update_action_states();
    return false;
}

void MainWindow::clear_trace()
{
    if (m_impl->canvas == nullptr) {
        return;
    }
    if (!m_impl->canvas->has_active_trace()) {
        publish_trace_feedback("No active trace to clear", ActivityLogSeverity::Info, 3000);
        update_action_states();
        return;
    }
    m_impl->canvas->clear_trace();
    publish_trace_feedback("Trace cleared", ActivityLogSeverity::Info, 3000);
    update_action_states();
}

void MainWindow::focus_trace()
{
    if (m_impl->canvas == nullptr) {
        return;
    }
    if (!m_impl->canvas->has_active_trace()) {
        publish_trace_feedback("No active trace to focus", ActivityLogSeverity::Info, 3000);
        update_action_states();
        return;
    }
    m_impl->canvas->focus_trace();
    publish_trace_feedback("Focused active trace", ActivityLogSeverity::Info, 3000);
    update_action_states();
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
