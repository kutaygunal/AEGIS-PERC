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

TEST_CASE("ActivityLog dock records workspace events instead of placeholder text", "[ui][P7][P7-002][ActivityLog]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.dock_widget_titles().contains("Log"));
    REQUIRE(window.activity_log_entry_count() >= 1);
    REQUIRE(window.activity_log_entry_text(0).contains("Workspace initialized"));

    REQUIRE(window.trigger_workspace_action("open_sample"));
    REQUIRE(window.last_status_message().contains("Loaded sample"));

    const auto entries = window.activity_log_entries();
    bool saw_sample_load = false;
    for (const auto& entry : entries) {
        if (entry.contains("Loaded sample: inverter")) {
            saw_sample_load = true;
            break;
        }
    }
    REQUIRE(saw_sample_load);

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive));

    const auto after_run_checks = window.activity_log_entries();
    bool saw_run_checks = false;
    for (const auto& entry : after_run_checks) {
        if (entry.contains("Run Checks completed")) {
            saw_run_checks = true;
            break;
        }
    }
    REQUIRE(saw_run_checks);
}

TEST_CASE("ActivityLog keeps bounded history under repeated UI events", "[ui][P7][P7-002][ActivityLog]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.trigger_workspace_action("open_sample"));

    const int max_entries = window.activity_log_max_entries();
    REQUIRE(max_entries > 0);

    for (int i = 0; i < max_entries + 25; ++i) {
        window.clear_trace();
    }

    REQUIRE(window.activity_log_entry_count() == max_entries);
    REQUIRE(window.activity_log_entry_text(window.activity_log_entry_count() - 1).contains("No active trace to clear"));
}
