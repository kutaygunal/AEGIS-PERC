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

TEST_CASE("SampleWorkflow exposes bundled sample choices and browser action", "[ui][P7][P7-003][SampleWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const auto ids = window.workspace_action_ids();
    REQUIRE(ids.contains("open_sample"));
    REQUIRE(ids.contains("open_sample_nand2"));
    REQUIRE(ids.contains("open_sample_ring_oscillator"));
    REQUIRE(ids.contains("browse_samples"));

    const auto sample_ids = window.bundled_sample_ids();
    REQUIRE(sample_ids.contains("inverter"));
    REQUIRE(sample_ids.contains("nand2"));
    REQUIRE(sample_ids.contains("ring_oscillator"));
    REQUIRE(window.onboarding_visible());
    REQUIRE(window.onboarding_text().contains("Run Checks"));
    REQUIRE(window.workspace_action_enabled("documentation"));

    REQUIRE(window.trigger_workspace_action("browse_samples"));
    QApplication::processEvents();
    REQUIRE(window.is_sample_browser_visible());
    REQUIRE(window.last_status_message().contains("sample browser", Qt::CaseInsensitive));
}

TEST_CASE("SampleWorkflow loads inverter nand2 and ring oscillator samples", "[ui][P7][P7-003][SampleWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE_FALSE(window.onboarding_visible());
    REQUIRE(window.report_preview_summary_text().contains("Design: inverter"));
    REQUIRE(window.last_status_message().contains("Loaded sample: inverter"));

    REQUIRE(window.load_bundled_sample("nand2"));
    REQUIRE(window.report_preview_summary_text().contains("Design: nand2"));
    REQUIRE(window.last_status_message().contains("Loaded sample: nand2"));

    REQUIRE(window.load_bundled_sample("ring_oscillator"));
    REQUIRE(window.report_preview_summary_text().contains("Design: ring_oscillator"));
    REQUIRE(window.last_status_message().contains("Loaded sample: ring_oscillator"));
}

TEST_CASE("SampleWorkflow rejects unknown sample ids with UI feedback", "[ui][P7][P7-003][SampleWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE_FALSE(window.load_bundled_sample("missing_sample"));
    REQUIRE(window.last_status_message().contains("Unknown bundled sample"));

    const auto entries = window.activity_log_entries();
    bool saw_error = false;
    for (const auto& entry : entries) {
        if (entry.contains("Unknown bundled sample")) {
            saw_error = true;
            break;
        }
    }
    REQUIRE(saw_error);
}
