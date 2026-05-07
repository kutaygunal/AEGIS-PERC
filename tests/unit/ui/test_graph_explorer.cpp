#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>

using namespace aegis::graph;
using namespace aegis::parsing;
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
    ir.design_name = "graph_explorer_test";
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

} // namespace

TEST_CASE("Graph explorer displays graph nodes and supports search", "[ui][P3-010][GraphExplorer]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    GraphExplorerPanel panel;
    panel.set_graph(&graph);

    REQUIRE(panel.visible_item_count() > 0);
    REQUIRE(panel.status_text().contains("graph nodes visible"));
    REQUIRE(panel.search_and_select("n1"));
    REQUIRE(panel.current_node_name() == "n1");
}

TEST_CASE("Graph explorer level of detail limits visible nodes", "[ui][P3-010][GraphExplorer]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    GraphExplorerPanel panel;
    panel.set_graph(&graph);
    panel.set_lod_limit(2);

    REQUIRE(panel.lod_limit() == 2);
    REQUIRE(panel.visible_item_count() == 2);
}

TEST_CASE("MainWindow syncs graph explorer selection with trace and canvas selection", "[ui][P3-010][GraphExplorer]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    MainWindow window;
    window.set_scene(build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    REQUIRE(window.dock_widget_titles().contains("Graph Explorer"));
    REQUIRE(window.is_graph_explorer_visible());
    REQUIRE(window.graph_explorer_count() > 0);

    REQUIRE(window.search_graph_node("A"));
    REQUIRE(window.current_graph_node_name() == "A");
    REQUIRE(window.selected_item_count() == 1);
    REQUIRE(window.has_active_trace());

    window.set_graph_lod_limit(2);
    REQUIRE(window.graph_explorer_count() == 2);
}
