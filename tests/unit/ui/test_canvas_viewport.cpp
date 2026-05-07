#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QSignalSpy>

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

bool close_enough(double a, double b, double eps = 0.001)
{
    return std::abs(a - b) <= eps;
}

} // namespace

TEST_CASE("Canvas viewport fit, reset, and transforms are stable", "[ui][P3-003][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    REQUIRE(canvas.zoom_level() > 0.0);
    REQUIRE(close_enough(canvas.view_center().x(), 100.0));
    REQUIRE(close_enough(canvas.view_center().y(), 50.0));

    const QPointF scene_center(100.0, 50.0);
    const QPointF widget_center = canvas.scene_to_widget(scene_center);
    REQUIRE(close_enough(widget_center.x(), 250.0));
    REQUIRE(close_enough(widget_center.y(), 150.0));

    const QPointF round_trip = canvas.widget_to_scene(widget_center);
    REQUIRE(close_enough(round_trip.x(), scene_center.x()));
    REQUIRE(close_enough(round_trip.y(), scene_center.y()));

    canvas.reset_view();
    REQUIRE(close_enough(canvas.zoom_level(), 1.0));
    REQUIRE(close_enough(canvas.view_center().x(), 100.0));
    REQUIRE(close_enough(canvas.view_center().y(), 50.0));
}

TEST_CASE("Canvas viewport zoom preserves widget anchor", "[ui][P3-003][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    const QPointF anchor(125.0, 80.0);
    const QPointF scene_before = canvas.widget_to_scene(anchor);
    const double zoom_before = canvas.zoom_level();

    canvas.zoom_by(2.0, anchor);

    const QPointF scene_after = canvas.widget_to_scene(anchor);
    REQUIRE(canvas.zoom_level() > zoom_before);
    REQUIRE(close_enough(scene_after.x(), scene_before.x()));
    REQUIRE(close_enough(scene_after.y(), scene_before.y()));
}

TEST_CASE("Canvas viewport pan moves view center in scene units", "[ui][P3-003][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));
    canvas.reset_view();

    canvas.pan_by(QPointF(10.0, -20.0));

    REQUIRE(close_enough(canvas.view_center().x(), 90.0));
    REQUIRE(close_enough(canvas.view_center().y(), 30.0));
}

TEST_CASE("Canvas grid toggles and viewport emits status data", "[ui][P3-003][CanvasViewport]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(500, 300);
    canvas.set_scene(build_ui_scene(make_viewport_ir()));

    REQUIRE(canvas.grid_visible());
    canvas.toggle_grid();
    REQUIRE_FALSE(canvas.grid_visible());
    canvas.set_grid_visible(true);
    REQUIRE(canvas.grid_visible());

    QSignalSpy spy(&canvas, &LayoutCanvas::viewport_changed);
    canvas.zoom_by(1.25, QPointF(250.0, 150.0));
    REQUIRE(spy.count() == 1);
}
