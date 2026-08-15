# Architecture Decisions

## DEC-001: Core modules must not depend on Qt widgets
Status: accepted
Reason:
Core logic must be unit-testable and usable from CLI, scripting, and future worker processes.

## DEC-002: ML inference must be optional
Status: accepted
Reason:
The product should support deterministic rule checking without requiring AI/ML runtime.

## DEC-003: Successful autonomous tasks go to review, not done
Status: accepted
Reason:
Human review is required before a task is considered complete.

## DEC-004: Parser and rule engine are separate modules
Status: accepted
Reason:
Parsing converts external data into internal representation. Rule engine analyzes that representation.

## DEC-005: Service registry lifetime must be explicit
Status: accepted
Reason:
Hidden global singletons make testing and distributed execution difficult. Prefer dependency injection and explicit service registries.

## DEC-006: Project memory lives in the repository
Status: accepted
Reason:
Coding-agent sessions are forgetful. The repository is the single source of truth for architecture, decisions, task state, and run history.

## DEC-007: Logging backend is spdlog
Status: accepted
Reason:
The task explicitly recommends a lightweight library. spdlog is header-optional, fast (millions of msgs/sec), provides rotating file sinks, color console sinks, and severity filtering out of the box. A thin `aegis::core::Logger` facade wraps it to enforce module tagging and output format without leaking spdlog into consumer headers.

## DEC-008: Rule programmability is being built incrementally, not as a rule DSL up front
Status: accepted
Reason:
A commercial gap analysis (2026-08-14) identified the lack of a programmable rule language (à la Calibre SVRF/TVF) as the largest gap relative to the project's own "PERC" namesake. Rather than design a full expression grammar speculatively, S2-001 adds a minimal declarative `type: condition` rule format (field/operator/value predicates, ANDed, over device/net/pin fields) on top of the existing RuleEngine/RulePack architecture. This validates that the rule engine generalizes past its four fixed checks before investing in a parser/grammar for a real DSL. See docs/design/declarative_rules.md for the schema and explicit non-goals (no OR/NOT, no cross-node predicates, no geometry predicates).

## DEC-009: [proposed] Beachhead segment and open-source/open-core stance
Status: proposed — awaiting human decision, not yet accepted
Reason:
A separate, product-lens gap analysis (2026-08-14, "AEGIS-PERC as a product, not a codebase," run alongside the DEC-008 commercial/engineering-lens analysis) found the codebase technically sound but flagged that several product/GTM questions are being decided by default rather than on purpose. Two of those are business-model calls this task (S3-004) is explicitly not authorized to make unilaterally; they are recorded here as a clearly-labeled recommendation for the repo owner to accept, reject, or amend.

**1. Beachhead segment.** The README currently describes "chip designers, verification leads, and methodology teams" — which is the entire addressable market for an ERC tool, not a first customer. The gap analysis's candidate, worth testing rather than assuming: **fabless startups and university/research groups running early-stage ERC without an EDA budget or a Calibre seat**, who would plausibly take a scriptable, CI-gated, self-hosted checker over nothing, given they have no incumbent tool to displace. This is a hypothesis based on the tool's current shape (self-hosted, no license server, CLI-first, MIT-licensed) — not validated against any actual prospective user. The README's "Built for" section (this task) now includes this framing, explicitly labeled as a candidate fit rather than a validated segment, with a pointer back to this entry.

**2. Open-source vs. open-core.** The project is MIT-licensed today (`LICENSE`), but the gap analysis's read is that this happened by default (MIT is a common placeholder choice) rather than as a deliberate business-model decision. Worth deciding on purpose:
   - **Stay fully open-source (MIT or similar)** — maximizes adoption friction removal for the beachhead segment above (no budget, no procurement process), at the cost of no direct monetization path from the software itself.
   - **Open-core** — keep the current signoff-workflow core (import, graph, rules, waivers, baseline/diff, CLI) open, and reserve some future capability (e.g. the "enterprise upload" responsibility already named as a placeholder in `AGENTS.md`'s `storage` module table, or a hosted/team-collaboration layer) as a commercial add-on. This preserves the "trust statement" now in the README (local-only execution, no cloud calls in the core loop) while creating a monetization path that doesn't compromise that promise for the open core.
   - **Fully proprietary** — contradicts the current MIT `LICENSE` and the project's own "commercial-grade" framing being about capability rather than distribution model; not obviously a good fit given the beachhead candidate above explicitly lacks procurement budget, but included for completeness.

Neither of these two questions is resolved by this entry — they require a human decision (the repo owner's actual goals: portfolio piece vs. eventual product vs. something else). This entry exists so the decision, once made, has a recorded rationale rather than being inferred later from an MIT `LICENSE` file nobody remembers choosing on purpose.

**Note on an unresolved tension this does *not* attempt to resolve:** the README's existing framing (H1 tagline, "Why This Project Matters" section, footer) describes AEGIS-PERC as "commercial-grade" / evolving into "a commercial-grade platform." That framing was left untouched by this task (S3-004) per its own scope — flagging it here rather than silently editing marketing copy to match a "solo, modest, portfolio-stage" framing that this same task *did* apply to the newly-added "Built for" bullets and trust statement. Whoever resolves DEC-009 should also decide whether "commercial-grade" is aspirational language that should stay, or should be softened for consistency with the rest of the README's now-more-modest framing. See `orchestration/runs/run_2026-08-14_s3-004-product-quick-wins.md` for the full note.
