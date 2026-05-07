#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>

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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "report_preview_test";
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

TEST_CASE("Report preview summarizes scene and filtered violation counts", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.dock_widget_titles().contains("Report Preview"));
    REQUIRE(window.report_preview_summary_text().contains("Design: report_preview_test"));
    REQUIRE(window.report_preview_summary_text().contains("Layers: 2"));
    REQUIRE(window.report_preview_summary_text().contains("Violations: 2"));
    REQUIRE(window.report_preview_summary_text().contains("warning=1 error=1"));
}

TEST_CASE("Report preview updates for filtered violations and selected violation details", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    aegis::ui::ViolationFilterState state;
    state.layer = "M1";
    window.set_violation_filter_state(state);
    REQUIRE(window.report_preview_summary_text().contains("Violations: 1"));

    window.select_violation_row(0);
    REQUIRE(window.report_preview_summary_text().contains("Selected violation: V1"));
    REQUIRE(window.report_preview_summary_text().contains("message=Width issue"));
}

TEST_CASE("Report preview exposes snapshot hook status", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.report_preview_snapshot_status_text() == "Canvas snapshot ready");
    REQUIRE(window.report_preview_summary_text().contains("Export is reserved for P5."));
}
