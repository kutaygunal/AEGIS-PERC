#include <catch2/catch_test_macros.hpp>
#include "aegis/core/service_registry.hpp"

// ---------------------------------------------------------------------------
// Helper types at file scope (avoid local-class static member issues)
// ---------------------------------------------------------------------------
namespace {

struct IAnalytics : aegis::core::IService {
    virtual int count() const = 0;
    std::string name() const override { return "Analytics"; }
};

struct AnalyticsService : IAnalytics {
    int count() const override { return 42; }
};

struct IEngine : aegis::core::IService {
    virtual std::string engine_name() const = 0;
    std::string name() const override { return "Engine"; }
};

struct GraphEngine : IEngine {
    std::string engine_name() const override { return "Graph"; }
};

struct RuleEngine : IEngine {
    std::string engine_name() const override { return "Rule"; }
};

struct ILoopA : aegis::core::IService {
    std::string name() const override { return "LoopA"; }
};

struct LoopAImpl : ILoopA {};

struct ILoopB : aegis::core::IService {
    std::string name() const override { return "LoopB"; }
};

struct CounterService : aegis::core::IService {
    static int s_destructions;
    std::string name() const override { return "Counter"; }
    ~CounterService() override { ++s_destructions; }
};

int CounterService::s_destructions = 0;

struct IAlpha : aegis::core::IService {
    std::string name() const override { return "Alpha"; }
};

struct IBeta : aegis::core::IService {
    std::string name() const override { return "Beta"; }
};

struct AlphaImpl : IAlpha {};
struct BetaImpl : IBeta {};

} // anonymous namespace

// ---------------------------------------------------------------------------
// P1-001 baseline tests
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// P1-002 tests: type-safe registration, resolution, cleanup
// ---------------------------------------------------------------------------
TEST_CASE("ServiceRegistry type-safe registration and resolution", "[core][p1-002][ServiceRegistry]")
{
    aegis::core::ServiceRegistry registry;
    REQUIRE(registry.has<IAnalytics>() == false);

    registry.register_service<IAnalytics>(std::make_unique<AnalyticsService>());

    REQUIRE(registry.has<IAnalytics>() == true);
    REQUIRE(registry.service_count() == 1);

    auto* svc = registry.resolve<IAnalytics>();
    REQUIRE(svc != nullptr);
    REQUIRE(svc->count() == 42);

    auto opt = registry.try_resolve<IAnalytics>();
    REQUIRE(opt.has_value());
    REQUIRE((*opt)->count() == 42);
}

TEST_CASE("ServiceRegistry named resolution", "[core][p1-002][ServiceRegistry]")
{
    aegis::core::ServiceRegistry registry;
    registry.register_service<IEngine>(std::make_unique<GraphEngine>(), "graph");
    registry.register_service<IEngine>(std::make_unique<RuleEngine>(), "rule");

    REQUIRE(registry.has<IEngine>("graph") == true);
    REQUIRE(registry.has<IEngine>("rule") == true);
    REQUIRE(registry.has<IEngine>("missing") == false);

    auto* graph = registry.resolve<IEngine>("graph");
    REQUIRE(graph != nullptr);
    REQUIRE(graph->engine_name() == "Graph");

    auto* rule = registry.resolve<IEngine>("rule");
    REQUIRE(rule != nullptr);
    REQUIRE(rule->engine_name() == "Rule");

    auto* missing = registry.resolve<IEngine>("missing");
    REQUIRE(missing == nullptr);

    auto opt = registry.try_resolve<IEngine>("missing");
    REQUIRE(opt.has_value() == false);
}

TEST_CASE("ServiceRegistry explicit cleanup", "[core][p1-002][ServiceRegistry]")
{
    int prev = CounterService::s_destructions;

    {
        aegis::core::ServiceRegistry registry;
        registry.register_service<CounterService>(std::make_unique<CounterService>());
        REQUIRE(registry.service_count() == 1);
        REQUIRE(CounterService::s_destructions == prev);

        registry.clear();
        REQUIRE(registry.service_count() == 0);
        REQUIRE(CounterService::s_destructions == prev + 1);
    }

    // Verify no extra destructions after clear + registry destruction
    REQUIRE(CounterService::s_destructions == prev + 1);
}

TEST_CASE("ServiceRegistry circular dependency detection", "[core][p1-002][ServiceRegistry]")
{
    aegis::core::ServiceRegistry reg;
    reg.register_factory<ILoopA>([&reg]() -> std::unique_ptr<aegis::core::IService> {
        // Circular: factory for A tries to resolve A during construction
        (void)reg.resolve<ILoopA>();
        return std::make_unique<LoopAImpl>();
    });

    REQUIRE_THROWS_AS(reg.resolve<ILoopA>(), aegis::core::CircularDependencyException);
}

TEST_CASE("ServiceRegistry multiple types coexist", "[core][p1-002][ServiceRegistry]")
{
    aegis::core::ServiceRegistry registry;
    registry.register_service<IAlpha>(std::make_unique<AlphaImpl>());
    registry.register_service<IBeta>(std::make_unique<BetaImpl>());

    REQUIRE(registry.has<IAlpha>() == true);
    REQUIRE(registry.has<IBeta>() == true);
    REQUIRE(registry.service_count() == 2);

    auto* alpha = registry.resolve<IAlpha>();
    auto* beta  = registry.resolve<IBeta>();
    REQUIRE(alpha != nullptr);
    REQUIRE(beta  != nullptr);
    REQUIRE(alpha != reinterpret_cast<IAlpha*>(beta)); // distinct objects
}
