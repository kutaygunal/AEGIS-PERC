#pragma once

#include "aegis/graph/connectivity_graph.hpp"
#include "aegis/graph/data_model.hpp"
#include "aegis/rules/violation.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aegis::rules {

// ---------------------------------------------------------------------------
// Context passed to every rule — immutable / borrowed refs only
// ---------------------------------------------------------------------------

struct RuleContext {
    const aegis::graph::ConnectivityGraph& graph;
    aegis::graph::PropertyMap               parameters;
    std::string                              design_name;
};

// ---------------------------------------------------------------------------
// Abstract rule interface
// ---------------------------------------------------------------------------

class IRule {
public:
    virtual ~IRule() = default;

    /// Machine-friendly unique identifier (e.g. "PERC-001").
    virtual std::string id() const = 0;

    /// Human-readable name (e.g. "Open-circuit check").
    virtual std::string name() const = 0;

    /// Category for grouping (e.g. "electrical", "physical", "drc").
    virtual std::string category() const = 0;

    /// Optional longer description.
    virtual std::string description() const { return ""; }

    /// List of rule IDs that must produce results before this rule runs.
    /// The engine topologically sorts rules based on these.
    virtual std::vector<std::string> dependencies() const { return {}; }

    /**
     * Execute the rule against the given context.
     *
     * Rules must not mutate the graph.  Any thrown exception is caught
     * by the engine and converted into an error Violation.
     *
     * @return A list of violations produced by this rule (may be empty).
     */
    virtual std::vector<Violation> execute(const RuleContext& ctx) const = 0;
};

// ---------------------------------------------------------------------------
// Rule engine — registration, dependency resolution, dispatch
// ---------------------------------------------------------------------------

/**
 * The rule engine maintains a registry of rules and runs them in
 * dependency order.
 *
 * Usage:
 *   RuleEngine engine;
 *   engine.register_rule(std::make_unique<MyRule>());
 *   auto violations = engine.run_all(ctx);
 *
 * Thread safety: register_rule / run_* are NOT thread-safe.
 * Callers must synchronise externally.
 */
class RuleEngine {
public:
    RuleEngine();
    ~RuleEngine();

    RuleEngine(const RuleEngine&) = delete;
    RuleEngine& operator=(const RuleEngine&) = delete;
    RuleEngine(RuleEngine&&) noexcept;
    RuleEngine& operator=(RuleEngine&&) noexcept;

    /**
     * Register a rule.  Ownership is transferred.
     *
     * @throws std::invalid_argument if a rule with the same id() is
     *         already registered.
     */
    void register_rule(std::unique_ptr<IRule> rule);

    /// Unregister a rule by id.  No-op if not found.
    void unregister_rule(const std::string& rule_id);

    /// Query
    [[nodiscard]] std::size_t rule_count() const;
    [[nodiscard]] bool has_rule(const std::string& rule_id) const;
    [[nodiscard]] std::vector<std::string> rule_ids() const;
    [[nodiscard]] std::vector<std::string> categories() const;

    // -----------------------------------------------------------------------
    // Execution
    // -----------------------------------------------------------------------

    /**
     * Run all registered rules in dependency order.
     *
     * Rules are topologically sorted.  If a cycle exists among declared
     * dependencies, an exception is thrown before any rule executes.
     *
     * Individual rule exceptions are caught and converted into Violation
     * records (severity "fatal").
     *
     * @return All collected violations across all rules.
     */
    std::vector<Violation> run_all(const RuleContext& ctx) const;

    /**
     * Run only rules belonging to the given category.
     *
     * @throws std::invalid_argument if the category is unknown.
     */
    std::vector<Violation> run_category(
        const std::string& category,
        const RuleContext& ctx) const;

    /**
     * Run a single rule by id.
     *
     * @throws std::invalid_argument if the rule id is unknown.
     * @return Violations from that rule only.
     */
    std::vector<Violation> run_one(
        const std::string& rule_id,
        const RuleContext& ctx) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace aegis::rules
