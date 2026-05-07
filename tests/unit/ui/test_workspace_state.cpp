#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "workspace_state";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.nets.push_back(Net{"n1", {"A", "M1.gate"}, {}});
    ir.nets.push_back(Net{"n2", {"Y", "M1.drain"}, {}});
    ir.nets.push_back(Net{"GND", {"M1.source"}, {}});
    ir.devices.push_back(Device{"M1", "NMOS", {{"gate", "n1"}, {"drain", "n2"}, {"source", "GND"}}, {}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    ir.ports.push_back(Port{"Y", "OUTPUT", "n2", std::string{"M2"}, Point{115.0, 55.0}});
    return ir;
}

aegis::graph::ConnectivityGraph make_graph(const aegis::parsing::LayoutIR& ir)
{
    std::vector<std::string> unresolved;
    auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
    REQUIRE(unresolved.empty());
    return graph;
}

aegis::rules::ViolationCollection make_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;
    Violation v{"R_TRACE", Severity::Error, "Traceable violation"};
    v.id = "V-TRACE";
    v.location.net_name = "n2";
    violations.push_back(v);
    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("WorkspaceState shared actions reflect empty loaded selection violation and trace states", "[ui][P7][P7-005][WorkspaceState]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;

    REQUIRE(!window.workspace_action_enabled("fit_view"));
    REQUIRE(!window.workspace_action_enabled("run_checks"));
    REQUIRE(!window.workspace_action_enabled("trace_from_selection"));
    REQUIRE(!window.workspace_action_enabled("trace_from_violation"));
    REQUIRE(!window.workspace_action_enabled("focus_trace"));
    REQUIRE(!window.workspace_action_enabled("clear_trace_action"));
    REQUIRE(!window.workspace_action_enabled("clear_selection"));
    REQUIRE(!window.workspace_action_enabled("toggle_overlays"));

    window.set_scene(aegis::ui::build_ui_scene(ir));
    REQUIRE(window.workspace_action_enabled("fit_view"));
    REQUIRE(window.workspace_action_enabled("reset_view"));
    REQUIRE(window.workspace_action_enabled("toggle_grid"));
    REQUIRE(!window.workspace_action_enabled("run_checks"));

    window.set_connectivity_graph(&graph);
    REQUIRE(window.workspace_action_enabled("run_checks"));
    REQUIRE(!window.workspace_action_enabled("trace_from_selection"));
    REQUIRE(!window.workspace_action_enabled("trace_from_violation"));

    window.select_scene_item_by_id("port:A");
    REQUIRE(window.workspace_action_enabled("clear_selection"));
    REQUIRE(window.workspace_action_enabled("trace_from_selection"));

    window.set_violations(make_violations());
    REQUIRE(window.workspace_action_enabled("toggle_overlays"));
    window.select_violation_row(0);
    REQUIRE(window.workspace_action_enabled("trace_from_violation"));

    REQUIRE(window.request_trace_by_name("n1"));
    REQUIRE(window.has_active_trace());
    REQUIRE(window.workspace_action_enabled("focus_trace"));
    REQUIRE(window.workspace_action_enabled("clear_trace_action"));

    REQUIRE(window.trigger_workspace_action("clear_trace_action"));
    REQUIRE(!window.has_active_trace());
    REQUIRE(!window.workspace_action_enabled("focus_trace"));
    REQUIRE(!window.workspace_action_enabled("clear_trace_action"));
}

TEST_CASE("WorkspaceState sample workflow keeps graph-dependent actions synchronized", "[ui][P7][P7-005][WorkspaceState]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.trigger_workspace_action("open_sample"));
    REQUIRE(window.workspace_action_enabled("run_checks"));
    REQUIRE(!window.workspace_action_enabled("trace_from_selection"));

    window.select_scene_item_by_id("port:in");
    REQUIRE(window.workspace_action_enabled("trace_from_selection"));
    REQUIRE(window.request_trace_by_name("in"));
    REQUIRE(window.workspace_action_enabled("focus_trace"));
    REQUIRE(window.workspace_action_enabled("clear_trace_action"));
}
