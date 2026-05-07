#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/selection_model.hpp"

#include <QApplication>
#include <QTest>

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

LayoutIR make_selection_ir()
{
    LayoutIR ir;
    ir.design_name = "selection_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{60.0, 0.0, 40.0, 20.0}});
    ir.ports.push_back(Port{"IN", "INPUT", "net_in", std::string{"M1"}, Point{10.0, 10.0}});
    ir.annotations.push_back(Annotation{"note", "marker", std::string{"M2"}, Point{80.0, 10.0}});
    return ir;
}

} // namespace

TEST_CASE("Canvas hit testing finds shape, port, and annotation by scene id", "[ui][P3-005][CanvasSelection]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    SelectionModel selection;
    canvas.set_selection_model(&selection);
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_selection_ir()));

    const UiScene& scene = canvas.scene();
    REQUIRE(scene.find_item_by_id("geometry:0") != nullptr);
    REQUIRE(scene.find_item_by_id("port:0") != nullptr);
    REQUIRE(scene.find_item_by_id("annotation:0") != nullptr);

    const QPointF shape_widget = canvas.scene_to_widget(QPointF(20.0, 10.0));
    const QPointF port_widget = canvas.scene_to_widget(QPointF(10.0, 10.0));
    const QPointF annotation_widget = canvas.scene_to_widget(QPointF(80.0, 10.0));

    REQUIRE(canvas.hit_test_widget_position(shape_widget)->id == "geometry:0");
    REQUIRE(canvas.hit_test_widget_position(port_widget)->id == "port:0");
    REQUIRE(canvas.hit_test_widget_position(annotation_widget)->id == "annotation:0");
}

TEST_CASE("Canvas click selection supports single and multi-select", "[ui][P3-005][CanvasSelection]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    SelectionModel selection;
    canvas.set_selection_model(&selection);
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_selection_ir()));
    canvas.show();

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                      canvas.scene_to_widget(QPointF(20.0, 10.0)).toPoint());
    REQUIRE(selection.selected_ids().size() == 1);
    REQUIRE(selection.selected_ids().front() == "geometry:0");

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ControlModifier,
                      canvas.scene_to_widget(QPointF(80.0, 10.0)).toPoint());
    REQUIRE(selection.selected_ids().size() == 2);

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                      canvas.scene_to_widget(QPointF(10.0, 10.0)).toPoint());
    REQUIRE(selection.selected_ids().size() == 1);
    REQUIRE(selection.selected_ids().front() == "port:0");
}

TEST_CASE("MainWindow properties panel updates through selection model", "[ui][P3-005][CanvasSelection]")
{
    QtAppGuard guard;
    MainWindow window;
    window.resize(900, 500);
    window.set_scene(build_ui_scene(make_selection_ir()));
    window.show();

    auto* canvas = window.findChild<LayoutCanvas*>();
    REQUIRE(canvas != nullptr);

    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      canvas->scene_to_widget(QPointF(10.0, 10.0)).toPoint());

    REQUIRE(window.selected_item_count() == 1);
    const QString summary = window.properties_summary_text();
    REQUIRE(summary.contains("Selected: 1"));
    REQUIRE(summary.contains("port:0"));
    REQUIRE(summary.contains("name=IN"));
}
