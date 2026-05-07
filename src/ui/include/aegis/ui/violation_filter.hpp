#pragma once

#include "aegis/rules/violation.hpp"

#include <QVariantMap>

#include <optional>
#include <string>

namespace aegis::ui {

struct ViolationFilterState {
    std::optional<aegis::rules::Severity> severity;
    std::string rule_id;
    std::string layer;
    std::string net;
    std::string search_text;

    [[nodiscard]] QVariantMap to_variant_map() const;
    [[nodiscard]] static ViolationFilterState from_variant_map(const QVariantMap& map);

    bool operator==(const ViolationFilterState&) const noexcept = default;
};

} // namespace aegis::ui
