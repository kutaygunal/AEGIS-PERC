#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/main_window.hpp"

#include <QApplication>
#include <QSettings>

#include <memory>

namespace {

struct QtAppGuard {
    int argc = 1;
    char arg0[8] = "aegis";
    char* argv[2] = { arg0, nullptr };
    std::unique_ptr<QApplication> app;

    QtAppGuard()
    {
        if (!QApplication::instance()) {
            app = std::make_unique<QApplication>(argc, argv);
        }
    }
};

struct SettingsCleanupGuard {
    SettingsCleanupGuard()
    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }

    ~SettingsCleanupGuard()
    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }
};

} // namespace

TEST_CASE("HelpActions are discoverable through shared action registry", "[ui][P7][P7-001][HelpActions]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const auto ids = window.workspace_action_ids();
    REQUIRE(ids.contains("about"));
    REQUIRE(ids.contains("documentation"));
    REQUIRE(window.workspace_action_enabled("about"));
    REQUIRE(window.workspace_action_enabled("documentation"));
    REQUIRE(!window.workspace_action_tooltip("about").isEmpty());
    REQUIRE(!window.workspace_action_tooltip("documentation").isEmpty());
}

TEST_CASE("HelpActions open a real About dialog without crashing headless tests", "[ui][P7][P7-001][HelpActions]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.show();

    REQUIRE(window.trigger_workspace_action("about"));
    QApplication::processEvents();

    REQUIRE(window.is_about_dialog_visible());
    REQUIRE(window.last_status_message().contains("Opened About dialog"));
}

TEST_CASE("HelpActions documentation entry point degrades gracefully", "[ui][P7][P7-001][HelpActions]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.show();

    REQUIRE(window.trigger_workspace_action("documentation"));
    QApplication::processEvents();

    REQUIRE(window.is_documentation_dialog_visible());
    REQUIRE(!window.documentation_summary_text().isEmpty());
    const QString summary = window.documentation_summary_text();
    const bool has_readme_line = summary.contains("README:");
    const bool has_docs_line = summary.contains("Docs directory:");
    REQUIRE((has_readme_line || has_docs_line));
    REQUIRE(window.last_status_message().contains("documentation", Qt::CaseInsensitive));
}
