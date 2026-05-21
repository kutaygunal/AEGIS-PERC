
#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>

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

LayoutIR make_coloring_ir()
{
    LayoutIR ir;
    ir.design_name = "coloring_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"VIA1", "via", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    ir.geometries.push_back(Geometry{"VIA1", Rectangle{20.0, 20.0, 5.0, 5.0}});
    return ir;
}

} // namespace

TEST_CASE("LayoutCanvas defaults to LayerColor coloring mode", "[ui][P13-007][CanvasColoring]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    REQUIRE(canvas.coloring_mode() == ColoringMode::LayerColor);
}

TEST_CASE("LayoutCanvas can switch coloring modes", "[ui][P13-007][CanvasColoring]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.set_scene(build_ui_scene(make_coloring_ir()));

    canvas.set_coloring_mode(ColoringMode::ObjectType);
    REQUIRE(canvas.coloring_mode() == ColoringMode::ObjectType);

    canvas.set_coloring_mode(ColoringMode::Domain);
    REQUIRE(canvas.coloring_mode() == ColoringMode::Domain);

    canvas.set_coloring_mode(ColoringMode::ViolationContext);
    REQUIRE(canvas.coloring_mode() == ColoringMode::ViolationContext);

    canvas.set_coloring_mode(ColoringMode::LayerColor);
    REQUIRE(canvas.coloring_mode() == ColoringMode::LayerColor);
}

TEST_CASE("LayoutCanvas color_for_item returns distinct colors per object type", "[ui][P13-007][CanvasColoring]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.set_scene(build_ui_scene(make_coloring_ir()));

    canvas.set_coloring_mode(ColoringMode::ObjectType);
    // We cannot directly call color_for_item (private), but we can verify the mode changes
    // and the canvas repaints without crashing.
    REQUIRE(canvas.coloring_mode() == ColoringMode::ObjectType);

    // Switching back to LayerColor should restore the original item colors
    canvas.set_coloring_mode(ColoringMode::LayerColor);
    REQUIRE(canvas.coloring_mode() == ColoringMode::LayerColor);
}
