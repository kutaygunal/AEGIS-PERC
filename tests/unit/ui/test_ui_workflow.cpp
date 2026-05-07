#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QApplication>
#include <QSettings>

#include <memory>
#include <vector>

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

aegis::parsing::LayoutIR make_workflow_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "ui_workflow_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 100.0, 50.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{120.0, 30.0, 60.0, 30.0}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    ir.annotations.push_back(Annotation{"note", "hello", std::string{"M2"}, Point{150.0, 45.0}});
    return ir;
}

aegis::rules::ViolationCollection make_workflow_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;

    Violation a{"R_WIDTH", Severity::Warning, "Width issue"};
    a.id = "V1";
    a.location.layer = "M1";
    a.location.net_name = "n1";
    violations.push_back(a);

    Violation b{"R_SPACE", Severity::Error, "Spacing issue"};
    b.id = "V2";
    b.location.layer = "M2";
    b.location.point = aegis::graph::Point{140.0, 40.0};
    violations.push_back(b);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("UiWorkflow main window smoke path loads bundled sample data", "[ui][P3-015][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.has_layout_canvas());
    REQUIRE(window.dock_widget_titles().contains("Layers"));
    REQUIRE(window.dock_widget_titles().contains("Violations"));
    REQUIRE(window.dock_widget_titles().contains("Report Preview"));

    REQUIRE(window.trigger_workspace_action("open_sample"));
    REQUIRE(window.layer_panel_count() > 0);
    REQUIRE(window.workspace_action_enabled("fit_view"));
    REQUIRE(window.report_preview_summary_text().contains("Design: inverter"));
}

TEST_CASE("UiWorkflow verifies layer toggles selection updates and violation filters", "[ui][P3-015][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_workflow_ir()));
    window.set_violations(make_workflow_violations());

    REQUIRE(window.layer_panel_count() == 2);
    REQUIRE(window.is_layer_visible("M1"));
    window.set_layer_visible("M1", false);
    REQUIRE_FALSE(window.is_layer_visible("M1"));
    window.set_layer_visible("M1", true);
    REQUIRE(window.is_layer_visible("M1"));

    window.select_scene_item_by_id("port:A");
    REQUIRE(window.selected_item_count() == 1);
    REQUIRE(window.properties_summary_text().contains("port:A"));
    REQUIRE(window.trigger_workspace_action("clear_selection"));
    REQUIRE(window.selected_item_count() == 0);

    aegis::ui::ViolationFilterState filter_state;
    filter_state.layer = "M2";
    window.set_violation_filter_state(filter_state);
    REQUIRE(window.visible_violation_overlay_count() == 1);
    REQUIRE(window.violation_filter_summary_text().contains("1 / 2 violations"));
    REQUIRE(window.report_preview_summary_text().contains("Violations: 1"));
}

TEST_CASE("UiWorkflow existing app shell state persistence path remains valid", "[ui][P3-015][UiWorkflow][UiAppShell]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;

    QByteArray saved_geo;
    QByteArray saved_state;
    {
        aegis::ui::MainWindow window;
        window.resize(900, 600);
        window.save_window_state();

        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        saved_geo = settings.value("mainWindow/geometry").toByteArray();
        saved_state = settings.value("mainWindow/state").toByteArray();
    }

    REQUIRE(!saved_geo.isEmpty());
    REQUIRE(!saved_state.isEmpty());

    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.setValue("mainWindow/geometry", saved_geo);
        settings.setValue("mainWindow/state", saved_state);

        aegis::ui::MainWindow restored;
        restored.restore_window_state();
        REQUIRE_FALSE(restored.isVisible());
        REQUIRE(restored.has_layout_canvas());
    }
}
