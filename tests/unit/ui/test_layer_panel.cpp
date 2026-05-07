#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/ui/layer_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
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

LayoutIR make_layers_ir()
{
    LayoutIR ir;
    ir.design_name = "layers_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"VIA", "via", 1, ""});
    ir.layers.push_back(Layer{"M2", "metal", 2, "#0000FF"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    ir.geometries.push_back(Geometry{"VIA", Rectangle{20.0, 20.0, 5.0, 5.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{40.0, 40.0, 10.0, 10.0}});
    return ir;
}

} // namespace

TEST_CASE("LayoutCanvas tracks per-layer visibility without reloading scene", "[ui][P3-004][LayerPanel]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.set_scene(build_ui_scene(make_layers_ir()));

    REQUIRE(canvas.visible_layers().size() == 3);
    REQUIRE(canvas.layer_visibility("M1"));
    REQUIRE(canvas.layer_visibility("VIA"));

    canvas.set_layer_visibility("VIA", false);
    REQUIRE_FALSE(canvas.layer_visibility("VIA"));
    REQUIRE(canvas.scene_item_count() == 3);

    canvas.hide_all_layers();
    REQUIRE(canvas.visible_layers().empty());

    canvas.show_all_layers();
    REQUIRE(canvas.visible_layers().size() == 3);
}

TEST_CASE("MainWindow layer panel reflects scene layers in draw order", "[ui][P3-004][LayerPanel]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_layers_ir()));

    REQUIRE(window.layer_panel_count() == 3);
    const QStringList names = window.layer_panel_names();
    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "M1");
    REQUIRE(names[1] == "VIA");
    REQUIRE(names[2] == "M2");
}

TEST_CASE("MainWindow layer panel toggles and isolates layers", "[ui][P3-004][LayerPanel]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_layers_ir()));

    REQUIRE(window.is_layer_visible("M1"));
    REQUIRE(window.is_layer_visible("VIA"));
    REQUIRE(window.is_layer_visible("M2"));

    auto* panel = window.findChild<QWidget*>("LayersDock");
    REQUIRE(panel != nullptr);

    auto* layer_panel = window.findChild<aegis::ui::LayerPanel*>();
    REQUIRE(layer_panel != nullptr);

    layer_panel->set_layer_visible("VIA", false);
    REQUIRE_FALSE(window.is_layer_visible("VIA"));

    layer_panel->isolate_layer("M2");
    REQUIRE_FALSE(window.is_layer_visible("M1"));
    REQUIRE_FALSE(window.is_layer_visible("VIA"));
    REQUIRE(window.is_layer_visible("M2"));

    layer_panel->show_all_layers();
    REQUIRE(window.is_layer_visible("M1"));
    REQUIRE(window.is_layer_visible("VIA"));
    REQUIRE(window.is_layer_visible("M2"));
}
