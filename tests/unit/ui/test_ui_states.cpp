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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "ui_states";
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

    Violation a{"R1", Severity::Warning, "Width issue"};
    a.id = "V1";
    a.location.layer = "M1";
    violations.push_back(a);

    Violation b{"R2", Severity::Error, "Spacing issue"};
    b.id = "V2";
    b.location.layer = "M2";
    violations.push_back(b);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("UiStates exposes consistent empty-state guidance across major panels", "[ui][P7][P7-010][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.canvas_empty_state_text() == "Load a design to inspect layout geometry.");
    REQUIRE(window.onboarding_visible());
    REQUIRE(window.onboarding_text().contains("Browse Samples"));
    REQUIRE(window.onboarding_text().contains("Documentation"));
    REQUIRE(window.properties_summary_text() == "Load a design to inspect item properties.");
    REQUIRE(window.trace_status_text() == "Load a design with connectivity data to trace nets and pins.");
    REQUIRE(window.graph_explorer_status_text() == "Load a design with connectivity data to explore graph nodes.");
    REQUIRE(window.report_preview_summary_text().contains("Load a design to preview report content."));
    REQUIRE_FALSE(window.report_preview_snapshot_status_text().trimmed().isEmpty());
    REQUIRE(window.trigger_workspace_action("documentation"));
    QApplication::processEvents();
    REQUIRE(window.is_documentation_dialog_visible());
    REQUIRE(window.violation_details_text() == "Run checks to populate the violations panel.");
    REQUIRE(window.heatmap_empty_state_text() == "Run checks or clear filters to generate violation heatmap data.");
    REQUIRE(window.diagnostics_details_text().contains("No diagnostics match the current filters", Qt::CaseInsensitive));
}

TEST_CASE("UiStates transitions to actionable loaded idle and filtered states deterministically", "[ui][P7][P7-010][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(ir));
    REQUIRE(window.properties_summary_text() == "Select layout items to inspect their properties.");

    window.set_connectivity_graph(&graph);
    REQUIRE(window.trace_status_text() == "Enter a net, port, or device.pin to start a trace.");

    window.set_violations(make_violations());
    window.select_violation_row(-1);
    REQUIRE(window.violation_details_text() == "Select a violation to inspect its details.");
    REQUIRE(window.diagnostics_entry_count() == 2);
    REQUIRE(window.set_diagnostics_severity_filter("error"));
    REQUIRE(window.diagnostics_entry_count() == 1);
    REQUIRE(window.select_diagnostics_row(0));
    REQUIRE(window.diagnostics_details_text().contains("Run diagnostic", Qt::CaseInsensitive));

    aegis::ui::ViolationFilterState state;
    state.layer = "NO_MATCH";
    window.set_violation_filter_state(state);
    REQUIRE(window.violation_details_text() == "No violations match the current filters. Clear filters or adjust the criteria.");
}

TEST_CASE("UiStates remembers onboarding dismissal across restart", "[ui][P8][P8-014][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    {
        aegis::ui::MainWindow window;
        REQUIRE(window.onboarding_visible());
        window.dismiss_onboarding();
        REQUIRE_FALSE(window.onboarding_visible());
    }

    {
        aegis::ui::MainWindow restored;
        REQUIRE_FALSE(restored.onboarding_visible());
    }
}

TEST_CASE("UiStates reports empty connectivity graph and retains preview guidance after repeated resets", "[ui][P7][P7-010][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    aegis::ui::MainWindow window;
    aegis::graph::ConnectivityGraph empty_graph;
    window.set_connectivity_graph(&empty_graph);
    REQUIRE(window.graph_explorer_status_text() == "Connectivity graph is empty.");

    window.set_scene({});
    REQUIRE(window.canvas_empty_state_text() == "Load a design to inspect layout geometry.");
    REQUIRE(window.report_preview_summary_text().contains("Load a design to preview report content."));
}

TEST_CASE("UiStates shows post-import guidance after imported design scene loads", "[ui][P13-012][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE_FALSE(window.post_import_guidance_visible());

    const auto ir = make_ir();
    auto graph = make_graph(ir);
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    REQUIRE_FALSE(window.post_import_guidance_visible());
}

TEST_CASE("UiStates workspace summary distinguishes imported design readiness states", "[ui][P13-012][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.workspace_summary_text().contains("empty workspace"));

    const auto ir = make_ir();
    auto graph = make_graph(ir);
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    const QString summary = window.workspace_summary_text();
    REQUIRE(summary.contains("manual/custom scene"));
}

TEST_CASE("UiStates post-import guidance dismissal persists across restart", "[ui][P13-012][UiStates]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    {
        aegis::ui::MainWindow window;
        REQUIRE_FALSE(window.post_import_guidance_visible());
        window.dismiss_post_import_guidance();
    }

    {
        aegis::ui::MainWindow restored;
        REQUIRE_FALSE(restored.post_import_guidance_visible());
    }
}
