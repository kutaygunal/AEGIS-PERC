#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"
#include "aegis/ui/violation_heatmap.hpp"

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
    ir.design_name = "heatmap_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, aegis::parsing::Point{10.0, 10.0}});
    ir.ports.push_back(Port{"Y", "OUTPUT", "n2", std::string{"M2"}, aegis::parsing::Point{115.0, 55.0}});
    return ir;
}

ViolationCollection make_violations()
{
    std::vector<Violation> violations;

    Violation a{"R1", Severity::Warning, "M1 issue"};
    a.id = "V1";
    a.location.layer = "M1";
    a.location.point = aegis::graph::Point{10.0, 10.0};
    violations.push_back(a);

    Violation b{"R2", Severity::Error, "M2 issue"};
    b.id = "V2";
    b.location.layer = "M2";
    b.location.point = aegis::graph::Point{115.0, 55.0};
    violations.push_back(b);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("Heatmap colors are centralized", "[ui][P3-011][ViolationHeatmap]")
{
    REQUIRE(violation_heatmap_color(0.0, 0.5).isValid());
    REQUIRE(violation_heatmap_color(0.0, 0.5) != violation_heatmap_color(1.0, 0.5));
}

TEST_CASE("LayoutCanvas builds heatmap buckets and empty state", "[ui][P3-011][ViolationHeatmap]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_ir()));
    canvas.set_violations(make_violations());

    REQUIRE(canvas.heatmap_bucket_count() == 2);
    REQUIRE(canvas.heatmap_empty_state_text().isEmpty());

    canvas.set_violations(ViolationCollection{});
    REQUIRE(canvas.heatmap_bucket_count() == 0);
    REQUIRE(canvas.heatmap_empty_state_text() == "Run checks or clear filters to generate violation heatmap data.");
}

TEST_CASE("MainWindow heatmap respects filtered violations and settings", "[ui][P3-011][ViolationHeatmap]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    window.set_heatmap_visible(true);
    window.set_heatmap_opacity(0.35);
    REQUIRE(window.heatmap_visible());
    REQUIRE(std::abs(window.heatmap_opacity() - 0.35) < 0.001);
    REQUIRE(window.heatmap_bucket_count() == 2);

    ViolationFilterState state;
    state.layer = "M1";
    window.set_violation_filter_state(state);
    REQUIRE(window.visible_violation_overlay_count() == 1);
    REQUIRE(window.heatmap_bucket_count() == 1);

    window.clear_violation_filters();
    REQUIRE(window.heatmap_bucket_count() == 2);
}
