#include "aegis/ui/main_window.hpp"
#include "aegis/ui/layer_panel.hpp"
#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/report_preview_panel.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/ui/trace_panel.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/parsing/layout_ir.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace aegis::ui {
namespace {

using ActionMap = std::map<QString, QAction*>;

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

    for (const auto& port : j.at("netlist").at("ports")) {
        ir.ports.push_back(aegis::parsing::Port{
            port.at("name").get<std::string>(),
            port.at("direction").get<std::string>(),
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
    ConnectivityTraceAdapter trace_adapter;
    QDockWidget* graph_dock = nullptr;
    QList<QDockWidget*> docks;
    QMenuBar* menu_bar = nullptr;
    QMenu* view_menu = nullptr;
    QToolBar* workspace_toolbar = nullptr;
    QLabel* performance_status_label = nullptr;
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
    statusBar()->showMessage("Ready");

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

    auto* open_sample = register_action("open_sample", "Open &Sample", QKeySequence("Ctrl+Shift+O"), false,
                                        "Load the bundled inverter sample design");
    connect(open_sample, &QAction::triggered, this, [this]() {
        try {
            const auto ir = load_sample_design_ir("inverter.json");
            if (!ir.has_value()) {
                statusBar()->showMessage("Sample design not found");
                return;
            }
            set_scene(build_ui_scene(*ir));
            set_violations({});
            statusBar()->showMessage(QString("Loaded sample: %1").arg(QString::fromStdString(ir->design_name)));
        } catch (const std::exception& error) {
            statusBar()->showMessage(QString("Failed to load sample: %1").arg(error.what()));
        }
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
                                       "UI hook for future rule-check execution");
    connect(run_checks, &QAction::triggered, this, [this]() {
        statusBar()->showMessage("Run Checks is a UI placeholder in Sprint 3");
    });

    auto* clear_selection = register_action("clear_selection", "C&lear Selection", QKeySequence(Qt::Key_Escape), false,
                                            "Clear the current canvas selection");
    connect(clear_selection, &QAction::triggered, this, [this]() {
        if (m_impl->selection_model != nullptr) {
            m_impl->selection_model->clear();
        }
    });
}

void MainWindow::setup_menus()
{
    m_impl->menu_bar = menuBar();

    // File
    QMenu* fileMenu = m_impl->menu_bar->addMenu("&File");
    fileMenu->addAction(m_impl->actions.at("open_sample"));
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
    toolsMenu->addAction(m_impl->actions.at("clear_selection"));

    // Help
    QMenu* helpMenu = m_impl->menu_bar->addMenu("&Help");
    helpMenu->addAction("&About AEGIS-PERC");
    helpMenu->addAction("&Documentation");
}

void MainWindow::setup_toolbar()
{
    m_impl->workspace_toolbar = addToolBar("Workspace");
    m_impl->workspace_toolbar->setObjectName("WorkspaceToolbar");
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("open_sample"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("fit_view"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("reset_view"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("toggle_grid"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("toggle_overlays"));
    m_impl->workspace_toolbar->addSeparator();
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("run_checks"));
    m_impl->workspace_toolbar->addAction(m_impl->actions.at("clear_selection"));
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
                this, [this](const QStringList&) { update_action_states(); });
    }
    auto* properties_dock = make_dock("Properties", Qt::RightDockWidgetArea, m_impl->properties_panel);

    m_impl->violation_explorer = new ViolationExplorerPanel(this);
    connect(m_impl->violation_explorer, &ViolationExplorerPanel::current_violation_changed,
            this, [this]() {
                if (m_impl->violation_explorer == nullptr || m_impl->canvas == nullptr) {
                    return;
                }
                const auto* violation = m_impl->violation_explorer->current_violation();
                if (m_impl->report_preview != nullptr) {
                    m_impl->report_preview->set_current_violation(violation);
                    m_impl->report_preview->set_snapshot(m_impl->canvas->grab());
                }
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
                statusBar()->showMessage(
                    m_impl->violation_explorer != nullptr
                        ? m_impl->violation_explorer->filter_summary_text()
                        : QString("Violations updated"));
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
            [this](const QString& stable_name) { Q_UNUSED(request_trace_by_name(stable_name)); });
    connect(m_impl->trace_panel, &TracePanel::clear_trace_requested, this, &MainWindow::clear_trace);
    connect(m_impl->trace_panel, &TracePanel::focus_trace_requested, this, &MainWindow::focus_trace);
    auto* trace_dock = make_dock("Trace", Qt::RightDockWidgetArea, m_impl->trace_panel);

    auto* log = new QLabel("Log\n\nContent placeholder.", this);
    log->setAlignment(Qt::AlignCenter);
    auto* log_dock = make_dock("Log", Qt::BottomDockWidgetArea, log);

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
    const bool has_violations = m_impl->canvas != nullptr && m_impl->canvas->violation_count() > 0;
    const bool has_selection = m_impl->selection_model != nullptr && !m_impl->selection_model->empty();

    m_impl->actions.at("fit_view")->setEnabled(has_scene);
    m_impl->actions.at("reset_view")->setEnabled(has_scene);
    m_impl->actions.at("toggle_grid")->setEnabled(has_scene);
    m_impl->actions.at("run_checks")->setEnabled(has_scene);
    m_impl->actions.at("clear_selection")->setEnabled(has_selection);
    m_impl->actions.at("toggle_overlays")->setEnabled(has_violations);

    if (m_impl->canvas != nullptr) {
        m_impl->actions.at("toggle_grid")->setChecked(m_impl->canvas->grid_visible());
        m_impl->actions.at("toggle_overlays")->setChecked(m_impl->canvas->violation_overlays_visible());
    }
}

void MainWindow::restore_window_state()
{
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    if (settings.contains("mainWindow/geometry")) {
        restoreGeometry(settings.value("mainWindow/geometry").toByteArray());
    }
    if (settings.contains("mainWindow/state")) {
        restoreState(settings.value("mainWindow/state").toByteArray());
    }
}

void MainWindow::save_window_state()
{
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    save_window_state();
    QMainWindow::closeEvent(event);
}

void MainWindow::set_scene(UiScene scene)
{
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
    m_impl->trace_adapter.set_graph(graph);
    if (m_impl->graph_explorer != nullptr) {
        m_impl->graph_explorer->set_graph(graph);
    }
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

    const auto result = m_impl->trace_adapter.trace_by_name(stable_name.trimmed().toStdString(), m_impl->canvas->scene());
    m_impl->canvas->set_trace_result(result);
    if (m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_request_text(stable_name);
        m_impl->trace_panel->set_status_text(QString::fromStdString(result.message));
    }
    statusBar()->showMessage(QString::fromStdString(result.message));
    return result.resolved;
}

bool MainWindow::request_trace_from_selection()
{
    if (m_impl->selection_model == nullptr || m_impl->canvas == nullptr) {
        return false;
    }
    const auto& ids = m_impl->selection_model->selected_ids();
    if (ids.empty()) {
        if (m_impl->trace_panel != nullptr) {
            m_impl->trace_panel->set_status_text("No selection available for trace");
        }
        return false;
    }
    const auto* item = m_impl->canvas->scene().find_item_by_id(ids.front());
    if (item == nullptr) {
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
    if (m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_status_text("Selected item has no traceable stable name");
    }
    return false;
}

bool MainWindow::request_trace_from_current_violation()
{
    if (m_impl->violation_explorer == nullptr) {
        return false;
    }
    const auto* violation = m_impl->violation_explorer->current_violation();
    if (violation == nullptr) {
        return false;
    }
    if (violation->location.net_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.net_name));
    }
    if (violation->location.pin_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.pin_name));
    }
    if (m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_status_text("Violation does not contain a traceable graph reference");
    }
    return false;
}

void MainWindow::clear_trace()
{
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->clear_trace();
    }
    if (m_impl->trace_panel != nullptr) {
        m_impl->trace_panel->set_status_text("Trace cleared");
    }
}

void MainWindow::focus_trace()
{
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->focus_trace();
    }
}

void MainWindow::set_violation_filter_state(ViolationFilterState state)
{
    if (m_impl->violation_explorer != nullptr) {
        m_impl->violation_explorer->set_filter_state(std::move(state));
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

void MainWindow::set_heatmap_visible(bool visible)
{
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_heatmap_visible(visible);
    }
}

void MainWindow::set_heatmap_opacity(double opacity)
{
    if (m_impl->canvas != nullptr) {
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

} // namespace aegis::ui
