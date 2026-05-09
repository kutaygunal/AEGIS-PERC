#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/ui/violation_table_model.hpp"

#include <QApplication>
#include <QClipboard>

#include <cmath>
#include <filesystem>
#include <fstream>

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
    ir.design_name = "violation_explorer_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.nets.push_back(Net{"n1", {"IN", "MDRV.gate"}, {}});
    ir.devices.push_back(Device{"MDRV", "NMOS", {{"gate", "n1"}}, {}});
    ir.ports.push_back(Port{"IN", "INPUT", "n1", std::string{"M1"}, Point{5.0, 5.0}});
    return ir;
}

aegis::graph::ConnectivityGraph make_graph(const LayoutIR& ir)
{
    std::vector<std::string> unresolved;
    auto graph = aegis::graph::ConnectivityGraph::from_layout_ir(ir, unresolved);
    REQUIRE(unresolved.empty());
    return graph;
}

ViolationCollection make_violations()
{
    ViolationCollection violations;

    Violation a{"R_WIDTH", Severity::Warning, "Width issue"};
    a.id = "V-001";
    a.location.layer = "M1";
    a.location.net_name = "n1";
    a.location.pin_name = "IN";
    a.location.point = aegis::graph::Point{5.0, 5.0};
    a.metadata = a.metadata.with("limit", std::string{"0.12"});
    a.metadata = a.metadata.with("measured", std::string{"0.09"});
    violations.add(a);

    Violation b{"R_SPACING", Severity::Error, "Spacing issue"};
    b.id = "V-002";
    b.location.layer = "M2";
    b.location.device_name = "M2_SEG";
    b.metadata = b.metadata.with("distance", std::string{"0.02"});
    violations.add(b);

    return violations;
}

namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("Violation table model exposes required explorer columns", "[ui][P3-007][ViolationExplorer]")
{
    ViolationTableModel model;
    model.set_violations(make_violations());

    REQUIRE(model.columnCount() == ViolationTableModel::ColumnCount);
    REQUIRE(model.rowCount() == 2);
    REQUIRE(model.headerData(ViolationTableModel::RuleIdColumn, Qt::Horizontal, Qt::DisplayRole).toString() == "Rule ID");
    REQUIRE(model.headerData(ViolationTableModel::PinColumn, Qt::Horizontal, Qt::DisplayRole).toString() == "Pin");
    REQUIRE(model.data(model.index(0, ViolationTableModel::RuleIdColumn)).toString() == "R_WIDTH");
    REQUIRE(model.data(model.index(0, ViolationTableModel::SeverityColumn)).toString() == "warning");
}

TEST_CASE("Violation explorer details show copyable ID and metadata", "[ui][P3-007][ViolationExplorer]")
{
    QtAppGuard guard;
    ViolationExplorerPanel panel;
    panel.set_violations(make_violations());

    REQUIRE(panel.violation_count() == 2);
    REQUIRE(panel.current_violation_id() == "V-001");
    REQUIRE(panel.details_summary_text().contains("Rule: R_WIDTH"));
    REQUIRE(panel.details_summary_text().contains("point=(5.00, 5.00)"));
    REQUIRE(panel.metadata_row_count() == 2);
}

TEST_CASE("Selecting a violation updates details and centers canvas", "[ui][P3-007][ViolationExplorer]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.violation_explorer_count() == 2);
    REQUIRE(window.current_violation_id() == "V-001");
    REQUIRE(std::abs(window.canvas_view_center().x() - 5.0) < 0.001);
    REQUIRE(std::abs(window.canvas_view_center().y() - 5.0) < 0.001);

    window.select_violation_row(1);
    REQUIRE(window.current_violation_id() == "V-002");
    REQUIRE(window.violation_details_text().contains("device=M2_SEG"));
    REQUIRE(window.violation_metadata_row_count() == 1);
    REQUIRE(std::abs(window.canvas_view_center().x() - 115.0) < 0.001);
    REQUIRE(std::abs(window.canvas_view_center().y() - 55.0) < 0.001);
}

TEST_CASE("Violation explorer supports bulk copy export and context actions", "[ui][P8][P8-012][ViolationExplorer]")
{
    QtAppGuard guard;
    MainWindow window;
    const auto ir = make_ir();
    auto graph = make_graph(ir);
    window.set_scene(build_ui_scene(ir));
    window.set_connectivity_graph(&graph);
    window.set_violations(make_violations());

    const fs::path out = fs::temp_directory_path() / "aegis_selected_violations.json";
    std::error_code ec;
    fs::remove(out, ec);
    window.select_violation_rows({});
    REQUIRE(window.selected_violation_count() == 0);
    REQUIRE_FALSE(window.violation_context_action_enabled("copy_selected"));
    REQUIRE_FALSE(window.violation_context_action_enabled("export_selected"));
    window.set_report_export_path_picker_for_tests([&out](const QString& kind) {
        return kind == "violation_json" ? QString::fromStdString(out.string()) : QString{};
    });

    window.select_violation_rows({0, 1});
    REQUIRE(window.selected_violation_count() == 2);
    REQUIRE(window.violation_context_action_enabled("copy_selected"));
    REQUIRE(window.violation_context_action_enabled("export_selected"));
    REQUIRE(window.trigger_violation_context_action("copy_selected"));
    REQUIRE(QApplication::clipboard()->text().contains("V-001"));
    REQUIRE(QApplication::clipboard()->text().contains("V-002"));
    REQUIRE(window.trigger_violation_context_action("export_selected"));
    REQUIRE(read_file(out).find("V-001") != std::string::npos);
    REQUIRE(read_file(out).find("V-002") != std::string::npos);
    int bulk_action_logs = 0;
    for (const auto& entry : window.activity_log_entries()) {
        if (entry.contains("Violation bulk action", Qt::CaseInsensitive)) {
            ++bulk_action_logs;
        }
    }
    REQUIRE(bulk_action_logs == 2);

    window.select_violation_row(0);
    REQUIRE(window.violation_context_action_enabled("trace"));
    REQUIRE(window.violation_context_action_enabled("center"));
    REQUIRE(window.violation_context_action_enabled("copy_id"));
    REQUIRE(window.violation_context_action_enabled("copy_details"));
    REQUIRE(window.trigger_violation_context_action("copy_id"));
    REQUIRE(QApplication::clipboard()->text() == "V-001");
    REQUIRE(window.trigger_violation_context_action("copy_details"));
    REQUIRE(QApplication::clipboard()->text().contains("Width issue"));
    REQUIRE(window.trigger_violation_context_action("trace"));
    REQUIRE(window.has_active_trace());
}
