#include <catch2/catch_test_macros.hpp>
#include "aegis/reporting/report_generator.hpp"

using namespace aegis::reporting;

TEST_CASE("ReportGenerator constructs and generates HTML", "[reporting][p1-010][fast][ModuleBoundary]")
{
    ReportGenerator gen;
    REQUIRE(gen.generate_html("test_report.html"));
}
