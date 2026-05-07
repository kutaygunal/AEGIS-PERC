#pragma once

#include "aegis/rules/rule_engine.hpp"

namespace aegis::rules {

/**
 * ELEC-001 — Floating Net Check
 *
 * Identifies nets that have no connected pins (neither device pins
 * nor external ports).  A floating net is declared but electrically
 * isolated.
 */
class FloatingNetRule : public IRule {
public:
    std::string id() const override;
    std::string name() const override;
    std::string category() const override;
    std::string description() const override;
    std::vector<Violation> execute(const RuleContext& ctx) const override;
};

/**
 * ELEC-002 — Open-Circuit Check
 *
 * Identifies device pins that are attached to a device but not
 * connected to any net.  Also verifies that known device types
 * (NMOS, PMOS, etc.) have their required terminal pins present.
 */
class OpenCircuitRule : public IRule {
public:
    std::string id() const override;
    std::string name() const override;
    std::string category() const override;
    std::string description() const override;
    std::vector<Violation> execute(const RuleContext& ctx) const override;
};

/**
 * ELEC-003 — Short-Circuit / Driver Contention Check
 *
 * Identifies nets that are driven by more than one output-capable
 * pin (OUTPUT or INOUT direction).  Multiple drivers on the same
 * net create a contention condition.
 */
class ShortCircuitRule : public IRule {
public:
    std::string id() const override;
    std::string name() const override;
    std::string category() const override;
    std::string description() const override;
    std::vector<Violation> execute(const RuleContext& ctx) const override;
};

/**
 * DOMAIN-001 — Power/Signal Domain Tagging Check
 *
 * Propagates domain tags seeded on nets and devices through the
 * connectivity graph and reports conflicts as violations.
 */
class DomainTaggingRule : public IRule {
public:
    std::string id() const override;
    std::string name() const override;
    std::string category() const override;
    std::string description() const override;
    std::vector<Violation> execute(const RuleContext& ctx) const override;
};

} // namespace aegis::rules
