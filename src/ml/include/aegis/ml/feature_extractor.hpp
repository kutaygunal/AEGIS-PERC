#pragma once

#include <memory>
#include <vector>

namespace aegis::ml {

class FeatureExtractor {
public:
    FeatureExtractor();
    ~FeatureExtractor();

    FeatureExtractor(const FeatureExtractor&) = delete;
    FeatureExtractor& operator=(const FeatureExtractor&) = delete;
    FeatureExtractor(FeatureExtractor&&) noexcept;
    FeatureExtractor& operator=(FeatureExtractor&&) noexcept;

    std::vector<double> extract() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::ml
