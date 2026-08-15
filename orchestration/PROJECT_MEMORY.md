# Project Memory

## Project
AEGIS-PERC is a Qt6/C++20 commercial-grade AI-assisted semiconductor verification platform.

## Architecture Rules
- UI observes state; core owns state.
- Parser, graph, rule engine, ML, reporting, storage, and scripting must stay decoupled.
- Core modules must be testable without launching the UI.
- ML inference is optional and replaceable.
- Visualization must not own business logic.
- Python scripting must call core services, not UI classes.

## Current Architecture
- CMake-based modular structure.
- Qt6 desktop application.
- Core engine intended to be independent from widgets.
- Imported customer packages now build into a UI-agnostic `ImportedDesignSession` that links package metadata, parsed LEF/DEF/netlist data, connectivity graph state, rule-pack references, and optional power/current enrichments.
- Imported package commit now also materializes a 2D `UiScene` immediately from the session, including DEF-derived geometry plus synthesized placement rectangles for instances and fallback markers when LEF macro sizes are missing.
- Future modules: parser, graph, rule engine, ML, reporting, storage, scripting, plugins.

## Important Constraints
- Keep every change PR-sized.
- Do not implement future sprint features early.
- Mark successful autonomous work as `review`, not `done`.
- Only the user can mark tasks `done`.

## Rule Engine
- Rule packs (JSON/YAML) support four `type`s: `floating_net`, `power_domain_mismatch`, `em_current_limit` (all built-in, C++-backed), and `condition` (declarative — added S2-001).
- `type: condition` lets a rule pack define a new check (field/operator/value predicates over device/net/pin, ANDed) as pure data, without a new C++ `IRule` subclass. See docs/design/declarative_rules.md for schema, operators, and explicit non-goals (no OR/NOT, no cross-node predicates, no geometry predicates yet).
- The YAML rule-pack loader (`src/rules/src/rule_pack.cpp`) is a small hand-rolled indentation parser, not a general YAML implementation — it only supports the shapes rule packs need (flat `parameters:` map, nested `conditions:` list of flat maps).

## Commercial Value Focus
- The single biggest missing product loop is the **signoff workflow**:
  - Run checks → apply waivers (suppress known issues with audit trail) → diff vs baseline → gate regressions → export audit-ready reports.
- Sprint 1 task registry has been reset to prioritize waivers, baselines, and regression diff/gating as a cohesive headless + UI workflow.

## Known Risks
- Context-window loss can cause duplicated work.
- Later tasks in tasks.yaml may need refinement.
- Architecture drift must be prevented by reviewer/architect prompts.
