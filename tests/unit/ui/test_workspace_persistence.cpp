#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QSettings>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

using Catch::Approx;

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
    ir.design_name = "workspace_persistence";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    return ir;
}

aegis::rules::ViolationCollection make_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;

    Violation a{"R_WARN", Severity::Warning, "Width issue"};
    a.id = "V1";
    a.location.layer = "M1";
    violations.push_back(a);

    Violation b{"R_ERR", Severity::Error, "Spacing issue"};
    b.id = "V2";
    b.location.layer = "M2";
    b.location.net_name = "n2";
    violations.push_back(b);

    return ViolationCollection{std::move(violations)};
}

namespace fs = std::filesystem;

QDockWidget* find_dock(aegis::ui::MainWindow& window, const QString& title)
{
    for (auto* dock : window.findChildren<QDockWidget*>()) {
        if (dock != nullptr && dock->windowTitle() == title) {
            return dock;
        }
    }
    return nullptr;
}

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_workspace_persistence_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST_CASE("WorkspacePersistence saves and restores empty-workspace UI state", "[ui][P7][P7-006][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    {
        aegis::ui::MainWindow window;
        window.show();
        QApplication::processEvents();

        auto* graph_dock = find_dock(window, "Graph Explorer");
        REQUIRE(graph_dock != nullptr);
        graph_dock->hide();
        QApplication::processEvents();

        window.set_heatmap_visible(true);
        window.set_heatmap_opacity(0.42);
        window.set_performance_metrics_visible(true);
        window.save_window_state();
    }

    {
        aegis::ui::MainWindow restored;
        restored.show();
        QApplication::processEvents();

        REQUIRE_FALSE(restored.is_dock_widget_visible("Graph Explorer"));
        REQUIRE(restored.heatmap_visible());
        REQUIRE(restored.heatmap_opacity() == Approx(0.42).margin(0.011));
        REQUIRE(restored.performance_metrics_visible());
        REQUIRE(restored.grid_visible());
        REQUIRE(restored.violation_overlays_visible());
        REQUIRE(restored.violation_filter_state() == aegis::ui::ViolationFilterState{});
    }
}

TEST_CASE("WorkspacePersistence saves and restores populated-workspace filters overlays and metrics", "[ui][P7][P7-006][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    {
        aegis::ui::MainWindow window;
        window.set_scene(aegis::ui::build_ui_scene(make_ir()));
        window.set_violations(make_violations());
        window.show();
        QApplication::processEvents();

        auto* violations_dock = find_dock(window, "Violations");
        REQUIRE(violations_dock != nullptr);
        violations_dock->hide();
        QApplication::processEvents();

        REQUIRE(window.trigger_workspace_action("toggle_grid"));
        REQUIRE(window.trigger_workspace_action("toggle_overlays"));
        window.set_heatmap_visible(true);
        window.set_heatmap_opacity(0.75);
        window.set_performance_metrics_visible(true);

        aegis::ui::ViolationFilterState state;
        state.severity = aegis::rules::Severity::Warning;
        state.layer = "M1";
        state.search_text = "Width";
        window.set_violation_filter_state(state);
        window.save_window_state();
    }

    {
        aegis::ui::MainWindow restored;
        restored.set_scene(aegis::ui::build_ui_scene(make_ir()));
        restored.set_violations(make_violations());
        restored.show();
        QApplication::processEvents();

        REQUIRE_FALSE(restored.is_dock_widget_visible("Violations"));
        REQUIRE_FALSE(restored.grid_visible());
        REQUIRE_FALSE(restored.violation_overlays_visible());
        REQUIRE(restored.heatmap_visible());
        REQUIRE(restored.heatmap_opacity() == Approx(0.75).margin(0.011));
        REQUIRE(restored.performance_metrics_visible());

        const auto filter_state = restored.violation_filter_state();
        REQUIRE(filter_state.severity == aegis::rules::Severity::Warning);
        REQUIRE(filter_state.layer == "M1");
        REQUIRE(filter_state.search_text == "Width");
        REQUIRE(restored.violation_filter_summary_text().contains("1 / 2 violations"));
    }
}

TEST_CASE("WorkspacePersistence persists recent imported projects and reopen-last-session preference", "[ui][P8][P8-010][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M3\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");

    {
        aegis::ui::MainWindow window;
        REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
        QApplication::processEvents();
        REQUIRE(window.trigger_import_load_action());
        window.set_reopen_last_session_enabled(true);
        window.save_window_state();
    }

    {
        aegis::ui::MainWindow restored;
        restored.show();
        QApplication::processEvents();
        REQUIRE(restored.reopen_last_session_enabled());
        REQUIRE(restored.recent_project_paths().contains(QString::fromStdString(root.string())));
        REQUIRE(restored.has_loaded_import_package());
        REQUIRE(restored.loaded_import_project_name().contains("workspace_persistence", Qt::CaseInsensitive));
    }
}

