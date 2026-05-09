#include <catch2/catch_test_macros.hpp>

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

namespace fs = std::filesystem;

namespace {

fs::path repo_import_package_dir()
{
    const fs::path rooted = fs::path(AEGIS_SOURCE_DIR) / "data" / "import_packages" / "openframe_simple_design";
    if (fs::exists(rooted)) {
        return rooted;
    }
    return fs::path("..") / ".." / "data" / "import_packages" / "openframe_simple_design";
}

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

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_run_checks_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

aegis::parsing::LayoutIR make_scene_only_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "scene_only";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    return ir;
}

} // namespace

TEST_CASE("RunChecks executes real electrical rules for bundled sample graphs", "[ui][P7][P7-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.trigger_workspace_action("run_checks"));

    REQUIRE(window.last_status_message().contains("Run Checks completed"));
    REQUIRE(window.violation_explorer_count() > 0);
    REQUIRE(window.visible_violation_overlay_count() > 0);

    const auto entries = window.activity_log_entries();
    bool saw_start = false;
    bool saw_complete = false;
    for (const auto& entry : entries) {
        if (entry.contains("Run Checks started")) {
            saw_start = true;
        }
        if (entry.contains("Run Checks completed")) {
            saw_complete = true;
        }
    }
    REQUIRE(saw_start);
    REQUIRE(saw_complete);
}

TEST_CASE("RunChecks reports no-graph failure for scene-only workflows", "[ui][P7][P7-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    window.set_scene(aegis::ui::build_ui_scene(make_scene_only_ir()));
    REQUIRE_FALSE(window.workspace_action_enabled("run_checks"));
    REQUIRE_FALSE(window.trigger_workspace_action("run_checks"));
    REQUIRE(window.violation_explorer_count() == 0);
}

TEST_CASE("RunChecks executes repo import package without preloaded sample graph", "[ui][P8][ImportPackage][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = repo_import_package_dir();
    REQUIRE(fs::exists(root / "layout" / "technology.lef"));
    REQUIRE(fs::exists(root / "layout" / "top.def"));
    REQUIRE(fs::exists(root / "netlist" / "top.v"));
    REQUIRE(fs::exists(root / "rules" / "aegis_rules.yaml"));
    REQUIRE(fs::exists(root / "reports" / "current_report.csv"));

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.import_load_action_enabled());
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.has_loaded_import_package());
    REQUIRE(window.workspace_summary_text().contains("local job pipeline", Qt::CaseInsensitive));
    REQUIRE(window.workspace_action_enabled("run_checks"));

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }, 5000));
    REQUIRE(window.last_status_message().contains("local job pipeline", Qt::CaseInsensitive));
    REQUIRE(window.violation_explorer_count() == 1);
    REQUIRE(window.visible_violation_overlay_count() == 1);
}

TEST_CASE("RunChecks uses imported rule packs and current enrichment in desktop mode", "[ui][P8][P8-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3 ;\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.import_load_action_enabled());
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }));
    REQUIRE(window.last_status_message().contains("local job pipeline", Qt::CaseInsensitive));
    REQUIRE(window.violation_explorer_count() == 1);
    REQUIRE(window.visible_violation_overlay_count() == 1);

    const auto entries = window.activity_log_entries();
    bool saw_import_mode = false;
    bool saw_stage = false;
    bool saw_parse_detail = false;
    for (const auto& entry : entries) {
        saw_import_mode = saw_import_mode || entry.contains("imported package content", Qt::CaseInsensitive);
        saw_stage = saw_stage || entry.contains("Run Checks pipeline: parse", Qt::CaseInsensitive);
        saw_parse_detail = saw_parse_detail || entry.contains("Parsing imported artifacts", Qt::CaseInsensitive);
    }
    REQUIRE(saw_import_mode);
    REQUIRE(saw_stage);
    REQUIRE(saw_parse_detail);

    remove_tree(root);
}

TEST_CASE("RunChecks uses local job pipeline progress for imported desktop execution while sample mode remains synchronous", "[ui][P8][P8-005][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(window.last_status_message().contains("built-in desktop defaults", Qt::CaseInsensitive));

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3 ;\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE_FALSE(window.workspace_action_enabled("run_checks"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }));
    REQUIRE(window.workspace_action_enabled("run_checks"));
    REQUIRE(window.last_status_message().contains("local job pipeline", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("RunChecks surfaces imported package parsing failures with actionable feedback", "[ui][P8][P8-004][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3 ;\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,,CORE_0V8,metal_1\n");

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.import_load_action_enabled());
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks failed", Qt::CaseInsensitive);
    }));
    REQUIRE(window.last_status_message().contains("current/activity", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("RunChecks exposes visible progress and cancel controls for imported desktop jobs", "[ui][P8][P8-006][RunChecks]")
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
    REQUIRE(wait_until([&window]() {
        return window.has_active_job() && !window.job_progress_text().isEmpty();
    }));
    REQUIRE(window.job_progress_text().contains("Job:", Qt::CaseInsensitive));
    REQUIRE(window.workspace_action_enabled("cancel_active_job"));
    REQUIRE_FALSE(window.workspace_action_enabled("run_checks"));

    REQUIRE(window.trigger_workspace_action("cancel_active_job"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("canceled", Qt::CaseInsensitive);
    }, 5000));
    REQUIRE_FALSE(window.has_active_job());
    REQUIRE(window.workspace_action_enabled("run_checks"));

    remove_tree(root);
}

TEST_CASE("RunChecks exposes retry after imported local job failure when inputs remain valid", "[ui][P8][P8-006][RunChecks]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M3 ;\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: EM_CURRENT_LIMIT\n    type: em_current_limit\n    severity: medium\n    parameters:\n      metal_1_max_mA: 20\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,,CORE_0V8,metal_1\n");

    REQUIRE(window.load_bundled_sample("inverter"));
    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();

    REQUIRE(window.trigger_workspace_action("run_checks"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks failed", Qt::CaseInsensitive);
    }));
    REQUIRE(window.workspace_action_enabled("retry_last_job"));

    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nvdd,52.4,CORE_0V8,metal_1\n");
    REQUIRE(window.trigger_workspace_action("retry_last_job"));
    REQUIRE(wait_until([&window]() {
        return window.last_status_message().contains("Run Checks completed", Qt::CaseInsensitive);
    }, 5000));
    REQUIRE(window.last_status_message().contains("local job pipeline", Qt::CaseInsensitive));

    remove_tree(root);
}
