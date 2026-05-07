#pragma once

#include <optional>
#include <string>
#include <vector>

namespace aegis::rules {

/**
 * Minimal violation record — will be expanded by P2-008.
 *
 * Contains enough fields for the rule engine to collect and
 * forward to consumers (reporting, UI, ML).
 */
struct Violation {
    std::string rule_id;
    std::string severity;              // "error", "warning", "info"
    std::string message;
    std::optional<std::string> location; // net / pin / layer name
};

} // namespace aegis::rules
