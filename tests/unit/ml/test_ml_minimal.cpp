#include <catch2/catch_test_macros.hpp>
#include "aegis/ml/feature_extractor.hpp"

using namespace aegis::ml;

TEST_CASE("FeatureExtractor constructs and returns empty features", "[ml][p1-010][fast][ModuleBoundary]")
{
    FeatureExtractor extractor;
    auto features = extractor.extract();
    REQUIRE(features.empty());
}
