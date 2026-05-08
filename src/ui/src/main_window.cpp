#include "aegis/ui/main_window.hpp"
#include "aegis/ui/activity_log_panel.hpp"
#include "aegis/ui/layer_panel.hpp"
#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/report_preview_panel.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/ui/trace_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/electrical_rules.hpp"
#include "aegis/rules/rule_engine.hpp"
#include "aegis/storage/import_validation.hpp"
#include "aegis/storage/project_package.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
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

#include <nlohmann/json.hpp>

namespace aegis::ui {
namespace {

using ActionMap = std::map<QString, QAction*>;

constexpr int kWorkspaceUiStateVersion = 1;
constexpr auto kSettingsMainWindowGroup = "mainWindow";
constexpr auto kSettingsWorkspaceUiGroup = "mainWindow/workspaceUi";

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

void configure_action(QAction* action, const QString& tooltip)
{
    action->setToolTip(tooltip);
    action->setStatusTip(tooltip);
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

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct MainWindow::Impl {
    LayoutCanvas* canvas = nullptr;
    LayerPanel* layer_panel = nullptr;
    PropertiesPanel* properties_panel = nullptr;
    SelectionModel* selection_model = nullptr;
    ViolationExplorerPanel* violation_explorer = nullptr;
    ReportPreviewPanel* report_preview = nullptr;
    GraphExplorerPanel* graph_explorer = nullptr;
    TracePanel* trace_panel = nullptr;
    ActivityLogPanel* activity_log = nullptr;
    std::unique_ptr<aegis::graph::ConnectivityGraph> owned_graph;
    const aegis::graph::ConnectivityGraph* current_graph = nullptr;
    ConnectivityTraceAdapter trace_adapter;
    QDockWidget* graph_dock = nullptr;
    QList<QDockWidget*> docks;
    QMenuBar* menu_bar = nullptr;
    QMenu* view_menu = nullptr;
    QToolBar* workspace_toolbar = nullptr;
    QLabel* performance_status_label = nullptr;
    QDialog* about_dialog = nullptr;
    QDialog* documentation_dialog = nullptr;
    QDialog* sample_browser_dialog = nullptr;
    QDialog* import_review_dialog = nullptr;
    QPlainTextEdit* documentation_text = nullptr;
    QPlainTextEdit* import_review_text = nullptr;
    QListWidget* sample_browser_list = nullptr;
    aegis::storage::ImportPreflightValidator import_validator;
    aegis::storage::ProjectPackage pending_import_package;
    QString last_status_message;
    ActionMap actions;
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
    m_impl->canvas = new LayoutCanvas(this);
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
    m_impl->performance_status_label = new QLabel(this);
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
        configure_action(action, tooltip);
        m_impl->actions.emplace(id, action);
        return action;
    };

    auto* import_project = register_action("import_project", "&Import Design Package...", QKeySequence("Ctrl+I"), false,
                                           "Review customer design-package files or dropped project folders before analysis");
    connect(import_project, &QAction::triggered, this, [this]() {
        const QString message = "Import Design Package expects test-supplied paths or drag-and-drop folder input";
        open_import_review_dialog({}, false);
        publish_ui_notification(message, ActivityLogSeverity::Info, 4000);
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

    auto* run_checks = register_action("run_checks", "&Run Checks", QKeySequence(Qt::Key_F5), false,
                                       "Run available electrical checks for the active design graph");
    connect(run_checks, &QAction::triggered, this, [this]() {
        execute_run_checks();
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

    // Tools
    QMenu* toolsMenu = m_impl->menu_bar->addMenu("&Tools");
    toolsMenu->addAction(m_impl->actions.at("run_checks"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("trace_from_selection"));
    toolsMenu->addAction(m_impl->actions.at("trace_from_violation"));
    toolsMenu->addAction(m_impl->actions.at("focus_trace"));
    toolsMenu->addAction(m_impl->actions.at("clear_trace_action"));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_impl->actions.at("clear_selection"));

    // Help
    QMenu* helpMenu = m_impl->menu_bar->addMenu("&Help");
    helpMenu->addAction(m_impl->actions.at("about"));
    helpMenu->addAction(m_impl->actions.at("documentation"));
}

void MainWindow::setup_toolbar()
{
    m_impl->workspace_toolbar = addToolBar("Workspace");
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
    if (m_impl->current_graph == nullptr) {
        const QString message = "Run Checks unavailable: no connectivity graph available";
        publish_ui_notification(message, ActivityLogSeverity::Error, 4000);
        return;
    }

    publish_ui_notification("Run Checks started", ActivityLogSeverity::Info, 2000);

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

        const QString message = QString("Run Checks completed: %1 violation(s)").arg(collection.size());
        publish_ui_notification(message,
                                collection.empty() ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                5000);
    } catch (const std::exception& error) {
        const QString message = QString("Run Checks failed: %1").arg(error.what());
        publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
    }
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
        auto* text = new QPlainTextEdit(dialog);
        text->setReadOnly(true);
        layout->addWidget(text, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        auto* validate_button = new QPushButton("Validate Files", dialog);
        buttons->addButton(validate_button, QDialogButtonBox::ActionRole);
        connect(validate_button, &QPushButton::clicked, this, [this]() {
            refresh_import_review();
            const bool blocked = import_has_blockers();
            publish_ui_notification(blocked ? "Import validation found blocking issues" : "Import validation completed",
                                    blocked ? ActivityLogSeverity::Warning : ActivityLogSeverity::Info,
                                    4000);
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        m_impl->import_review_dialog = dialog;
        m_impl->import_review_text = text;
    }

    if (!paths.isEmpty()) {
        m_impl->pending_import_package = {};
        if (paths.size() == 1 && QFileInfo(paths.front()).isDir()) {
            m_impl->pending_import_package = m_impl->import_validator.scan_project_folder(paths.front().toStdString(), QFileInfo(paths.front()).fileName().toStdString());
        } else {
            aegis::storage::ProjectPackage package;
            package.set_manifest_version(1);
            package.project().name = "ImportedProject";
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

    if (m_impl->import_review_text != nullptr) {
        m_impl->import_review_text->setPlainText(import_summary_text(m_impl->pending_import_package));
    }
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
                    if (m_impl->report_preview != nullptr) {
                        m_impl->report_preview->set_selected_item_count(ids.size());
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
    auto* violations_dock = make_dock("Violations", Qt::BottomDockWidgetArea, m_impl->violation_explorer);

    m_impl->report_preview = new ReportPreviewPanel(this);
    connect(m_impl->report_preview, &ReportPreviewPanel::refresh_requested, this, [this]() {
        if (m_impl->report_preview != nullptr && m_impl->canvas != nullptr) {
            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
        }
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

    m_impl->trace_panel = new TracePanel(this);
    connect(m_impl->trace_panel, &TracePanel::trace_requested, this,
            [this](const QString& stable_name) {
                Q_UNUSED(request_trace_by_name(stable_name));
            });
    connect(m_impl->trace_panel, &TracePanel::clear_trace_requested, this, &MainWindow::clear_trace);
    connect(m_impl->trace_panel, &TracePanel::focus_trace_requested, this, &MainWindow::focus_trace);
    auto* trace_dock = make_dock("Trace", Qt::RightDockWidgetArea, m_impl->trace_panel);

    m_impl->activity_log = new ActivityLogPanel(this);
    append_activity_log("Workspace initialized", ActivityLogSeverity::Info);
    auto* log_dock = make_dock("Log", Qt::BottomDockWidgetArea, m_impl->activity_log);

    if (m_impl->view_menu != nullptr) {
        m_impl->view_menu->addSeparator();
        m_impl->view_menu->addAction(layers_dock->toggleViewAction());
        m_impl->view_menu->addAction(properties_dock->toggleViewAction());
        m_impl->view_menu->addAction(violations_dock->toggleViewAction());
        m_impl->view_menu->addAction(report_dock->toggleViewAction());
        if (m_impl->graph_dock != nullptr) {
            m_impl->view_menu->addAction(m_impl->graph_dock->toggleViewAction());
        }
        m_impl->view_menu->addAction(trace_dock->toggleViewAction());
        m_impl->view_menu->addAction(log_dock->toggleViewAction());
    }
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------
void MainWindow::update_action_states()
{
    const bool has_scene = m_impl->canvas != nullptr && m_impl->canvas->has_scene();
    const bool has_graph = m_impl->current_graph != nullptr;
    const bool has_violations = m_impl->canvas != nullptr && m_impl->canvas->violation_count() > 0;
    const bool has_selection = m_impl->selection_model != nullptr && !m_impl->selection_model->empty();
    const bool has_active_trace = m_impl->canvas != nullptr && m_impl->canvas->has_active_trace();
    const bool has_current_violation = m_impl->violation_explorer != nullptr && m_impl->violation_explorer->current_violation() != nullptr;

    m_impl->actions.at("fit_view")->setEnabled(has_scene);
    m_impl->actions.at("reset_view")->setEnabled(has_scene);
    m_impl->actions.at("toggle_grid")->setEnabled(has_scene);
    m_impl->actions.at("run_checks")->setEnabled(has_graph);
    m_impl->actions.at("trace_from_selection")->setEnabled(has_graph && has_selection);
    m_impl->actions.at("trace_from_violation")->setEnabled(has_graph && has_current_violation);
    m_impl->actions.at("focus_trace")->setEnabled(has_active_trace);
    m_impl->actions.at("clear_trace_action")->setEnabled(has_active_trace);
    m_impl->actions.at("clear_selection")->setEnabled(has_selection);
    m_impl->actions.at("toggle_overlays")->setEnabled(has_violations);

    if (m_impl->canvas != nullptr) {
        m_impl->actions.at("toggle_grid")->setChecked(m_impl->canvas->grid_visible());
        m_impl->actions.at("toggle_overlays")->setChecked(m_impl->canvas->violation_overlays_visible());
    }

    update_trace_controls();
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

    update_action_states();
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
        m_impl->owned_graph = std::make_unique<aegis::graph::ConnectivityGraph>(std::move(graph));
        set_connectivity_graph(m_impl->owned_graph.get());
        set_violations({});
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
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_violations(std::move(violations));
        if (m_impl->report_preview != nullptr) {
            m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
        }
    }
    update_action_states();
}

void MainWindow::set_connectivity_graph(const aegis::graph::ConnectivityGraph* graph)
{
    m_impl->current_graph = graph;
    m_impl->trace_adapter.set_graph(graph);
    if (m_impl->graph_explorer != nullptr) {
        m_impl->graph_explorer->set_graph(graph);
    }
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

int MainWindow::graph_explorer_count() const
{
    return m_impl->graph_explorer != nullptr ? m_impl->graph_explorer->visible_item_count() : 0;
}

void MainWindow::select_violation_row(int row)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->select_row(row);
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

bool MainWindow::is_import_dialog_visible() const
{
    return m_impl->import_review_dialog != nullptr && m_impl->import_review_dialog->isVisible();
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
