#include "aegis/ui/main_window.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace aegis::ui {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct MainWindow::Impl {
    QWidget* central_placeholder = nullptr;
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

    // Central placeholder for future canvas
    m_impl->central_placeholder = new QWidget(this);
    auto* layout = new QVBoxLayout(m_impl->central_placeholder);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel("Canvas Placeholder\n\nFuture 2D layout rendering will appear here.", this);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    setCentralWidget(m_impl->central_placeholder);

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
    auto make_dock = [this](const QString& title, Qt::DockWidgetArea area) -> QDockWidget* {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(title + "Dock");
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        auto* placeholder = new QLabel(title + "\n\nContent placeholder.", this);
        placeholder->setAlignment(Qt::AlignCenter);
        dock->setWidget(placeholder);
        addDockWidget(area, dock);
        m_impl->docks.append(dock);
        return dock;
    };

    make_dock("Layers", Qt::LeftDockWidgetArea);
    make_dock("Properties", Qt::RightDockWidgetArea);
    make_dock("Log", Qt::BottomDockWidgetArea);
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

// ---------------------------------------------------------------------------
// Test accessors
// ---------------------------------------------------------------------------
bool MainWindow::has_central_widget() const
{
    return centralWidget() != nullptr;
}

bool MainWindow::has_menu_bar() const
{
    return m_impl->menu_bar != nullptr && !m_impl->menu_bar->isHidden();
}

int MainWindow::dock_widget_count() const
{
    return static_cast<int>(m_impl->docks.size());
}

QStringList MainWindow::dock_widget_titles() const
{
    QStringList titles;
    for (const auto* dock : m_impl->docks) {
        if (dock) titles.append(dock->windowTitle());
    }
    return titles;
}

} // namespace aegis::ui
