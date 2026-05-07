#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>

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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "workspace_actions_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    return ir;
}

aegis::rules::ViolationCollection make_violations()
{
    using namespace aegis::rules;
    Violation violation{"R1", Severity::Warning, "Width issue"};
    violation.id = "V1";
    violation.location.layer = "M1";
    std::vector<Violation> violations;
    violations.push_back(violation);
    return ViolationCollection{std::move(violations)};
}

QDockWidget* find_dock(aegis::ui::MainWindow& window, const QString& title)
{
    for (auto* dock : window.findChildren<QDockWidget*>()) {
        if (dock != nullptr && dock->windowTitle() == title) {
            return dock;
        }
    }
    return nullptr;
}

QMenu* find_view_menu(aegis::ui::MainWindow& window)
{
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu != nullptr && menu->title().contains("View", Qt::CaseInsensitive)) {
            return menu;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("WorkspaceActions shared actions are discoverable and stateful", "[ui][P3-013][WorkspaceActions]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;

    const auto ids = window.workspace_action_ids();
    REQUIRE(ids.contains("open_sample"));
    REQUIRE(ids.contains("fit_view"));
    REQUIRE(ids.contains("reset_view"));
    REQUIRE(ids.contains("toggle_grid"));
    REQUIRE(ids.contains("toggle_overlays"));
    REQUIRE(ids.contains("run_checks"));
    REQUIRE(ids.contains("clear_selection"));

    REQUIRE(window.workspace_action_enabled("open_sample"));
    REQUIRE(!window.workspace_action_enabled("fit_view"));
    REQUIRE(!window.workspace_action_enabled("reset_view"));
    REQUIRE(!window.workspace_action_enabled("toggle_grid"));
    REQUIRE(!window.workspace_action_enabled("toggle_overlays"));
    REQUIRE(!window.workspace_action_enabled("run_checks"));
    REQUIRE(!window.workspace_action_enabled("clear_selection"));

    REQUIRE(window.workspace_action_shortcut_text("fit_view") == "F");
    REQUIRE(window.workspace_action_shortcut_text("toggle_grid") == "G");
    REQUIRE(window.workspace_action_shortcut_text("run_checks").contains("F5"));
    REQUIRE(!window.workspace_action_tooltip("open_sample").isEmpty());
}

TEST_CASE("WorkspaceActions load sample scene and control grid overlays and selection", "[ui][P3-013][WorkspaceActions]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.trigger_workspace_action("open_sample"));
    REQUIRE(window.layer_panel_count() > 0);
    REQUIRE(window.workspace_action_enabled("fit_view"));
    REQUIRE(window.workspace_action_enabled("reset_view"));
    REQUIRE(window.workspace_action_enabled("toggle_grid"));
    REQUIRE(window.workspace_action_enabled("run_checks"));

    REQUIRE(window.grid_visible());
    REQUIRE(window.workspace_action_checked("toggle_grid"));
    REQUIRE(window.trigger_workspace_action("toggle_grid"));
    REQUIRE(!window.grid_visible());
    REQUIRE(!window.workspace_action_checked("toggle_grid"));

    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.select_scene_item_by_id("port:A");
    REQUIRE(window.selected_item_count() == 1);
    REQUIRE(window.workspace_action_enabled("clear_selection"));
    REQUIRE(window.trigger_workspace_action("clear_selection"));
    REQUIRE(window.selected_item_count() == 0);

    window.set_violations(make_violations());
    REQUIRE(window.workspace_action_enabled("toggle_overlays"));
    REQUIRE(window.violation_overlays_visible());
    REQUIRE(window.trigger_workspace_action("toggle_overlays"));
    REQUIRE(!window.violation_overlays_visible());
}

TEST_CASE("WorkspaceActions view menu uses real dock toggle actions", "[ui][P3-013][WorkspaceActions]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.show();
    QApplication::processEvents();

    auto* view_menu = find_view_menu(window);
    REQUIRE(view_menu != nullptr);

    auto* layers_dock = find_dock(window, "Layers");
    auto* graph_dock = find_dock(window, "Graph Explorer");
    REQUIRE(layers_dock != nullptr);
    REQUIRE(graph_dock != nullptr);

    QAction* layers_toggle = nullptr;
    QAction* graph_toggle = nullptr;
    for (auto* action : view_menu->actions()) {
        if (action == nullptr) {
            continue;
        }
        if (action->text().contains("Layers", Qt::CaseInsensitive)) {
            layers_toggle = action;
        }
        if (action->text().contains("Graph Explorer", Qt::CaseInsensitive)) {
            graph_toggle = action;
        }
    }

    REQUIRE(layers_toggle != nullptr);
    REQUIRE(graph_toggle != nullptr);

    REQUIRE(!layers_dock->isHidden());
    layers_toggle->trigger();
    QApplication::processEvents();
    REQUIRE(layers_dock->isHidden());
    layers_toggle->trigger();
    QApplication::processEvents();
    REQUIRE(!layers_dock->isHidden());

    REQUIRE(window.is_graph_explorer_visible());
    graph_toggle->trigger();
    QApplication::processEvents();
    REQUIRE(!window.is_graph_explorer_visible());
    graph_toggle->trigger();
    QApplication::processEvents();
    REQUIRE(window.is_graph_explorer_visible());
}
