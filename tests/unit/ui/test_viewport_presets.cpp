#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/rules/violation.hpp"

#include <QApplication>
#include <QSettings>

#include <cmath>
#include <memory>

using namespace aegis::parsing;
using namespace aegis::ui;
using namespace aegis::rules;

namespace {

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

struct SettingsCleanupGuard {
    SettingsCleanupGuard() {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }
    ~SettingsCleanupGuard() {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }
};

LayoutIR make_ir()
{
    LayoutIR ir;
    ir.design_name = "preset_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 200.0, 100.0}});
    return ir;
}

} // namespace

TEST_CASE("ViewportPreset save and apply through MainWindow", "[ui][P13-006][ViewportPreset]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    MainWindow window;

    REQUIRE(window.viewport_preset_names().isEmpty());
    REQUIRE_FALSE(window.save_viewport_preset(""));

    window.set_scene(build_ui_scene(make_ir()));
    REQUIRE(window.save_viewport_preset("Overview"));
    REQUIRE(window.viewport_preset_names().contains("Overview"));

    window.set_heatmap_visible(true);
    REQUIRE(window.apply_viewport_preset("Overview"));
    REQUIRE(window.canvas_view_center().x() > 0.0);
}

TEST_CASE("ViewportPreset rename and delete", "[ui][P13-006][ViewportPreset]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_ir()));

    REQUIRE(window.save_viewport_preset("A"));
    REQUIRE(window.viewport_preset_names().contains("A"));

    REQUIRE(window.rename_viewport_preset("A", "B"));
    REQUIRE_FALSE(window.viewport_preset_names().contains("A"));
    REQUIRE(window.viewport_preset_names().contains("B"));

    REQUIRE(window.delete_viewport_preset("B"));
    REQUIRE_FALSE(window.viewport_preset_names().contains("B"));
    REQUIRE_FALSE(window.delete_viewport_preset("Missing"));
}

TEST_CASE("ViewportPreset workspace actions are discoverable", "[ui][P13-006][ViewportPreset]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    MainWindow window;

    const auto ids = window.workspace_action_ids();
    REQUIRE(ids.contains("save_viewport_preset"));
    REQUIRE(ids.contains("manage_viewport_presets"));
    REQUIRE(ids.contains("zoom_to_selection"));
    REQUIRE(ids.contains("zoom_to_violations"));
    REQUIRE(ids.contains("zoom_to_trace"));
    REQUIRE(ids.contains("jump_to_coordinate"));
    REQUIRE(ids.contains("viewport_back"));
    REQUIRE(ids.contains("viewport_forward"));
}
