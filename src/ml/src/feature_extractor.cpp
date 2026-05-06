#include "aegis/ml/feature_extractor.hpp"

namespace aegis::ml {

struct FeatureExtractor::Impl {};

FeatureExtractor::FeatureExtractor() : m_impl(std::make_unique<Impl>()) {}
FeatureExtractor::~FeatureExtractor() = default;
FeatureExtractor::FeatureExtractor(FeatureExtractor&&) noexcept = default;
FeatureExtractor& FeatureExtractor::operator=(FeatureExtractor&&) noexcept = default;

std::vector<double> FeatureExtractor::extract() const {
    return {};
}

} // namespace aegis::ml
