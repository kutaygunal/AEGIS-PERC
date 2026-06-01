#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>
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

aegis::parsing::LayoutIR make_cross_probe_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "cross_probe_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 80.0, 30.0}});
    ir.nets.push_back(Net{"n1", {"A", "M1.gate"}, {}});
    ir.nets.push_back(Net{"n2", {"Y", "M1.drain"}, {}});
    ir.nets.push_back(Net{"GND", {"M1.source"}, {}});
    ir.devices.push_back(Device{"M1", "NMOS", {{"gate", "n1"}, {"drain", "n2"}, {"source", "GND"}}, {}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    ir.ports.push_back(Port{"Y", "OUTPUT", "n2", std::string{"M2"}, Point{55.0, 10.0}});
    return ir;
}

namespace fs = std::filesystem;

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_ui_workflow_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

void remove_tree(const fs::path& root)
{
    std::error_code ec;
    fs::remove_all(root, ec);
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 3000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        QApplication::processEvents();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QApplication::processEvents();
    return predicate();
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
    REQUIRE(window.dock_widget_titles().contains("Jobs"));

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

TEST_CASE("UiWorkflow job monitor tracks recent workflow runs with progress history and report reopen actions", "[ui][P8][P8-007][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_job_pipeline_artificial_delay_for_tests(50);

    QString opened_report_path;
    window.set_report_opener_for_tests([&opened_report_path](const QString& path) {
        opened_report_path = path;
        return true;
    });

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M3\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() { return window.job_history_count() >= 1; }));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }, 5000));

    REQUIRE(window.select_job_history_row(window.job_history_count() - 1));
    REQUIRE(window.job_history_summary_text(window.job_history_count() - 1).contains("Completed", Qt::CaseInsensitive));
    REQUIRE(window.job_history_details_text().contains("Progress history", Qt::CaseInsensitive));
    REQUIRE(window.job_history_details_text().contains("report_export", Qt::CaseInsensitive));
    REQUIRE(window.job_history_open_json_enabled());
    REQUIRE(window.job_history_open_html_enabled());
    REQUIRE(window.trigger_job_history_open_json());
    REQUIRE(opened_report_path.endsWith("report.json"));

    remove_tree(root);
}

TEST_CASE("UiWorkflow job monitor keeps bounded history for recent workflow runs", "[ui][P8][P8-007][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    QString opened_report_path;
    window.set_report_opener_for_tests([&opened_report_path](const QString& path) {
        opened_report_path = path;
        return true;
    });

    const int runs = window.job_history_max_entries() + 2;
    for (int i = 0; i < runs; ++i) {
        const fs::path root = make_temp_dir();
        write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M3\nEND LIBRARY\n");
        write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
        write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
        write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
        write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");

        REQUIRE(window.load_bundled_sample("inverter"));
        REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
        QApplication::processEvents();
        REQUIRE(window.trigger_import_load_action());
        QApplication::processEvents();
        REQUIRE(window.trigger_workspace_action("run_checks"));
        REQUIRE(wait_until([&window]() {
            return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
        }, 5000));
        remove_tree(root);
    }

    REQUIRE(window.job_history_count() == window.job_history_max_entries());
    REQUIRE(window.select_job_history_row(window.job_history_count() - 1));
    REQUIRE(window.job_history_open_html_enabled());
    REQUIRE(window.trigger_job_history_open_html());
    REQUIRE(opened_report_path.endsWith("report.html"));
}