TEST_CASE("WorkspacePersistence drops stale recent imported projects gracefully", "[ui][P8][P8-010][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    const QString stale = QString::fromStdString((make_temp_dir() / "missing_project").string());
    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    settings.setValue("mainWindow/recentProjects/version", 1);
    settings.setValue("mainWindow/recentProjects/paths", QStringList{stale});
    settings.setValue("mainWindow/recentProjects/reopenLastSession", true);
    settings.setValue("mainWindow/recentProjects/lastSuccessfulPath", stale);
    settings.sync();

    aegis::ui::MainWindow restored;
    restored.show();
    QApplication::processEvents();

    REQUIRE_FALSE(restored.recent_project_paths().contains(stale));
    REQUIRE_FALSE(restored.has_loaded_import_package());
}

TEST_CASE("WorkspacePersistence persists named filter presets and workspace views across restart", "[ui][P8][P8-013][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    {
        aegis::ui::MainWindow window;
        window.set_scene(aegis::ui::build_ui_scene(make_ir()));
        window.set_violations(make_violations());
        window.show();
        QApplication::processEvents();

        auto* graph_dock = find_dock(window, "Graph Explorer");
        REQUIRE(graph_dock != nullptr);
        graph_dock->hide();
        QApplication::processEvents();

        aegis::ui::ViolationFilterState state;
        state.severity = aegis::rules::Severity::Warning;
        state.layer = "M1";
        state.search_text = "Width";
        window.set_violation_filter_state(state);
        REQUIRE(window.save_violation_filter_preset("Review"));

        REQUIRE(window.trigger_workspace_action("toggle_grid"));
        REQUIRE(window.trigger_workspace_action("toggle_overlays"));
        window.set_heatmap_visible(true);
        window.set_heatmap_opacity(0.33);
        window.set_performance_metrics_visible(true);
        REQUIRE(window.save_workspace_view("Analysis"));
        REQUIRE(window.rename_violation_filter_preset("Review", "Review Renamed"));
        REQUIRE(window.rename_workspace_view("Analysis", "Analysis Renamed"));
    }

    {
        aegis::ui::MainWindow restored;
        restored.set_scene(aegis::ui::build_ui_scene(make_ir()));
        restored.set_violations(make_violations());
        restored.show();
        QApplication::processEvents();

        REQUIRE(restored.violation_filter_preset_names() == QStringList{"Review Renamed"});
        REQUIRE(restored.workspace_view_names() == QStringList{"Analysis Renamed"});
        REQUIRE(restored.apply_violation_filter_preset("Review Renamed"));
        const auto filter_state = restored.violation_filter_state();
        REQUIRE(filter_state.severity == aegis::rules::Severity::Warning);
        REQUIRE(filter_state.layer == "M1");
        REQUIRE(filter_state.search_text == "Width");

        REQUIRE(restored.apply_workspace_view("Analysis Renamed"));
        REQUIRE_FALSE(restored.is_dock_widget_visible("Graph Explorer"));
        REQUIRE_FALSE(restored.grid_visible());
        REQUIRE_FALSE(restored.violation_overlays_visible());
        REQUIRE(restored.heatmap_visible());
        REQUIRE(restored.heatmap_opacity() == Approx(0.33).margin(0.011));
        REQUIRE(restored.performance_metrics_visible());

        REQUIRE(restored.delete_violation_filter_preset("Review Renamed"));
        REQUIRE(restored.delete_workspace_view("Analysis Renamed"));
        REQUIRE(restored.violation_filter_preset_names().isEmpty());
        REQUIRE(restored.workspace_view_names().isEmpty());
    }
}

TEST_CASE("WorkspacePersistence uses tolerant defaults for partial saved values", "[ui][P7][P7-006][WorkspacePersistence]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    QSettings settings("AEGIS-PERC", "AEGIS-PERC");
    settings.setValue("mainWindow/workspaceUi/version", 1);
    settings.setValue("mainWindow/workspaceUi/gridVisible", false);
    settings.sync();

    aegis::ui::MainWindow restored;
    restored.show();
    QApplication::processEvents();

    REQUIRE_FALSE(restored.grid_visible());
    REQUIRE(restored.violation_overlays_visible());
    REQUIRE_FALSE(restored.heatmap_visible());
    REQUIRE(restored.heatmap_opacity() == Approx(0.6).margin(0.011));
    REQUIRE_FALSE(restored.performance_metrics_visible());
    REQUIRE(restored.violation_filter_state() == aegis::ui::ViolationFilterState{});
}
