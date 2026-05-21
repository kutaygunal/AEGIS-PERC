#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/rules/violation.hpp"

#include <QApplication>

#include <cmath>
#include <memory>

using namespace aegis::parsing;
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

LayoutIR make_viewport_ir()
{
    LayoutIR ir;
    ir.design_name = "viewport_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 200.0, 100.0}});
    return ir;
}

LayoutIR make_multi_item_ir()
{
    LayoutIR ir;
    ir.design_name = "multi_item";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 50.0, 50.0}});
    ir.geometries.push_back(Geometry{"M1", Rectangle{100.0, 0.0, 150.0, 50.0}});
    return ir;
}

bool close_enough(double a, double b, double eps = 0.001)
{
    return std::abs(a - b) <= eps;
}

} // namespace

TEST_CASE("Canvas viewport state capture and restore", "[ui][P13-006][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    const ViewportState state = canvas.current_viewport_state();
    REQUIRE(state.zoom_level > 0.0);
    REQUIRE(close_enough(state.view_center.x(), 100.0));
    REQUIRE(close_enough(state.view_center.y(), 50.0));

    canvas.zoom_by(2.0, QPointF(250.0, 150.0));
    const ViewportState zoomed = canvas.current_viewport_state();
    REQUIRE(zoomed.zoom_level > state.zoom_level);

    canvas.restore_viewport_state(state);
    REQUIRE(close_enough(canvas.zoom_level(), state.zoom_level));
    REQUIRE(close_enough(canvas.view_center().x(), state.view_center.x()));
    REQUIRE(close_enough(canvas.view_center().y(), state.view_center.y()));
}

TEST_CASE("Canvas viewport history back and forward", "[ui][P13-006][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    REQUIRE_FALSE(canvas.can_viewport_back());
    REQUIRE_FALSE(canvas.can_viewport_forward());

    const ViewportState initial = canvas.current_viewport_state();

    canvas.center_on_scene_point(QPointF(50.0, 25.0));

    canvas.center_on_scene_point(QPointF(150.0, 75.0));

    REQUIRE(canvas.can_viewport_back());
    REQUIRE_FALSE(canvas.can_viewport_forward());

    canvas.viewport_back();
    const double back1_x = canvas.view_center().x();
    REQUIRE(close_enough(back1_x, 50.0));
    // Forward unavailable because live state is not stored in push-before history
    REQUIRE_FALSE(canvas.can_viewport_forward());

    canvas.viewport_back();
    REQUIRE(close_enough(canvas.view_center().x(), initial.view_center.x()));
    REQUIRE_FALSE(canvas.can_viewport_back());

    // Forward within stored history works
    canvas.viewport_forward();
    REQUIRE(close_enough(canvas.view_center().x(), 50.0));
}

TEST_CASE("Canvas jump_to_coordinate sets zoom and center", "[ui][P13-006][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    canvas.jump_to_coordinate(QPointF(42.0, 24.0), 2.5);
    REQUIRE(close_enough(canvas.view_center().x(), 42.0));
    REQUIRE(close_enough(canvas.view_center().y(), 24.0));
    REQUIRE(close_enough(canvas.zoom_level(), 2.5));
}

TEST_CASE("Canvas zoom_to_selection fits selected items", "[ui][P13-006][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_multi_item_ir()));
    SelectionModel model;
    canvas.set_selection_model(&model);

    canvas.reset_view();
    const double initial_zoom = canvas.zoom_level();

    model.select_only("geometry:0");
    canvas.zoom_to_selection();
    REQUIRE(canvas.zoom_level() > initial_zoom);
}

TEST_CASE("Canvas zoom_to_violations fits violation overlays", "[ui][P13-006][CanvasViewport]")
{
    QtAppGuard guard;
    using namespace aegis::rules;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_multi_item_ir()));

    Violation v{"R1", Severity::Warning, "Width issue"};
    v.id = "V1";
    v.location.layer = "M1";
    v.location.point = aegis::graph::Point{25.0, 25.0};
    canvas.set_violations(ViolationCollection{std::vector<Violation>{v}});

    canvas.reset_view();
    const double initial_zoom = canvas.zoom_level();

    canvas.zoom_to_violations();
    REQUIRE(canvas.zoom_level() > initial_zoom);
}