TEST_CASE("UiWorkflow workspace summary distinguishes sample and imported project readiness", "[ui][P8][P8-009][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.dock_widget_titles().contains("Workspace Summary"));
    REQUIRE(window.workspace_summary_text().contains("empty workspace", Qt::CaseInsensitive));

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.workspace_summary_text().contains("sample mode", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("Ready to run bundled sample", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("built-in desktop defaults", Qt::CaseInsensitive));
}

TEST_CASE("UiWorkflow workspace summary updates after import load and run completion", "[ui][P8][P8-009][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M3\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.workspace_summary_text().contains("imported customer project", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("Artifacts: 5", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("imported rule pack", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("scene and graph loaded", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("local job pipeline", Qt::CaseInsensitive));
    REQUIRE(window.loaded_import_design_session_summary_text().contains("Design layers:", Qt::CaseInsensitive));
    REQUIRE(window.loaded_import_design_session_summary_text().contains("Graph nodes:", Qt::CaseInsensitive));

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() {
         return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
     }, 5000));
    REQUIRE(window.workspace_summary_text().contains("Violations: 1", Qt::CaseInsensitive));

    // Baseline/diff summary: point at an empty baseline (new findings expected).
    const fs::path baseline_path = root / "reports" / "baseline.json";
    {
        write_file(baseline_path,
                   "{\n"
                   "  \"baseline_schema_version\": 1,\n"
                   "  \"project_name\": \"FalconCPU\",\n"
                   "  \"records\": []\n"
                   "}\n");
    }
    window.set_signoff_baseline_path_for_tests(QString::fromStdString(baseline_path.string()));
    REQUIRE(window.workspace_summary_text().contains("Baseline diff:", Qt::CaseInsensitive));
    REQUIRE(window.workspace_summary_text().contains("new 1", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("UiWorkflow import review and jobs expose accessible metadata and context menus", "[ui][P8][P8-016][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M3\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());

    auto* artifact_table = window.findChild<QTableWidget*>("ImportArtifactTable");
    REQUIRE(artifact_table != nullptr);
    REQUIRE(artifact_table->accessibleName() == "Detected Import Artifacts");
    REQUIRE(artifact_table->contextMenuPolicy() == Qt::CustomContextMenu);
    REQUIRE(!artifact_table->toolTip().isEmpty());

    auto* diagnostics_text = window.findChild<QPlainTextEdit*>("ImportDiagnosticsText");
    REQUIRE(diagnostics_text != nullptr);
    REQUIRE(diagnostics_text->accessibleName() == "Import Validation Diagnostics");
    REQUIRE(!diagnostics_text->toolTip().isEmpty());

    auto* validate_button = window.findChild<QPushButton*>("ImportValidateButton");
    auto* load_button = window.findChild<QPushButton*>("ImportLoadProjectButton");
    REQUIRE(validate_button != nullptr);
    REQUIRE(load_button != nullptr);
    REQUIRE(validate_button->accessibleName() == "Validate Import Files");
    REQUIRE(load_button->accessibleName() == "Load Imported Project");

    auto* job_history_list = window.findChild<QListWidget*>("JobHistoryList");
    REQUIRE(job_history_list != nullptr);
    REQUIRE(job_history_list->accessibleName() == "Job History");
    REQUIRE(job_history_list->contextMenuPolicy() == Qt::CustomContextMenu);

    remove_tree(root);
}

TEST_CASE("UiWorkflow cross-probes between diagnostics artifacts graph nodes and violations", "[ui][P8][P8-015][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const auto ir = make_cross_probe_ir();
    std::vector<std::string> unresolved;
    auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
    REQUIRE(unresolved.empty());
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    const fs::path root = make_temp_dir();
    const fs::path lef = root / "tech.lef";
    const fs::path def = root / "top.def";
    const fs::path netlist = root / "top.v";
    const fs::path rules = root / "aegis_rules.yaml";
    write_file(lef, "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(def, "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(netlist, "module top(input A, output Y); endmodule\n");
    write_file(rules, "rules:\n  - id: FLOATING_NET\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(lef.string()),
                                         QString::fromStdString(def.string()),
                                         QString::fromStdString(netlist.string()),
                                         QString::fromStdString(rules.string())}));
    QApplication::processEvents();

    aegis::rules::Violation linked{"R_LINK", aegis::rules::Severity::Error, "Linked violation"};
    linked.id = "VX";
    linked.location.net_name = "n1";
    linked.location.device_name = "M1";
    linked.metadata = linked.metadata.with("artifact_id", std::string{"artifact-1"});
    linked.metadata = linked.metadata.with("artifact_path", lef.string());
    aegis::rules::ViolationCollection violations;
    violations.add(linked);
    window.set_violations(violations);

    REQUIRE(window.select_diagnostics_row(0));
    REQUIRE(window.trigger_diagnostics_show_related_violations());
    REQUIRE(window.current_violation_id() == "VX");

    REQUIRE(window.select_import_artifact_row(0));
    REQUIRE(window.trigger_import_artifact_show_related_violations());
    REQUIRE(window.selected_violation_count() == 1);
    REQUIRE(window.current_violation_id() == "VX");

    REQUIRE(window.violation_context_action_enabled("navigate_related"));
    REQUIRE(window.trigger_violation_context_action("navigate_related"));
    REQUIRE(window.current_graph_node_name() == "n1");
    REQUIRE(window.current_import_artifact_path().contains("tech.lef", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("UiWorkflow cross-probing degrades gracefully when links are unresolved", "[ui][P8][P8-015][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_workflow_ir()));

    aegis::rules::Violation unlinked{"R_NONE", aegis::rules::Severity::Warning, "No links"};
    unlinked.id = "V-unlinked";
    aegis::rules::ViolationCollection violations;
    violations.add(unlinked);
    window.set_violations(violations);
    window.select_violation_row(0);

    REQUIRE(window.trigger_violation_context_action("navigate_related"));
    REQUIRE(window.last_status_message().contains("No related graph or import metadata", Qt::CaseInsensitive));
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

TEST_CASE("S1-014 first-launch workspace hides optional docks and reveals them on design load and check completion", "[ui][P-S1][S1-014][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;

    // First launch (clean QSettings): only mandatory docks should be visible.
    {
        aegis::ui::MainWindow window;
        REQUIRE(window.workspace_state() == aegis::ui::MainWindow::WorkspaceState::Empty);

        // Mandatory / always-on surfaces.
        REQUIRE(window.is_dock_widget_visible("Workspace Summary"));
        REQUIRE(window.is_dock_widget_visible("Log"));

        // Optional surfaces must be hidden at first launch.
        REQUIRE_FALSE(window.is_dock_widget_visible("Layers"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Graph Explorer"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Hierarchy"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Trace"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Report Preview"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Violations"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Diagnostics"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Jobs"));
    }

    // Setting a scene and a graph transitions to DesignLoaded and reveals
    // the design-aware optional docks.
    {
        aegis::ui::MainWindow window;
        const auto ir = make_workflow_ir();
        window.set_scene(aegis::ui::build_ui_scene(ir));
        std::vector<std::string> unresolved;
        aegis::graph::ConnectivityGraph graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
        window.set_connectivity_graph(&graph);

        REQUIRE(window.workspace_state() == aegis::ui::MainWindow::WorkspaceState::DesignLoaded);
        REQUIRE(window.is_dock_widget_visible("Layers"));
        REQUIRE(window.is_dock_widget_visible("Graph Explorer"));
        REQUIRE(window.is_dock_widget_visible("Hierarchy"));
        REQUIRE(window.is_dock_widget_visible("Properties"));
        REQUIRE(window.is_dock_widget_visible("Trace"));

        // Report Preview / Violations / Diagnostics / Jobs still hidden
        // until a check run produces results.
        REQUIRE_FALSE(window.is_dock_widget_visible("Report Preview"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Violations"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Diagnostics"));
        REQUIRE_FALSE(window.is_dock_widget_visible("Jobs"));
    }

    // Receiving a (possibly empty) violation collection transitions to
    // ChecksRun and reveals the result-oriented docks.
    {
        aegis::ui::MainWindow window;
        const auto ir = make_workflow_ir();
        window.set_scene(aegis::ui::build_ui_scene(ir));
        std::vector<std::string> unresolved;
        aegis::graph::ConnectivityGraph graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
        window.set_connectivity_graph(&graph);

        aegis::rules::ViolationCollection empty_results{};
        window.set_violations(std::move(empty_results));
        // S1-014: set_violations() updates the diagnostics panel but does
        // not force a workspace-state change on its own (the production
        // lifecycle transitions ChecksRun from JobWorkflowController when
        // a job completes). For this test we advance the state directly.
        window.set_workspace_state(aegis::ui::MainWindow::WorkspaceState::ChecksRun);
        REQUIRE(window.workspace_state() == aegis::ui::MainWindow::WorkspaceState::ChecksRun);
        REQUIRE(window.is_dock_widget_visible("Report Preview"));
        REQUIRE(window.is_dock_widget_visible("Violations"));
        REQUIRE(window.is_dock_widget_visible("Diagnostics"));
        REQUIRE(window.is_dock_widget_visible("Jobs"));
    }
}

TEST_CASE("S1-014 user-driven dock hide is remembered and overrides auto-show", "[ui][P-S1][S1-014][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;

    aegis::ui::MainWindow window;
    // The window must be shown for QDockWidget::visibilityChanged to fire
    // reliably. Without an event-loop pump, child show()/hide() may not
    // emit the signal.
    window.show();
    QApplication::processEvents();

    auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("LayersDock"));
    REQUIRE(layers_dock != nullptr);

    // At first launch, Layers is hidden by the Empty-state default. The
    // user has to toggle it on first (or drag it out) before they can
    // record an explicit hide intent.
    layers_dock->show();
    QApplication::processEvents();
    REQUIRE_FALSE(window.is_dock_hidden_by_user("Layers"));

    layers_dock->hide();
    QApplication::processEvents();
    REQUIRE(window.is_dock_hidden_by_user("Layers"));

    const auto ir = make_workflow_ir();
    window.set_scene(aegis::ui::build_ui_scene(ir));
    std::vector<std::string> unresolved;
    aegis::graph::ConnectivityGraph graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
    window.set_connectivity_graph(&graph);

    REQUIRE(window.workspace_state() == aegis::ui::MainWindow::WorkspaceState::DesignLoaded);
    REQUIRE_FALSE(window.is_dock_widget_visible("Layers"));
    REQUIRE(window.is_dock_widget_visible("Graph Explorer"));

    // A subsequent show() through the View menu must clear the user-override
    // and let auto-show work normally on future transitions.
    layers_dock->show();
    QApplication::processEvents();
    REQUIRE_FALSE(window.is_dock_hidden_by_user("Layers"));

    window.set_workspace_state(aegis::ui::MainWindow::WorkspaceState::Empty);
    REQUIRE_FALSE(window.is_dock_widget_visible("Layers"));
    window.set_workspace_state(aegis::ui::MainWindow::WorkspaceState::DesignLoaded);
    REQUIRE(window.is_dock_widget_visible("Layers"));
}

TEST_CASE("S1-014 View menu exposes a Panels submenu with toggleable dock actions", "[ui][P-S1][S1-014][UiWorkflow]")
{
    QtAppGuard app;
    SettingsCleanupGuard settings_guard;

    aegis::ui::MainWindow window;
    QMenuBar* menu_bar = window.menuBar();
    REQUIRE(menu_bar != nullptr);

    // Walk the menu hierarchy to find the View → Panels submenu.
    QMenu* view_menu = nullptr;
    for (QAction* action : menu_bar->actions()) {
        if (action->text().contains("View", Qt::CaseInsensitive)) {
            view_menu = action->menu();
            break;
        }
    }
    REQUIRE(view_menu != nullptr);

    QMenu* panels_menu = nullptr;
    for (QAction* action : view_menu->actions()) {
        if (action->menu() != nullptr && action->menu()->title() == "&Panels") {
            panels_menu = action->menu();
            break;
        }
    }
    REQUIRE(panels_menu != nullptr);

    QStringList panel_titles;
    for (QAction* action : panels_menu->actions()) {
        if (action->isCheckable()) {
            panel_titles.append(action->text());
        }
    }
    REQUIRE(panel_titles.contains("Layers"));
    REQUIRE(panel_titles.contains("Violations"));
    REQUIRE(panel_titles.contains("Report Preview"));
    REQUIRE(panel_titles.contains("Jobs"));
    REQUIRE(panel_titles.contains("Log"));
}
