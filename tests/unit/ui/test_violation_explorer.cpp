#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/ui/violation_table_model.hpp"

#include <QApplication>

#include <cmath>

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
    ir.ports.push_back(Port{"IN", "INPUT", "n1", std::string{"M1"}, Point{5.0, 5.0}});
    return ir;
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
