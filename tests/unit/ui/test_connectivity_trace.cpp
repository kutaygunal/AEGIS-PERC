#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>

#include <cmath>

using namespace aegis::graph;
using namespace aegis::parsing;
using namespace aegis::rules;
using namespace aegis::ui;

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

LayoutIR make_ir()
{
    LayoutIR ir;
    ir.design_name = "trace_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.nets.push_back(Net{"n1", {"A", "M1.gate"}, {}});
    ir.nets.push_back(Net{"n2", {"Y", "M1.drain"}, {}});
    ir.nets.push_back(Net{"GND", {"M1.source"}, {}});
    ir.devices.push_back(Device{"M1", "NMOS", {{"gate", "n1"}, {"drain", "n2"}, {"source", "GND"}}, {}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, aegis::parsing::Point{10.0, 10.0}});
    ir.ports.push_back(Port{"Y", "OUTPUT", "n2", std::string{"M2"}, aegis::parsing::Point{115.0, 55.0}});
    return ir;
}

ConnectivityGraph make_graph(const LayoutIR& ir)
{
    std::vector<std::string> unresolved;
    auto graph = ConnectivityGraph::from_layout_ir(ir, unresolved);
    REQUIRE(unresolved.empty());
    return graph;
}

ViolationCollection make_violations()
{
    ViolationCollection violations;
    Violation v{"R_TRACE", Severity::Error, "Trace this net"};
    v.id = "V-TRACE";
    v.location.net_name = "n2";
    violations.add(v);
    return violations;
}

} // namespace

TEST_CASE("ConnectivityTraceAdapter resolves nets and stable device pin names", "[ui][P3-009][ConnectivityTrace]")
{
    const auto ir = make_ir();
    auto graph = make_graph(ir);
    ConnectivityTraceAdapter adapter;
    adapter.set_graph(&graph);

    const auto scene = build_ui_scene(ir);
    const auto net_trace = adapter.trace_by_name("n1", scene);
    REQUIRE(net_trace.resolved);
    REQUIRE(net_trace.traced_net_names.contains("n1"));
    REQUIRE(!net_trace.related_scene_item_ids.empty());

    const auto pin_trace = adapter.trace_by_name("M1.gate", scene);
    REQUIRE(pin_trace.resolved);
    REQUIRE(pin_trace.traced_net_names.contains("n1"));
}

TEST_CASE("MainWindow traces from selection and violation and supports focus clear", "[ui][P3-009][ConnectivityTrace]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    MainWindow window;
    window.set_scene(build_ui_scene(ir));
    window.set_connectivity_graph(&graph);
    window.set_violations(make_violations());

    window.select_scene_item_by_id("port:0");
    REQUIRE(window.request_trace_from_selection());
    REQUIRE(window.has_active_trace());
    REQUIRE(window.traced_item_count() >= 1);

    window.focus_trace();
    REQUIRE(std::abs(window.canvas_view_center().x() - 10.0) < 0.001);
    REQUIRE(std::abs(window.canvas_view_center().y() - 10.0) < 0.001);

    window.select_violation_row(0);
    REQUIRE(window.request_trace_from_current_violation());
    REQUIRE(window.trace_status_text().contains("Trace resolved"));

    window.clear_trace();
    REQUIRE_FALSE(window.has_active_trace());
    REQUIRE(window.trace_status_text() == "Trace cleared");
}

TEST_CASE("Missing graph references produce nonfatal feedback", "[ui][P3-009][ConnectivityTrace]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    MainWindow window;
    window.set_scene(build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    REQUIRE_FALSE(window.request_trace_by_name("missing_net"));
    REQUIRE_FALSE(window.has_active_trace());
    REQUIRE(window.trace_status_text().contains("not found"));
}
