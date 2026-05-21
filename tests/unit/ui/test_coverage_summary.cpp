
#include <catch2/catch_test_macros.hpp>

#include "aegis/parsing/layout_ir.hpp"
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

} // namespace

TEST_CASE("MainWindow has no coverage summary before import", "[ui][P13-008][CoverageSummary]")
{
    QtAppGuard guard;
    MainWindow window;
    REQUIRE_FALSE(window.has_coverage_summary());
    REQUIRE(window.coverage_summary_text().isEmpty());
    REQUIRE(window.coverage_skipped_count() == 0);
    REQUIRE(window.coverage_fallback_count() == 0);
    REQUIRE_FALSE(window.coverage_has_issues());
}

TEST_CASE("MainWindow workspace summary mentions coverage after sample load", "[ui][P13-008][CoverageSummary]")
{
    QtAppGuard guard;
    MainWindow window;
    window.load_bundled_sample("inverter");
    REQUIRE_FALSE(window.has_coverage_summary());
    REQUIRE(window.workspace_summary_text().contains("sample mode"));
}
