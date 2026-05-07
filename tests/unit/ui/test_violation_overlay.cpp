#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_overlay.hpp"

#include <QApplication>
#include <QImage>
#include <QPainter>

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

LayoutIR make_overlay_ir()
{
    LayoutIR ir;
    ir.design_name = "overlay_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 50.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{60.0, 0.0, 30.0, 30.0}});
    ir.ports.push_back(Port{"IN", "INPUT", "net_in", std::string{"M1"}, Point{10.0, 10.0}});
    return ir;
}

ViolationCollection make_violations()
{
    ViolationCollection violations;

    Violation v1{"R1", Severity::Warning, "Point violation"};
    v1.id = "V1";
    v1.location.layer = "M1";
    v1.location.point = aegis::graph::Point{10.0, 10.0};
    violations.add(v1);

    Violation v2{"R2", Severity::Error, "Layer highlight"};
    v2.id = "V2";
    v2.location.layer = "M2";
    violations.add(v2);

    Violation v3{"R3", Severity::Fatal, "Unresolved entry"};
    v3.id = "V3";
    v3.location.net_name = "missing_net";
    violations.add(v3);

    return violations;
}

} // namespace

TEST_CASE("Violation severity colors are centralized", "[ui][P3-006][ViolationOverlay]")
{
    REQUIRE(violation_severity_color(Severity::Info).isValid());
    REQUIRE(violation_severity_color(Severity::Warning) != violation_severity_color(Severity::Error));
    REQUIRE(violation_severity_color(Severity::Fatal) != violation_severity_color(Severity::Info));
}

TEST_CASE("LayoutCanvas builds violation overlays and counts unresolved entries", "[ui][P3-006][ViolationOverlay]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_overlay_ir()));
    canvas.set_violations(make_violations());

    REQUIRE(canvas.violation_count() == 3);
    REQUIRE(canvas.violation_overlays().size() == 3);
    REQUIRE(canvas.unresolved_violation_count() == 1);
    REQUIRE(canvas.violation_overlays()[0].resolved);
    REQUIRE(canvas.violation_overlays()[0].point.has_value());
    REQUIRE(canvas.violation_overlays()[1].resolved);
    REQUIRE(canvas.violation_overlays()[1].bounds.has_value());
    REQUIRE_FALSE(canvas.violation_overlays()[2].resolved);
}

TEST_CASE("Violation overlays can be toggled without changing violation data", "[ui][P3-006][ViolationOverlay]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_overlay_ir()));
    canvas.set_violations(make_violations());

    REQUIRE(canvas.violation_overlays_visible());
    const auto before_count = canvas.violation_count();
    canvas.set_violation_overlays_visible(false);
    REQUIRE_FALSE(canvas.violation_overlays_visible());
    REQUIRE(canvas.violation_count() == before_count);
    canvas.set_violation_overlays_visible(true);
    REQUIRE(canvas.violation_overlays_visible());
}

TEST_CASE("Violation overlays render without crashing across zoom levels", "[ui][P3-006][ViolationOverlay]")
{
    QtAppGuard guard;
    LayoutCanvas canvas;
    canvas.resize(600, 300);
    canvas.set_scene(build_ui_scene(make_overlay_ir()));
    canvas.set_violations(make_violations());

    canvas.zoom_by(2.0, QPointF(300.0, 150.0));

    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    canvas.render(&painter);
    painter.end();

    REQUIRE(image.size() == QSize(600, 300));
}

TEST_CASE("MainWindow exposes unresolved violation count from canvas", "[ui][P3-006][ViolationOverlay]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_overlay_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.unresolved_violation_count() == 1);
}
