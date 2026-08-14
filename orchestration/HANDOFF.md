# Handoff

## Last Updated
2026-08-14

## Current Status
S2-001 (declarative `type: condition` rule pack entries) is complete and under review. This opens a new Sprint 2 ("Rule Engine Programmability"), sourced from an external commercial gap analysis, run alongside the still-open Sprint 1 tasks below.

## Last Completed Task
S2-001 — Rules: declarative `type: condition` rule pack entries (field/operator/value checks)

## Current Task
No implementation task in progress.

## Ready Tasks
- `S1-009` — UI: violation explorer supports 'Active vs Waived' filter pill (Sprint 1, still open)
- Sprint 2 follow-ons are **not yet scheduled as tasks**: physical/reliability check types (blocked on GDSII/OASIS ingestion, itself unscheduled), OR/NOT combinators for `condition` (only if the AND-only version proves too limited in practice).

## Blocked Tasks
- None recorded.

## Important Notes
- README source-code synchronization (2026-05-23) is still valid for everything except the new S2-001 capability below (README has not been re-synced for it yet — next task should add a short "declarative rule packs" bullet to the Verification Core feature list).
- S2-001 implementation:
  - New rule pack `type: condition`: data-defined checks (field/operator/value predicates, ANDed) over device/net/pin nodes — no new C++ class or recompile needed. Full schema in `docs/design/declarative_rules.md`.
  - `RulePackRuleDefinition` gained `target` / `conditions` / `message`; `RuleCondition` is the new predicate struct.
  - The hand-rolled YAML rule-pack loader was rewritten from a single-pass state machine to a line-indexed parser to support the nested `conditions:` list; existing flat-`parameters:` behavior is unchanged and covered by the pre-existing `test_rule_pack.cpp` suite (still 100% passing).
  - This is explicitly *not* a rule DSL — no OR/NOT, no cross-node predicates, no geometry predicates. See DEC-008 in `DECISIONS.md` for why an incremental step was chosen over designing a grammar up front.
  - Tests: `ctest --test-dir build/windows-release -C Release -L rules` passes (38/38, 10 new).
  - Full suite: 496/498 passing; the 2 failures (`WorkspacePersistence` tests #156, #158) are pre-existing Qt-side segfaults unrelated to this change (see run log for reasoning — this task touched no UI code).
- Separately, a commercial gap analysis (2026-08-14, "AEGIS-PERC vs. commercial signoff tools") was produced and is the source for the new Sprint 2 phase in `roadmap.yaml`. It also identified GDSII/OASIS ingestion, physical/reliability checks, RVE-grade reporting, and distributed execution as larger, unscheduled gaps — none of those are started.

## Next Recommended Action
Two independent options, pick based on priority:
1. Continue Sprint 1: `S1-009` — add an "Active/Waived" toggle in the violation explorer filter model.
2. Continue Sprint 2: sync the README feature list for S2-001, then decide whether the next rule-engine step is OR/NOT combinators (still no geometry data, so still topology-only) or to defer rule-engine work until GDSII/OASIS ingestion unlocks physical checks.
