#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QSettings>

#include <memory>

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

LayoutIR make_ir()
{
    LayoutIR ir;
    ir.design_name = "graph_explorer_ui";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.nets.push_back(Net{"A", {"IN", "M1.gate"}, {}});
    ir.nets.push_back(Net{"Y", {"OUT", "M1.drain"}, {}});
    ir.nets.push_back(Net{"VSS", {"M1.source"}, {}});
    ir.devices.push_back(Device{"M1", "NMOS", {{"gate", "A"}, {"drain", "Y"}, {"source", "VSS"}}, {}});
    ir.ports.push_back(Port{"IN", "INPUT", "A", std::string{"M1"}, aegis::parsing::Point{10.0, 10.0}});
    ir.ports.push_back(Port{"OUT", "OUTPUT", "Y", std::string{"M2"}, aegis::parsing::Point{115.0, 55.0}});
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

TEST_CASE("GraphExplorerUi distinguishes no-graph and empty-graph states", "[ui][P7][P7-008][GraphExplorerUi]")
{
    QtAppGuard guard;

    GraphExplorerPanel panel;
    REQUIRE(panel.status_text() == "Load a design with connectivity data to explore graph nodes.");
    REQUIRE_FALSE(panel.search_and_select("A"));
    REQUIRE(panel.status_text() == "Load a design with connectivity data to explore graph nodes.");

    ConnectivityGraph empty_graph;
    panel.set_graph(&empty_graph);
    REQUIRE(panel.visible_item_count() == 0);
    REQUIRE(panel.status_text() == "Connectivity graph is empty.");
    REQUIRE_FALSE(panel.search_and_select("A"));
    REQUIRE(panel.status_text() == "Connectivity graph is empty.");
}

TEST_CASE("GraphExplorerUi distinguishes exact substring and LOD-limited search results", "[ui][P7][P7-008][GraphExplorerUi]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    GraphExplorerPanel panel;
    panel.set_graph(&graph);

    REQUIRE(panel.search_and_select("A"));
    REQUIRE(panel.current_node_name() == "A");
    REQUIRE(panel.status_text().contains("selected: A"));

    REQUIRE(panel.search_and_select("drain"));
    REQUIRE(panel.current_node_name() == "M1.drain");
    REQUIRE(panel.status_text().contains("selected: M1.drain"));

    panel.set_lod_limit(1);
    REQUIRE(panel.visible_item_count() == 1);
    REQUIRE(panel.status_text().contains("LOD limit 1"));
    REQUIRE_FALSE(panel.search_and_select("M1.drain"));
    REQUIRE(panel.status_text().contains("within the current LOD range"));
}

TEST_CASE("GraphExplorerUi preserves current-node selection when graphs reload and clears it when truncated away", "[ui][P7][P7-008][GraphExplorerUi]")
{
    QtAppGuard guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    GraphExplorerPanel panel;
    panel.set_graph(&graph);
    REQUIRE(panel.search_and_select("OUT"));
    REQUIRE(panel.current_node_name() == "OUT");

    panel.set_graph(&graph);
    REQUIRE(panel.current_node_name() == "OUT");
    REQUIRE(panel.status_text().contains("selected: OUT"));

    panel.set_lod_limit(1);
    REQUIRE(panel.visible_item_count() == 1);
    REQUIRE(panel.current_node_name().isEmpty());
    REQUIRE_FALSE(panel.status_text().contains("selected: OUT"));
}

TEST_CASE("GraphExplorerUi main window search keeps trace and selection sync stable", "[ui][P7][P7-008][GraphExplorerUi]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    MainWindow window;
    window.set_scene(build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    REQUIRE(window.search_graph_node("A"));
    REQUIRE(window.current_graph_node_name() == "A");
    REQUIRE(window.selected_item_count() == 1);
    REQUIRE(window.has_active_trace());
    REQUIRE(window.trace_status_text().contains("Trace resolved for A"));

    window.set_graph_lod_limit(1);
    REQUIRE(window.graph_explorer_status_text().contains("LOD limit 1"));

    REQUIRE_FALSE(window.search_graph_node("M1.drain"));
    REQUIRE(window.graph_explorer_status_text().contains("within the current LOD range"));
}
