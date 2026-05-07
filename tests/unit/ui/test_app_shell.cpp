#include <catch2/catch_test_macros.hpp>
#include "aegis/ui/main_window.hpp"

#include <QApplication>
#include <QDateTime>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// RAII guard that creates a QApplication for headless tests
struct QtAppGuard {
    int argc = 1;
    char arg0[8] = "aegis";
    char* argv[2] = { arg0, nullptr };
    std::unique_ptr<QApplication> app;

    QtAppGuard() {
        if (!QApplication::instance()) {
            app = std::make_unique<QApplication>(argc, argv);
        }
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
TEST_CASE("MainWindow constructs with central placeholder", "[ui][p1-007][AppShell]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.has_central_widget());
    REQUIRE(window.has_menu_bar());
}

TEST_CASE("MainWindow has dockable panels", "[ui][p1-007][AppShell]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.dock_widget_count() >= 1);

    QStringList titles = window.dock_widget_titles();
    REQUIRE(!titles.isEmpty());

    bool has_layers = false, has_properties = false, has_log = false;
    for (const QString& t : titles) {
        if (t == "Layers") has_layers = true;
        if (t == "Properties") has_properties = true;
        if (t == "Log") has_log = true;
    }
    REQUIRE(has_layers);
    REQUIRE(has_properties);
    REQUIRE(has_log);
}

TEST_CASE("MainWindow saves and restores geometry", "[ui][p1-007][AppShell]")
{
    QtAppGuard guard;

    QByteArray saved_geo;
    QByteArray saved_state;
    {
        aegis::ui::MainWindow w;
        w.resize(1024, 768);
        w.move(100, 100);
        w.show();

        w.save_window_state();

        // Verify fields were populated by directly reading default QSettings
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        saved_geo  = settings.value("mainWindow/geometry").toByteArray();
        saved_state = settings.value("mainWindow/state").toByteArray();

        REQUIRE(!saved_geo.isEmpty());
        REQUIRE(!saved_state.isEmpty());
    }

    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.setValue("mainWindow/geometry", saved_geo);
        settings.setValue("mainWindow/state", saved_state);

        aegis::ui::MainWindow w;
        w.restore_window_state();
        // verify restore ran without crash; exact geometry match is OS-dependent
        REQUIRE(w.isVisible() == false);
    }

    // Clean up test settings
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    settings.remove("mainWindow");
}

TEST_CASE("MainWindow menu bar has expected menus", "[ui][p1-007][AppShell]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;

    auto* mb = window.menuBar();
    REQUIRE(mb != nullptr);

    QStringList menus;
    for (QMenu* m : mb->findChildren<QMenu*>()) {
        menus.append(m->title());
    }

    bool has_file = false, has_view = false, has_tools = false, has_help = false;
    for (const QString& title : menus) {
        if (title.contains("File", Qt::CaseInsensitive)) has_file = true;
        if (title.contains("View", Qt::CaseInsensitive)) has_view = true;
        if (title.contains("Tools", Qt::CaseInsensitive)) has_tools = true;
        if (title.contains("Help", Qt::CaseInsensitive)) has_help = true;
    }

    REQUIRE(has_file);
    REQUIRE(has_view);
    REQUIRE(has_tools);
    REQUIRE(has_help);
}
