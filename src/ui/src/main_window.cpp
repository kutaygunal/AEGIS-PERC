#include "aegis/ui/main_window.hpp"
#include "aegis/ui/layer_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/selection_model.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QWidget>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct MainWindow::Impl {
    LayoutCanvas* canvas = nullptr;
    LayerPanel* layer_panel = nullptr;
    PropertiesPanel* properties_panel = nullptr;
    SelectionModel* selection_model = nullptr;
    QList<QDockWidget*> docks;
    QMenuBar* menu_bar = nullptr;
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
    setCentralWidget(m_impl->canvas);

    // Status bar
    statusBar()->showMessage("Ready");

    // Menus
    setup_menus();

    // Dock panels
    setup_dock_panels();
}

void MainWindow::setup_menus()
{
    m_impl->menu_bar = menuBar();

    // File
    QMenu* fileMenu = m_impl->menu_bar->addMenu("&File");
    {
        auto* a = fileMenu->addAction("&New Project");
        a->setShortcuts(QKeySequence::New);
        connect(a, &QAction::triggered, this, &MainWindow::close);
    }
    {
        auto* a = fileMenu->addAction("&Open...");
        a->setShortcuts(QKeySequence::Open);
        connect(a, &QAction::triggered, this, []() {});
    }
    fileMenu->addSeparator();
    {
        auto* a = fileMenu->addAction("E&xit");
        a->setShortcuts(QKeySequence::Quit);
        connect(a, &QAction::triggered, qApp, &QApplication::quit);
    }

    // View
    QMenu* viewMenu = m_impl->menu_bar->addMenu("&View");
    // Placeholder entries for future toggles
    viewMenu->addAction("Show &Layers Panel");
    viewMenu->addAction("Show &Properties Panel");
    viewMenu->addAction("Show &Log Panel");
    viewMenu->addSeparator();
    viewMenu->addAction("&Reset Layout");

    // Tools
    QMenu* toolsMenu = m_impl->menu_bar->addMenu("&Tools");
    toolsMenu->addAction("&Run Checks");
    toolsMenu->addAction("&Preferences");

    // Help
    QMenu* helpMenu = m_impl->menu_bar->addMenu("&Help");
    helpMenu->addAction("&About AEGIS-PERC");
    helpMenu->addAction("&Documentation");
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
    make_dock("Layers", Qt::LeftDockWidgetArea, m_impl->layer_panel);

    m_impl->properties_panel = new PropertiesPanel(this);
    if (m_impl->selection_model != nullptr) {
        connect(m_impl->selection_model, &SelectionModel::selection_changed,
                m_impl->properties_panel, &PropertiesPanel::set_selected_ids);
    }
    make_dock("Properties", Qt::RightDockWidgetArea, m_impl->properties_panel);

    auto* log = new QLabel("Log\n\nContent placeholder.", this);
    log->setAlignment(Qt::AlignCenter);
    make_dock("Log", Qt::BottomDockWidgetArea, log);
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------
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
    if (m_impl->selection_model != nullptr) {
        m_impl->selection_model->clear();
    }
    if (m_impl->canvas != nullptr) {
        m_impl->canvas->set_scene(std::move(scene));
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

QString MainWindow::properties_summary_text() const
{
    return m_impl->properties_panel != nullptr ? m_impl->properties_panel->summary_text() : QString{};
}

} // namespace aegis::ui
