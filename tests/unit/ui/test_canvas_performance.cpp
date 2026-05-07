#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>

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

aegis::parsing::LayoutIR make_metrics_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "canvas_performance_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 120.0, 80.0}});
    ir.geometries.push_back(Geometry{"M1", Rectangle{150.0, 0.0, 30.0, 20.0}});
    return ir;
}

aegis::rules::ViolationCollection make_overlay_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;

    Violation a{"R1", Severity::Warning, "Width issue"};
    a.id = "V1";
    a.location.layer = "M1";
    violations.push_back(a);

    Violation b{"R2", Severity::Error, "Spacing issue"};
    b.id = "V2";
    b.location.point = aegis::graph::Point{15.0, 15.0};
    b.location.layer = "M1";
    violations.push_back(b);

    return aegis::rules::ViolationCollection{std::move(violations)};
}

QImage render_canvas(aegis::ui::LayoutCanvas& canvas)
{
    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    canvas.render(&painter);
    painter.end();
    return image;
}

aegis::ui::UiScene make_lod_scene()
{
    using namespace aegis::ui;
    UiScene scene;
    scene.design_name = "lod_scene";
    scene.layers.push_back(SceneLayer{"layer:M1", "M1", "metal", "#FF0000", 0, 0});
    scene.bounds = SceneBounds{0.0, 0.0, 1000.0, 1000.0, true};

    SceneItem visible_large;
    visible_large.id = "large";
    visible_large.kind = SceneItemKind::Geometry;
    visible_large.shape_kind = SceneShapeKind::Rectangle;
    visible_large.layer_name = "M1";
    visible_large.color = "#FF0000";
    visible_large.bounds = SceneBounds{0.0, 0.0, 100.0, 100.0, true};
    scene.items.push_back(visible_large);

    SceneItem visible_tiny = visible_large;
    visible_tiny.id = "tiny";
    visible_tiny.bounds = SceneBounds{200.0, 200.0, 200.1, 200.1, true};
    scene.items.push_back(visible_tiny);

    SceneItem offscreen = visible_large;
    offscreen.id = "offscreen";
    offscreen.bounds = SceneBounds{5000.0, 5000.0, 5200.0, 5200.0, true};
    scene.items.push_back(offscreen);

    return scene;
}

aegis::ui::UiScene make_stress_scene(std::size_t item_count)
{
    using namespace aegis::ui;
    UiScene scene;
    scene.design_name = "stress_scene";
    scene.layers.push_back(SceneLayer{"layer:M1", "M1", "metal", "#FF0000", 0, 0});
    scene.bounds = SceneBounds{0.0, 0.0, 10000.0, 10000.0, true};
    scene.items.reserve(item_count);

    const std::size_t columns = 1000;
    for (std::size_t i = 0; i < item_count; ++i) {
        const double x = static_cast<double>((i % columns) * 10);
        const double y = static_cast<double>((i / columns) * 10);
        SceneItem item;
        item.kind = SceneItemKind::Geometry;
        item.shape_kind = SceneShapeKind::Rectangle;
        item.layer_name = "M1";
        item.color = "#FF0000";
        item.bounds = SceneBounds{x, y, x + 4.0, y + 4.0, true};
        scene.items.push_back(std::move(item));
    }

    return scene;
}

} // namespace

TEST_CASE("CanvasPerformance records frame metrics and exposes developer status text", "[ui][P3-014][CanvasPerformance]")
{
    QtAppGuard guard;
    aegis::ui::LayoutCanvas canvas;
    canvas.resize(480, 320);
    canvas.set_scene(aegis::ui::build_ui_scene(make_metrics_ir()));
    canvas.set_violations(make_overlay_violations());
    canvas.set_performance_metrics_visible(true);

    render_canvas(canvas);

    const auto metrics = canvas.performance_metrics();
    REQUIRE(metrics.total_item_count == 2);
    REQUIRE(metrics.visible_item_count >= 1);
    REQUIRE(metrics.overlay_count >= 1);
    REQUIRE(metrics.frame_time_ms >= 0.0);
    REQUIRE(canvas.performance_metrics_text().contains("Visible"));
    REQUIRE(canvas.performance_metrics_text().contains("Overlays"));

    aegis::ui::MainWindow window;
    window.resize(640, 480);
    window.set_scene(aegis::ui::build_ui_scene(make_metrics_ir()));
    window.set_violations(make_overlay_violations());
    window.set_performance_metrics_visible(true);
    window.show();
    QApplication::processEvents();
    REQUIRE(window.performance_metrics_visible());
    REQUIRE(window.performance_metrics_text().contains("Visible"));
}

TEST_CASE("CanvasPerformance LOD cache skips offscreen items and simplifies tiny geometry", "[ui][P3-014][CanvasPerformance]")
{
    QtAppGuard guard;
    aegis::ui::LayoutCanvas canvas;
    canvas.resize(400, 300);
    canvas.set_scene(make_lod_scene());

    render_canvas(canvas);

    const auto metrics = canvas.performance_metrics();
    REQUIRE(metrics.total_item_count == 3);
    REQUIRE(canvas.lod_cache_item_count() == 2);
    REQUIRE(metrics.visible_item_count == 2);
    REQUIRE(metrics.simplified_item_count >= 1);
}

TEST_CASE("CanvasPerformance synthetic stress scene exercises large generated datasets", "[ui][P3-014][CanvasPerformance]")
{
    QtAppGuard guard;
    aegis::ui::LayoutCanvas canvas;
    canvas.resize(320, 240);
    canvas.set_scene(make_stress_scene(1000000));
    canvas.reset_view();

    render_canvas(canvas);

    const auto metrics = canvas.performance_metrics();
    REQUIRE(metrics.total_item_count == 1000000);
    REQUIRE(metrics.visible_item_count > 0);
    REQUIRE(metrics.visible_item_count <= metrics.total_item_count);
    REQUIRE(canvas.lod_cache_item_count() == metrics.visible_item_count);
}
