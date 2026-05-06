#include <catch2/catch_test_macros.hpp>
#include "aegis/core/service_registry.hpp"

TEST_CASE("ServiceRegistry instantiation", "[core][p1-001]")
{
    aegis::core::ServiceRegistry registry;
    REQUIRE(registry.service_count() == 0);
}

TEST_CASE("ServiceRegistry register and query", "[core][p1-001]")
{
    struct StubService : aegis::core::IService {
        std::string name() const override { return "StubService"; }
    };

    aegis::core::ServiceRegistry registry;
    registry.register_service(std::make_unique<StubService>());

    REQUIRE(registry.service_count() == 1);
    REQUIRE(registry.has_service("StubService") == true);
    REQUIRE(registry.has_service("MissingService") == false);
}
