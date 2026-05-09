#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/main_window.hpp"

#include <QApplication>
#include <QSettings>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

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

fs::path make_temp_dir()
{
    return fs::temp_directory_path() /
           ("aegis_import_wizard_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

} // namespace

TEST_CASE("ImportWizard exposes import action and opens review dialog from picker-selected paths", "[ui][P6][P6-007][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");

    window.set_import_picker_for_tests([root](QWidget*) {
        return QStringList{QString::fromStdString(root.string())};
    });

    REQUIRE(window.workspace_action_ids().contains("import_project"));
    REQUIRE(window.trigger_workspace_action("import_project"));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.last_status_message().contains("Opened import review", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("ImportWizard reports cancellation cleanly when picker returns no paths", "[ui][P8][P8-001][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    window.set_import_picker_for_tests([](QWidget*) {
        return QStringList{};
    });

    REQUIRE(window.trigger_workspace_action("import_project"));
    QApplication::processEvents();
    REQUIRE_FALSE(window.is_import_dialog_visible());
    REQUIRE(window.last_status_message().contains("Import canceled", Qt::CaseInsensitive));
}

TEST_CASE("ImportWizard validates incomplete package then allows correction by overriding role", "[ui][P6][P6-007][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    const fs::path lef = root / "tech.lef";
    const fs::path def = root / "top.def";
    const fs::path netlist = root / "top.v";
    const fs::path rules = root / "rules.txt";

    write_file(lef, "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(def, "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(netlist, "module top(input A, output Y); endmodule\n");
    write_file(rules, "rules:\n  - id: FLOATING_NET\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(lef.string()),
                                         QString::fromStdString(def.string()),
                                         QString::fromStdString(netlist.string()),
                                         QString::fromStdString(rules.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.import_has_blockers());
    REQUIRE(window.import_validation_summary_text().contains("Missing required AEGIS rule pack artifact"));
    REQUIRE_FALSE(window.import_load_action_enabled());

    REQUIRE(window.import_artifact_row_count() == 4);
    REQUIRE(window.import_artifact_role_text(3) == "imported_report");
    REQUIRE(window.import_artifact_status_text(3).contains("warning", Qt::CaseInsensitive));

    REQUIRE(window.set_import_artifact_role_from_ui(3, "aegis_rule_pack"));
    QApplication::processEvents();
    REQUIRE_FALSE(window.import_has_blockers());
    REQUIRE(window.import_load_action_enabled());
    REQUIRE(window.import_detected_roles().contains("aegis_rule_pack"));
    REQUIRE(window.import_artifact_role_text(3) == "aegis_rule_pack");

    remove_tree(root);
}

TEST_CASE("ImportWizard accepts customer project folder and shows optional enrichments", "[ui][P6][P6-007][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");
    write_file(root / "power/power_domains.csv", "instance,domain,voltage\nCPU_CORE,CORE_0V8,0.8\n");
    write_file(root / "reports/current_report.csv", "net_name,current_mA,voltage_domain,layer\nVDD_CPU,52.4,CORE_0V8,M4\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE_FALSE(window.import_has_blockers());
    REQUIRE(window.import_load_action_enabled());
    REQUIRE(window.import_artifact_row_count() == 6);
    REQUIRE(window.import_detected_roles().contains("lef"));
    REQUIRE(window.import_detected_roles().contains("def"));
    REQUIRE(window.import_detected_roles().contains("verilog"));
    REQUIRE(window.import_detected_roles().contains("aegis_rule_pack"));
    REQUIRE(window.import_detected_roles().contains("power_domains_csv"));
    REQUIRE(window.import_detected_roles().contains("current_csv"));
    REQUIRE(window.import_validation_summary_text().contains("optional", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("ImportWizard presents an interactive artifact table with diagnostics alongside it", "[ui][P8][P8-002][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/rules.txt", "rules:\n  - id: FLOATING_NET\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.import_artifact_row_count() == 4);
    REQUIRE(window.import_validation_summary_text().contains("Validation Diagnostics", Qt::CaseInsensitive));
    REQUIRE(window.import_artifact_role_text(3) == "imported_report");
    REQUIRE(window.set_import_artifact_role_from_ui(3, "aegis_rule_pack"));
    QApplication::processEvents();
    REQUIRE(window.import_artifact_role_text(3) == "aegis_rule_pack");
    REQUIRE_FALSE(window.import_has_blockers());

    remove_tree(root);
}

TEST_CASE("ImportWizard exposes actionable diagnostics panel with filtering and artifact navigation", "[ui][P8][P8-011][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.dock_widget_titles().contains("Diagnostics"));
    REQUIRE(window.diagnostics_entry_count() > 0);
    REQUIRE(window.set_diagnostics_severity_filter("error"));
    REQUIRE(window.diagnostics_entry_count() > 0);
    REQUIRE(window.select_diagnostics_row(0));
    REQUIRE(window.diagnostics_details_text().contains("Import diagnostic", Qt::CaseInsensitive));
    REQUIRE(window.diagnostics_details_text().contains("Code:", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("ImportWizard load action commits validated package into workspace and closes review dialog", "[ui][P8][P8-003][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/technology.lef", "VERSION 5.8 ;\nLAYER M1\n  TYPE ROUTING ;\n  WIDTH 0.10 ;\n  PITCH 0.20 ;\n  DIRECTION HORIZONTAL ;\nEND M1\nEND LIBRARY\n");
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");
    write_file(root / "netlist/top.v", "module top(input A, output Y); endmodule\n");
    write_file(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.import_load_action_enabled());
    REQUIRE_FALSE(window.has_loaded_import_package());

    REQUIRE(window.trigger_import_load_action());
    QApplication::processEvents();
    REQUIRE_FALSE(window.is_import_dialog_visible());
    REQUIRE(window.has_loaded_import_package());
    REQUIRE(window.loaded_import_project_name() == QString::fromStdString(root.filename().string()));
    REQUIRE(window.last_status_message().contains("Loaded imported project package", Qt::CaseInsensitive));

    remove_tree(root);
}

TEST_CASE("ImportWizard load action stays disabled while blocking diagnostics remain", "[ui][P8][P8-003][ImportWizard]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    const fs::path root = make_temp_dir();
    write_file(root / "layout/top.def", "VERSION 5.8 ;\nDESIGN top ;\n");

    REQUIRE(window.import_project_paths({QString::fromStdString(root.string())}));
    QApplication::processEvents();
    REQUIRE(window.is_import_dialog_visible());
    REQUIRE(window.import_has_blockers());
    REQUIRE_FALSE(window.import_load_action_enabled());
    REQUIRE_FALSE(window.trigger_import_load_action());
    REQUIRE_FALSE(window.has_loaded_import_package());

    remove_tree(root);
}
