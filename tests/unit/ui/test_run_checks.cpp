#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

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

aegis::parsing::LayoutIR make_scene_only_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "scene_only";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    return ir;
}

} // namespace

TEST_CASE("RunChecks executes real electrical rules for bundled sample graphs", "[ui][P7][P7-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.trigger_workspace_action("run_checks"));

    REQUIRE(window.last_status_message().contains("Run Checks completed"));
    REQUIRE(window.violation_explorer_count() > 0);
    REQUIRE(window.visible_violation_overlay_count() > 0);

    const auto entries = window.activity_log_entries();
    bool saw_start = false;
    bool saw_complete = false;
    for (const auto& entry : entries) {
        if (entry.contains("Run Checks started")) {
            saw_start = true;
        }
        if (entry.contains("Run Checks completed")) {
            saw_complete = true;
        }
    }
    REQUIRE(saw_start);
    REQUIRE(saw_complete);
}

TEST_CASE("RunChecks reports no-graph failure for scene-only workflows", "[ui][P7][P7-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    window.set_scene(aegis::ui::build_ui_scene(make_scene_only_ir()));
    REQUIRE_FALSE(window.workspace_action_enabled("run_checks"));
    REQUIRE_FALSE(window.trigger_workspace_action("run_checks"));
    REQUIRE(window.violation_explorer_count() == 0);
}
