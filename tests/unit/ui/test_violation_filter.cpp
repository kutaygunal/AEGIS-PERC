#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_filter.hpp"
#include "aegis/ui/violation_filter_proxy_model.hpp"
#include "aegis/ui/violation_table_model.hpp"

#include <QApplication>
#include <QSettings>

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

struct SettingsCleanupGuard {
    SettingsCleanupGuard()
    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }

    ~SettingsCleanupGuard()
    {
        QSettings settings("AEGIS-PERC", "AEGIS-PERC");
        settings.remove("mainWindow");
    }
};

LayoutIR make_ir()
{
    LayoutIR ir;
    ir.design_name = "violation_filter_test";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    return ir;
}

ViolationCollection make_violations()
{
    std::vector<Violation> violations;

    Violation a{"R_WIDTH", Severity::Warning, "Width issue on input"};
    a.id = "V-001";
    a.location.layer = "M1";
    a.location.net_name = "n1";
    violations.push_back(a);

    Violation b{"R_SPACING", Severity::Error, "Spacing issue on output"};
    b.id = "V-002";
    b.location.layer = "M2";
    b.location.net_name = "n2";
    violations.push_back(b);

    Violation c{"R_DENSITY", Severity::Info, "Density note"};
    c.id = "V-003";
    c.location.layer = "M1";
    c.location.net_name = "bias";
    violations.push_back(c);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("Violation filter state is serializable", "[ui][P3-008][ViolationFilter]")
{
    ViolationFilterState state;
    state.severity = Severity::Error;
    state.rule_id = "R_SPACE";
    state.layer = "M2";
    state.net = "n1";
    state.search_text = "output";

    const auto roundtrip = ViolationFilterState::from_variant_map(state.to_variant_map());
    REQUIRE(roundtrip == state);
}

TEST_CASE("Violation proxy filters by severity rule layer net and search", "[ui][P3-008][ViolationFilter]")
{
    ViolationTableModel model;
    model.set_violations(make_violations());

    ViolationFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    ViolationFilterState state;
    state.severity = Severity::Error;
    state.rule_id = "spacing";
    state.layer = "m2";
    state.net = "n2";
    state.search_text = "output";
    proxy.set_filter_state(state);

    REQUIRE(proxy.rowCount() == 1);
    REQUIRE(proxy.violation_at_proxy_row(0)->id == "V-002");
}

TEST_CASE("MainWindow filters overlays and reports summary counts", "[ui][P3-008][ViolationFilter]")
{
    QtAppGuard guard;
    MainWindow window;
    window.set_scene(build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.violation_explorer_count() == 3);
    REQUIRE(window.visible_violation_overlay_count() == 3);
    REQUIRE(window.violation_filter_summary_text() == "3 / 3 violations");

    ViolationFilterState state;
    state.layer = "M1";
    window.set_violation_filter_state(state);

    REQUIRE(window.violation_explorer_count() == 2);
    REQUIRE(window.visible_violation_overlay_count() == 2);
    REQUIRE(window.violation_filter_summary_text() == "2 / 3 violations");

    state = {};
    state.search_text = "spacing";
    window.set_violation_filter_state(state);
    REQUIRE(window.violation_explorer_count() == 1);
    REQUIRE(window.current_violation_id() == "V-002");
    REQUIRE(window.visible_violation_overlay_count() == 1);

    window.clear_violation_filters();
    REQUIRE(window.violation_explorer_count() == 3);
    REQUIRE(window.visible_violation_overlay_count() == 3);
    REQUIRE(window.violation_filter_summary_text() == "3 / 3 violations");
}

TEST_CASE("MainWindow saves restores renames and deletes named filter presets", "[ui][P8][P8-013][ViolationFilter]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;

    MainWindow window;
    window.set_scene(build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    ViolationFilterState state;
    state.severity = Severity::Warning;
    state.layer = "M1";
    state.search_text = "Width";
    window.set_violation_filter_state(state);

    REQUIRE(window.save_violation_filter_preset("Warnings"));
    REQUIRE(window.violation_filter_preset_names() == QStringList{"Warnings"});

    window.clear_violation_filters();
    REQUIRE(window.apply_violation_filter_preset("Warnings"));
    const auto restored = window.violation_filter_state();
    REQUIRE(restored == state);

    REQUIRE(window.rename_violation_filter_preset("Warnings", "Critical Review"));
    REQUIRE(window.violation_filter_preset_names() == QStringList{"Critical Review"});
    REQUIRE(window.delete_violation_filter_preset("Critical Review"));
    REQUIRE(window.violation_filter_preset_names().isEmpty());
}
