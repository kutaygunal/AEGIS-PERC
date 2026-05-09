#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/ui/main_window.hpp"
#include "aegis/ui/scene_adapter.hpp"

#include <QApplication>

#include <filesystem>
#include <fstream>

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

aegis::parsing::LayoutIR make_ir()
{
    using namespace aegis::parsing;
    LayoutIR ir;
    ir.design_name = "report_preview_test";
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

namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("Report preview summarizes scene and filtered violation counts", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.dock_widget_titles().contains("Report Preview"));
    REQUIRE(window.report_preview_summary_text().contains("Design: report_preview_test"));
    REQUIRE(window.report_preview_summary_text().contains("Layers: 2"));
    REQUIRE(window.report_preview_summary_text().contains("Violations: 2"));
    REQUIRE(window.report_preview_summary_text().contains("warning=1 error=1"));
}

TEST_CASE("Report preview updates for filtered violations and selected violation details", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    aegis::ui::ViolationFilterState state;
    state.layer = "M1";
    window.set_violation_filter_state(state);
    REQUIRE(window.report_preview_summary_text().contains("Violations: 1"));

    window.select_violation_row(0);
    REQUIRE(window.report_preview_summary_text().contains("Selected violation: V1"));
    REQUIRE(window.report_preview_summary_text().contains("message=Width issue"));
}

TEST_CASE("Report preview exposes snapshot hook status", "[ui][P3-012][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    REQUIRE(window.report_preview_snapshot_status_text() == "Canvas snapshot ready");
    REQUIRE(window.report_preview_summary_text().contains("Export actions: JSON and HTML available", Qt::CaseInsensitive));
}

TEST_CASE("Report preview exports JSON and HTML outputs with summary violations and diagnostics", "[ui][P8][P8-008][ReportPreview]")
{
    QtAppGuard guard;
    aegis::ui::MainWindow window;
    window.set_scene(aegis::ui::build_ui_scene(make_ir()));
    window.set_violations(make_violations());

    const fs::path root = fs::temp_directory_path() / "aegis_report_preview_export";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    window.set_report_export_path_picker_for_tests([&root](const QString& format) {
        return QString::fromStdString((root / (format == "html" ? "preview.html" : "preview.json")).string());
    });

    REQUIRE(window.report_preview_export_json_enabled());
    REQUIRE(window.report_preview_export_html_enabled());
    window.trigger_report_preview_export_json();
    window.trigger_report_preview_export_html();

    REQUIRE(fs::exists(root / "preview.json"));
    REQUIRE(fs::exists(root / "preview.html"));
    const auto json_text = read_file(root / "preview.json");
    const auto html_text = read_file(root / "preview.html");
    REQUIRE(json_text.find("\"violations\"") != std::string::npos);
    REQUIRE(json_text.find("\"runtime_diagnostics\"") != std::string::npos);
    REQUIRE(html_text.find("AEGIS-PERC Report Preview Export") != std::string::npos);
    REQUIRE(html_text.find("Width issue") != std::string::npos);
}
