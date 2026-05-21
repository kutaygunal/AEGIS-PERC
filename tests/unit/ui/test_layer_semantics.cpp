
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

LayoutIR make_classified_ir()
{
    LayoutIR ir;
    ir.design_name = "classified_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"VIA1", "via", 1, "#00FF00"});
    ir.layers.push_back(Layer{"PIN", "pin", 2, "#0000FF"});
    ir.layers.push_back(Layer{"OBS", "obs", 3, "#FFFFFF"});
    ir.layers.push_back(Layer{"POLY", "poly", 4, "#FF00FF"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    ir.geometries.push_back(Geometry{"VIA1", Rectangle{20.0, 20.0, 5.0, 5.0}});
    ir.ports.push_back(Port{"PIN", "INPUT", "net1", "PIN", Point{30.0, 30.0}});
    return ir;
}

} // namespace

TEST_CASE("SceneAdapter classifies layers by name and purpose", "[ui][P13-007][LayerSemantics]")
{
    const auto scene = build_ui_scene(make_classified_ir());
    REQUIRE(scene.layers.size() == 5);

    REQUIRE(scene.layers[0].category == SceneLayerCategory::Routing);   // M1
    REQUIRE(scene.layers[1].category == SceneLayerCategory::CutVia);    // VIA1
    REQUIRE(scene.layers[2].category == SceneLayerCategory::Pin);       // PIN
    REQUIRE(scene.layers[3].category == SceneLayerCategory::Blockage); // OBS
    REQUIRE(scene.layers[4].category == SceneLayerCategory::Technology); // POLY
}

TEST_CASE("SceneAdapter assigns layer_category to scene items", "[ui][P13-007][LayerSemantics]")
{
    const auto scene = build_ui_scene(make_classified_ir());
    REQUIRE(!scene.items.empty());

    const auto* m1_item = scene.find_item_by_id("geometry:0");
    REQUIRE(m1_item != nullptr);
    REQUIRE(m1_item->layer_category == SceneLayerCategory::Routing);

    const auto* via_item = scene.find_item_by_id("geometry:1");
    REQUIRE(via_item != nullptr);
    REQUIRE(via_item->layer_category == SceneLayerCategory::CutVia);

    const auto* port_item = scene.find_item_by_id("port:0");
    REQUIRE(port_item != nullptr);
    REQUIRE(port_item->layer_category == SceneLayerCategory::Pin);
}

TEST_CASE("LayerPanel applies visibility presets correctly", "[ui][P13-007][LayerPanel]")
{
    QtAppGuard guard;
    LayerPanel panel;
    panel.set_layers(build_ui_scene(make_classified_ir()).layers);

    REQUIRE(panel.layer_count() == 5);

    panel.apply_visibility_preset(VisibilityPreset::RoutingOnly);
    REQUIRE(panel.layer_visible("M1"));
    REQUIRE(panel.layer_visible("VIA1"));
    REQUIRE_FALSE(panel.layer_visible("PIN"));
    REQUIRE_FALSE(panel.layer_visible("OBS"));
    REQUIRE_FALSE(panel.layer_visible("POLY"));
    REQUIRE(panel.current_visibility_preset() == VisibilityPreset::RoutingOnly);

    panel.apply_visibility_preset(VisibilityPreset::PinsAndPorts);
    REQUIRE_FALSE(panel.layer_visible("M1"));
    REQUIRE_FALSE(panel.layer_visible("VIA1"));
    REQUIRE(panel.layer_visible("PIN"));
    REQUIRE_FALSE(panel.layer_visible("OBS"));
    REQUIRE_FALSE(panel.layer_visible("POLY"));

    panel.apply_visibility_preset(VisibilityPreset::All);
    REQUIRE(panel.layer_visible("M1"));
    REQUIRE(panel.layer_visible("VIA1"));
    REQUIRE(panel.layer_visible("PIN"));
    REQUIRE(panel.layer_visible("OBS"));
    REQUIRE(panel.layer_visible("POLY"));
}

TEST_CASE("LayerPanel set_category_visible toggles categories", "[ui][P13-007][LayerPanel]")
{
    QtAppGuard guard;
    LayerPanel panel;
    panel.set_layers(build_ui_scene(make_classified_ir()).layers);

    panel.set_category_visible(SceneLayerCategory::Routing, false);
    REQUIRE_FALSE(panel.layer_visible("M1"));
    REQUIRE(panel.layer_visible("VIA1"));

    panel.set_category_visible(SceneLayerCategory::CutVia, false);
    REQUIRE_FALSE(panel.layer_visible("VIA1"));

    panel.set_category_visible(SceneLayerCategory::Routing, true);
    REQUIRE(panel.layer_visible("M1"));
}

TEST_CASE("MainWindow forwards visibility presets and coloring mode", "[ui][P13-007][MainWindow]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_classified_ir()));

    REQUIRE(window.visibility_preset_names().size() == 6);
    REQUIRE(window.apply_visibility_preset("Routing Only"));
    REQUIRE_FALSE(window.is_layer_visible("POLY"));
    REQUIRE(window.is_layer_visible("M1"));

    window.set_coloring_mode("Object Type");
    REQUIRE(window.coloring_mode_name() == "Object Type");

    window.set_coloring_mode("Layer Color");
    REQUIRE(window.coloring_mode_name() == "Layer Color");
}
