#include <catch2/catch_test_macros.hpp>

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QSettings>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>

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
    ir.design_name = "ui_notifications";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.nets.push_back(Net{"n1", {"A", "M1.gate"}, {}});
    ir.devices.push_back(Device{"M1", "NMOS", {{"gate", "n1"}}, {}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    return ir;
}

aegis::graph::ConnectivityGraph make_graph(const aegis::parsing::LayoutIR& ir)
{
    std::vector<std::string> unresolved;
    auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
    REQUIRE(unresolved.empty());
    return graph;
}

namespace fs = std::filesystem;

int count_entries_containing(const QStringList& entries, const QString& needle)
{
    int count = 0;
    for (const auto& entry : entries) {
        if (entry.contains(needle, Qt::CaseInsensitive)) {
            ++count;
        }
    }
    return count;
}

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_ui_notifications_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST_CASE("UiNotifications routes sample-load notifications to status and activity log", "[ui][P7][P7-011][UiNotifications]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.last_status_message() == "Loaded sample: inverter");
    REQUIRE(window.activity_log_entries().back().contains("Loaded sample: inverter"));
    REQUIRE(count_entries_containing(window.activity_log_entries(), "Loaded sample: inverter") == 1);
}

TEST_CASE("UiNotifications routes trace failures to status log and trace panel without duplicate request logs", "[ui][P7][P7-011][UiNotifications]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    const auto ir = make_ir();
    auto graph = make_graph(ir);

    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(ir));
    window.set_connectivity_graph(&graph);

    REQUIRE_FALSE(window.request_trace_by_name("missing_net"));
    REQUIRE(window.trace_status_text().contains("Trace target not found: missing_net"));
    REQUIRE(window.last_status_message() == window.trace_status_text());
    REQUIRE(window.activity_log_entries().back().contains("Trace target not found: missing_net"));
    REQUIRE(count_entries_containing(window.activity_log_entries(), "Trace requested:") == 0);
}

TEST_CASE("UiNotifications routes completed checks and filter updates consistently", "[ui][P7][P7-011][UiNotifications]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(window.last_status_message().contains("Run Checks completed"));
    REQUIRE(window.activity_log_entries().back().contains("Run Checks completed"));

    aegis::ui::ViolationFilterState state;
    state.layer = "NO_MATCH";
    window.set_violation_filter_state(state);
    REQUIRE(window.last_status_message() == window.violation_filter_summary_text());
    REQUIRE(window.activity_log_entries().back().contains(window.violation_filter_summary_text()));
}

TEST_CASE("UiNotifications logs cancellation and retry outcomes for local workflow jobs", "[ui][P8][P8-006][UiNotifications]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_job_pipeline_artificial_delay_for_tests(80);

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3 ;\n");
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
    REQUIRE(wait_until([&window]() { return window.has_active_job(); }));
    REQUIRE(window.trigger_workspace_action("cancel_active_job"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("canceled", Qt::CaseInsensitive);
    }, 5000));

    REQUIRE(window.trigger_workspace_action("retry_last_job"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }, 5000));

    const auto entries = window.activity_log_entries();
    REQUIRE(count_entries_containing(entries, "Cancellation requested for active local job") >= 1);
    REQUIRE(count_entries_containing(entries, "Run Checks canceled in local job pipeline") >= 1);
    REQUIRE(count_entries_containing(entries, "Retrying Run Checks using imported package content via local job pipeline") >= 1);
    REQUIRE(count_entries_containing(entries, "Run Checks completed") >= 1);

    remove_tree(root);
}
