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
    ir.design_name = "trace_workflow";
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

aegis::rules::ViolationCollection make_traceable_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;

    Violation traceable{"R_TRACE", Severity::Error, "Traceable violation"};
    traceable.id = "V-TRACE";
    traceable.location.net_name = "n2";
    violations.push_back(traceable);

    Violation untraceable{"R_OTHER", Severity::Warning, "No graph reference"};
    untraceable.id = "V-NO-TRACE";
    untraceable.location.layer = "M1";
    violations.push_back(untraceable);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("TraceWorkflow validates empty and missing-context requests with visible feedback", "[ui][P7][P7-007][TraceWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(ir));

    REQUIRE_FALSE(window.trace_request_enabled());
    REQUIRE_FALSE(window.trace_clear_enabled());
    REQUIRE_FALSE(window.trace_focus_enabled());

    REQUIRE_FALSE(window.request_trace_by_name("   "));
    REQUIRE(window.trace_status_text() == "Enter a net, port, or device.pin to trace");
    REQUIRE(window.last_status_message() == window.trace_status_text());

    REQUIRE_FALSE(window.request_trace_by_name("n1"));
    REQUIRE(window.trace_status_text() == "Trace unavailable: no connectivity graph available");
    REQUIRE_FALSE(window.has_active_trace());

    window.set_connectivity_graph(&graph);
    REQUIRE(window.trace_request_enabled());
    REQUIRE_FALSE(window.request_trace_from_selection());
    REQUIRE(window.trace_status_text() == "No selection available for trace");

    REQUIRE_FALSE(window.request_trace_by_name("missing_net"));
    REQUIRE(window.trace_status_text().contains("Trace target not found"));
    REQUIRE_FALSE(window.has_active_trace());

    REQUIRE(window.activity_log_entries().back().contains("Trace target not found"));
}

TEST_CASE("TraceWorkflow keeps trace controls synchronized across selection graph and violation traces", "[ui][P7][P7-007][TraceWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);
    window.set_violations(make_traceable_violations());

    REQUIRE(window.trace_request_enabled());
    REQUIRE_FALSE(window.trace_clear_enabled());
    REQUIRE_FALSE(window.trace_focus_enabled());

    window.select_scene_item_by_id("port:0");
    REQUIRE(window.request_trace_from_selection());
    REQUIRE(window.has_active_trace());
    REQUIRE(window.trace_request_text() == "n1");
    REQUIRE(window.trace_status_text().contains("Trace resolved for n1"));
    REQUIRE(window.trace_clear_enabled());
    REQUIRE(window.trace_focus_enabled());

    REQUIRE(window.search_graph_node("n2"));
    REQUIRE(window.has_active_trace());
    REQUIRE(window.trace_request_text() == "n2");
    REQUIRE(window.trace_status_text().contains("replaced previous trace"));

    window.select_violation_row(0);
    REQUIRE(window.request_trace_from_current_violation());
    REQUIRE(window.trace_request_text() == "n2");
    REQUIRE(window.trace_status_text().contains("Trace resolved for n2"));

    window.focus_trace();
    REQUIRE(window.trace_status_text() == "Focused active trace");
    REQUIRE(window.last_status_message() == "Focused active trace");

    window.clear_trace();
    REQUIRE_FALSE(window.has_active_trace());
    REQUIRE(window.trace_status_text() == "Trace cleared");
    REQUIRE_FALSE(window.trace_clear_enabled());
    REQUIRE_FALSE(window.trace_focus_enabled());
}

TEST_CASE("TraceWorkflow reports untraceable violation and repeated clear or focus without stale state", "[ui][P7][P7-007][TraceWorkflow]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);
    window.set_violations(make_traceable_violations());

    window.select_violation_row(1);
    REQUIRE_FALSE(window.request_trace_from_current_violation());
    REQUIRE(window.trace_status_text() == "Violation does not contain a traceable graph reference");
    REQUIRE_FALSE(window.has_active_trace());

    window.focus_trace();
    REQUIRE(window.trace_status_text() == "No active trace to focus");
    REQUIRE_FALSE(window.has_active_trace());

    window.clear_trace();
    REQUIRE(window.trace_status_text() == "No active trace to clear");
    REQUIRE_FALSE(window.has_active_trace());
}
