#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>
#include <QSettings>

#include <memory>

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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "report_preview_ui";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.layers.push_back(Layer{"M2", "metal", 1, "#00FF00"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 40.0, 20.0}});
    ir.geometries.push_back(Geometry{"M2", Rectangle{100.0, 40.0, 30.0, 30.0}});
    ir.ports.push_back(Port{"A", "INPUT", "n1", std::string{"M1"}, Point{10.0, 10.0}});
    return ir;
}

aegis::rules::ViolationCollection make_violations()
{
    using namespace aegis::rules;
    std::vector<Violation> violations;

    Violation a{"R1", Severity::Warning, "Width issue"};
    a.id = "V1";
    a.location.layer = "M1";
    violations.push_back(a);

    Violation b{"R2", Severity::Error, "Spacing issue"};
    b.id = "V2";
    b.location.layer = "M2";
    violations.push_back(b);

    return ViolationCollection{std::move(violations)};
}

} // namespace

TEST_CASE("ReportPreviewUi transitions from empty placeholder state to live preview state", "[ui][P7][P7-009][ReportPreviewUi]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;

    REQUIRE(window.report_preview_summary_text().contains("Load a design to preview report content."));
    REQUIRE_FALSE(window.report_preview_snapshot_status_text().trimmed().isEmpty());
    REQUIRE_FALSE(window.report_preview_refresh_enabled());

    window.trigger_report_preview_refresh();
    REQUIRE(window.report_preview_last_action_status_text() == "Load a design before refreshing the report preview");

    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    REQUIRE_FALSE(window.report_preview_summary_text().contains("Load a design to preview report content."));
    REQUIRE(window.report_preview_summary_text().contains("Design: report_preview_ui"));
    REQUIRE(window.report_preview_snapshot_status_text().contains("Canvas snapshot ready"));
    REQUIRE(window.report_preview_refresh_enabled());
    REQUIRE(window.report_preview_copy_summary_enabled());
    REQUIRE(window.report_preview_copy_snapshot_enabled());
}

TEST_CASE("ReportPreviewUi updates on filter selection and violation changes and supports copy actions", "[ui][P7][P7-009][ReportPreviewUi]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.report_preview_summary_text().contains("Violations: 2"));
    REQUIRE(window.report_preview_summary_text().contains("Selection: 0 item(s)"));

    window.select_scene_item_by_id("port:A");
    REQUIRE(window.report_preview_summary_text().contains("Selection: 1 item(s)"));
    REQUIRE(window.report_preview_snapshot_status_text().contains("Canvas snapshot ready"));

    aegis::ui::ViolationFilterState state;
    state.layer = "M1";
    window.set_violation_filter_state(state);
    REQUIRE(window.report_preview_summary_text().contains("Violations: 1"));

    window.select_violation_row(0);
    REQUIRE(window.report_preview_summary_text().contains("Selected violation: V1"));

    window.trigger_report_preview_copy_summary();
    REQUIRE(window.report_preview_last_action_status_text() == "Copied report summary");

    window.trigger_report_preview_copy_snapshot();
    REQUIRE(window.report_preview_last_action_status_text() == "Copied report snapshot");
}

TEST_CASE("ReportPreviewUi refresh action remains functional after repeated preview updates", "[ui][P7][P7-009][ReportPreviewUi]")
{
    QtAppGuard guard;
    SettingsCleanupGuard settings_guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    window.select_scene_item_by_id("port:A");
    window.trigger_report_preview_refresh();
    REQUIRE(window.report_preview_last_action_status_text() == "Requested snapshot refresh for report_preview_ui");
    REQUIRE(window.report_preview_snapshot_status_text().contains("Canvas snapshot ready"));

    window.clear_violation_filters();
    window.select_violation_row(1);
    REQUIRE(window.report_preview_summary_text().contains("Selected violation: V2"));

    window.trigger_report_preview_refresh();
    REQUIRE(window.report_preview_last_action_status_text() == "Requested snapshot refresh for report_preview_ui");
}
