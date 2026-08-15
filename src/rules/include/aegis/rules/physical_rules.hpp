#pragma once

#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/rule_pack.hpp"

namespace aegis::rules {

/**
 * PHYS-001 — Antenna Ratio Check (`type: antenna_ratio` in a rule pack)
 *
 * Flags nets whose accumulated connected metal area exceeds a configured
 * multiple of the total gate area connected to that net (the classic
 * plasma-induced-gate-damage "antenna ratio" check).
 *
 * Like `CurrentLimitRule` (em_current_limit), this is a built-in C++ rule
 * constructed directly from a `RulePackRuleDefinition` rather than wrapped
 * by `ConfiguredRuleAdapter`, because it needs a numeric threshold
 * parameter (`max_ratio`) plus optional field-name overrides.
 *
 * Data sourcing (see docs/design/physical_reliability_checks.md for the
 * full rationale):
 *   - Gate area: `DeviceNode::properties[gate_area_field]` if present,
 *     otherwise `properties[gate_width_field] * properties[gate_length_field]`
 *     for every device connected to the net via a `gate_terminal`-named
 *     `DeviceToPin` edge. `w`/`l` are populated by real SPICE `M`-element
 *     netlist ingestion (src/parsing/src/spice_parser.cpp) when present.
 *   - Accumulated metal area: `NetNode::properties[metal_area_field]`.
 *     Not populated by any existing importer today — see the design doc's
 *     "Non-goals" for why (no net-owning geometry aggregation exists yet).
 *     The rule reads whatever is present, exactly like `em_current_limit`
 *     reads `current_mA` without caring how it got there.
 *
 * Missing or unparsable numeric data (gate w/l, gate_area, metal_area) is
 * treated as non-triggering for that node/net, never as an error — matching
 * the existing convention in `CurrentLimitRule` and `DeclarativeConditionRule`.
 */
class AntennaRatioRule final : public IRule {
public:
    explicit AntennaRatioRule(RulePackRuleDefinition definition);

    std::string id() const override;
    std::string name() const override;
    std::string category() const override;
    std::string description() const override;
    std::vector<Violation> execute(const RuleContext& ctx) const override;

private:
    RulePackRuleDefinition m_definition;
};

} // namespace aegis::rules
