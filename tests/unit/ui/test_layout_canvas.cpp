#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>

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

LayoutIR make_canvas_ir()
{
    LayoutIR ir;
    ir.design_name = "canvas_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"POLY", "gate", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 100.0, 50.0}});
    ir.geometries.push_back(Geometry{"POLY", Polygon{{{20.0, 20.0}, {80.0, 20.0}, {50.0, 80.0}}}});
    return ir;
}

} // namespace

TEST_CASE("LayoutCanvas accepts and stores a UI scene", "[ui][P3-002][LayoutCanvas]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;

    REQUIRE_FALSE(canvas.has_scene());
    canvas.set_scene(build_ui_scene(make_canvas_ir()));

    REQUIRE(canvas.has_scene());
    REQUIRE(canvas.scene_item_count() == 2);
    REQUIRE(canvas.scene().design_name == "canvas_test");
}

TEST_CASE("LayoutCanvas paints scene to an image without mutating scene data", "[ui][P3-002][LayoutCanvas]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(400, 300);
    canvas.set_scene(build_ui_scene(make_canvas_ir()));

    const auto before_count = canvas.scene_item_count();
    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    canvas.render(&painter);
    painter.end();

    REQUIRE(canvas.scene_item_count() == before_count);
    REQUIRE(image.size() == QSize(400, 300));
}

TEST_CASE("MainWindow installs LayoutCanvas as central widget", "[ui][P3-002][LayoutCanvas]")
{
    QtAppGuard guard;
    MainWindow window;

    REQUIRE(window.has_central_widget());
    REQUIRE(window.has_layout_canvas());
    REQUIRE(dynamic_cast<LayoutCanvas*>(window.centralWidget()) != nullptr);
}
