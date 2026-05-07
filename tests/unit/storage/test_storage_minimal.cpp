#include <catch2/catch_test_macros.hpp>
#include "aegis/storage/storage_engine.hpp"

using namespace aegis::storage;

TEST_CASE("StorageEngine constructs and opens", "[storage][p1-010][fast][ModuleBoundary]")
{
    StorageEngine engine;
    REQUIRE(!engine.is_open());

    REQUIRE(engine.open("test.db"));
    REQUIRE(engine.is_open());
}
