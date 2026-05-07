#include "aegis/rules/rule_engine.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace aegis::rules {

struct RuleEngine::Impl {
    std::vector<std::shared_ptr<IRule>> rules;
    std::unordered_map<std::string, std::size_t> index_by_id;
};

namespace {

using IdToIdx = std::unordered_map<std::string, std::size_t>;

std::vector<std::size_t> topological_sort(
    const std::vector<std::shared_ptr<IRule>>& rules,
    const std::vector<std::size_t>& active_indices,
    const IdToIdx& id_to_idx)
{
    if (active_indices.empty()) return {};

    std::unordered_map<std::size_t, std::size_t> local_id;
    for (std::size_t i = 0; i < active_indices.size(); ++i) {
        local_id[active_indices[i]] = i;
    }

    const std::size_t N = active_indices.size();
    std::vector<std::vector<std::size_t>> adj(N);
    std::vector<std::size_t> in_degree(N, 0);

    for (std::size_t local_v = 0; local_v < N; ++local_v) {
        const std::size_t global_v = active_indices[local_v];
        const auto& deps = rules[global_v]->dependencies();
        for (const auto& dep_id : deps) {
            auto it = id_to_idx.find(dep_id);
            if (it == id_to_idx.end()) {
                throw std::invalid_argument(
                    "Rule '" + rules[global_v]->id() +
                    "' declares unknown dependency '" + dep_id + "'");
            }
            auto it_local = local_id.find(it->second);
            if (it_local == local_id.end()) {
                continue;
            }
            std::size_t local_u = it_local->second;
            adj[local_u].push_back(local_v);
            ++in_degree[local_v];
        }
    }

    std::queue<std::size_t> q;
    for (std::size_t i = 0; i < N; ++i) {
        if (in_degree[i] == 0) q.push(i);
    }

    std::vector<std::size_t> order;
    order.reserve(N);
    while (!q.empty()) {
        std::size_t u = q.front(); q.pop();
        order.push_back(active_indices[u]);
        for (std::size_t v : adj[u]) {
            if (--in_degree[v] == 0) q.push(v);
        }
    }

    if (order.size() != N) {
        throw std::runtime_error(
            "Rule dependency cycle detected in the active rule set");
    }
    return order;
}

Violation make_fatal_violation(const std::string& rule_id,
                                const std::string& message) {
    return Violation{rule_id, Severity::Fatal, message};
}

} // anonymous namespace

RuleEngine::RuleEngine() : m_impl(std::make_unique<Impl>()) {}
RuleEngine::~RuleEngine() = default;
RuleEngine::RuleEngine(RuleEngine&&) noexcept = default;
RuleEngine& RuleEngine::operator=(RuleEngine&&) noexcept = default;

void RuleEngine::register_rule(std::unique_ptr<IRule> rule) {
    if (!rule) return;
    const std::string& rid = rule->id();
    if (rid.empty()) {
        throw std::invalid_argument("Rule id cannot be empty");
    }
    if (m_impl->index_by_id.count(rid)) {
        throw std::invalid_argument(
            "Duplicate rule registration: '" + rid + "'");
    }
    std::size_t idx = m_impl->rules.size();
    m_impl->rules.push_back(std::move(rule));
    m_impl->index_by_id[rid] = idx;
}

void RuleEngine::unregister_rule(const std::string& rule_id) {
    auto it = m_impl->index_by_id.find(rule_id);
    if (it == m_impl->index_by_id.end()) return;

    std::size_t removed_idx = it->second;
    m_impl->rules.erase(
        m_impl->rules.begin() + static_cast<long>(removed_idx));
    m_impl->index_by_id.erase(it);

    for (auto& [id, idx] : m_impl->index_by_id) {
        if (idx > removed_idx) --idx;
    }
}

std::size_t RuleEngine::rule_count() const {
    return m_impl->rules.size();
}

bool RuleEngine::has_rule(const std::string& rule_id) const {
    return m_impl->index_by_id.count(rule_id) != 0;
}

std::vector<std::string> RuleEngine::rule_ids() const {
    std::vector<std::string> ids;
    ids.reserve(m_impl->rules.size());
    for (const auto& r : m_impl->rules) {
        ids.push_back(r->id());
    }
    return ids;
}

std::vector<std::string> RuleEngine::categories() const {
    std::unordered_set<std::string> seen;
    for (const auto& r : m_impl->rules) {
        seen.insert(r->category());
    }
    return std::vector<std::string>{seen.begin(), seen.end()};
}

static std::vector<Violation> run_sorted(
    const std::vector<std::shared_ptr<IRule>>& rules,
    const std::vector<std::size_t>& sorted_indices,
    const RuleContext& ctx)
{
    std::vector<Violation> all;
    for (std::size_t idx : sorted_indices) {
        const auto& rule = rules[idx];
        try {
            auto local = rule->execute(ctx);
            all.insert(all.end(), local.begin(), local.end());
        } catch (const std::exception& e) {
            all.push_back(make_fatal_violation(rule->id(), e.what()));
        } catch (...) {
            all.push_back(make_fatal_violation(
                rule->id(), "Rule threw an unknown exception"));
        }
    }
    return all;
}

std::vector<Violation> RuleEngine::run_all(const RuleContext& ctx) const {
    if (m_impl->rules.empty()) return {};

    std::vector<std::size_t> all_indices(m_impl->rules.size());
    std::iota(all_indices.begin(), all_indices.end(), 0);

    auto sorted = topological_sort(m_impl->rules, all_indices,
                                    m_impl->index_by_id);
    return run_sorted(m_impl->rules, sorted, ctx);
}

std::vector<Violation> RuleEngine::run_category(
    const std::string& category,
    const RuleContext& ctx) const
{
    std::vector<std::size_t> subset;
    for (std::size_t i = 0; i < m_impl->rules.size(); ++i) {
        if (m_impl->rules[i]->category() == category) {
            subset.push_back(i);
        }
    }
    if (subset.empty()) {
        throw std::invalid_argument(
            "Unknown or empty rule category: '" + category + "'");
    }

    auto sorted = topological_sort(m_impl->rules, subset,
                                   m_impl->index_by_id);
    return run_sorted(m_impl->rules, sorted, ctx);
}

std::vector<Violation> RuleEngine::run_one(
    const std::string& rule_id,
    const RuleContext& ctx) const
{
    auto it = m_impl->index_by_id.find(rule_id);
    if (it == m_impl->index_by_id.end()) {
        throw std::invalid_argument(
            "Unknown rule id: '" + rule_id + "'");
    }

    const auto& rule = m_impl->rules[it->second];
    try {
        return rule->execute(ctx);
    } catch (const std::exception& e) {
        return {make_fatal_violation(rule->id(), e.what())};
    } catch (...) {
        return {make_fatal_violation(
            rule->id(), "Rule threw an unknown exception")};
    }
}

} // namespace aegis::rules
