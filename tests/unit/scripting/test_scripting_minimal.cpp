#include <catch2/catch_test_macros.hpp>
#include "aegis/scripting/python_api.hpp"

using namespace aegis::scripting;

TEST_CASE("PythonApi constructs and initializes", "[scripting][p1-010][fast][ModuleBoundary]")
{
    PythonApi api;
    REQUIRE(!api.is_initialized());

    REQUIRE(api.initialize());
    REQUIRE(api.is_initialized());
}
