# AEGIS-PERC — AI Agent Operating Rules

> Commercial-grade AI-assisted semiconductor verification platform.
> Qt6 / C++20 desktop application with CMake.

## 1. Scope Discipline

- **Do not modify unrelated files.** Each change must be justified by the active task.
- **Do not rewrite architecture unless the task explicitly asks for it.**
- **Keep each change PR-sized.** One task, one focused changeset.
- **Do not implement future sprint features early.** Stay within the active task boundary.

## 2. Architectural Boundaries

- **UI observes state and results; core services own analysis state.**
  - Widgets must not mutate analysis models directly.
  - Use explicit controller/presenter interfaces if the UI needs to drive behavior.
- **Parser logic must remain separate from rule execution.**
- **Rule execution must remain separate from ML inference.**
- **ML inference must be optional and replaceable.**
  - Core verification must run without any ML components loaded.
- **Graph engine must not depend on Qt widgets.**
  - The connectivity/analysis graph is a pure C++ library.
- **Visualization must not own business logic.**
  - Canvas and overlays render data produced by core services.
- **Python scripting API must call stable core services, not UI classes.**

## 3. Concurrency & Performance

- **All long-running operations must support cancellation or progress reporting.**
- **Large file parsing must avoid blocking the UI thread.**
  - Use worker threads, futures, or explicit thread pools.
- **Treat semiconductor data as potentially large and performance-sensitive.**
  - Streaming, lazy loading, and memory-mapped access are preferred over full in-memory duplication.

## 4. State Management

- **Do not introduce global state unless explicitly justified.**
  - Prefer dependency injection and explicit service registries.
  - If a singleton is required, wrap it behind an opaque interface and document why.

## 5. AI/ML Safety

- **Any AI/ML recommendation must include confidence and explainability hooks.**
- **No safety-critical or destructive automated fix should run without explicit user confirmation.**
- ML components are advisory only; verification truth remains deterministic.

## 6. Quality Gates

- **Add or update tests for behavior changes.**
  - Every functional change must have a corresponding test addition or update.
- **Prefer interfaces and clear module boundaries.**
  - Use pure abstract classes or concepts to define contracts between modules.
- **Preserve Qt6 / C++20 compatibility.**
  - Use `std::` facilities where possible; limit Qt to UI, I/O, and container interoperability.
- **Preserve cross-platform design, with Windows as a primary target.**
  - Avoid platform-specific APIs unless wrapped behind an abstraction.
  - Avoid hardcoded absolute paths.

## 7. Observability

- **Keep logging structured and useful for diagnostic bundles.**
  - Include timestamps, severity, module tags, and request/correlation IDs where applicable.
  - Logs must be exportable as a zipped diagnostic bundle without manual curation.

## 8. Task Completion

- **Every task must include acceptance criteria and test commands.**
  - Before marking a task done, verify the acceptance criteria are satisfied.
  - Run the listed test commands and confirm success.
  - Document any deviations with explicit risk acknowledgment.

## 9. Module Reference

| Module | Responsibility | Must Not Depend On |
|--------|---------------|--------------------|
| `core` | Service registry, configuration, diagnostics, base types | Qt Widgets |
| `parsing` | Layout/netlist/token import and intermediate representation | rule engine, ML, Qt Widgets |
| `graph` | Connectivity model, device/net/pin topology | Qt Widgets, ML |
| `rules` | Rule checking engine, violation detection | ML, Qt Widgets |
| `ml` | Feature extraction, inference, clustering, explainability | Qt Widgets, rule execution internals |
| `ui` | Windows, docks, canvas, property panels, visualization | parser internals, rule execution internals, ML training |
| `scripting` | Python bindings, batch CLI | Qt Widgets internals |
| `reporting` | HTML/PDF/Diagnostic export | Qt Widgets (HTML generation may use QTextDocument; prefer headless) |
| `storage` | Local DB, project/session persistence, enterprise upload | Qt Widgets |

## 10. Context and Memory Rules

The agent must assume every session starts with no memory of previous chats.

The repository is the source of truth.

Before starting work, the agent must read:
1. AGENTS.md
2. /orchestration/PROJECT_MEMORY.md
3. /orchestration/DECISIONS.md
4. /orchestration/HANDOFF.md
5. /orchestration/roadmap.yaml
6. /orchestration/tasks.yaml
7. Relevant run logs from /orchestration/runs/

The agent must update HANDOFF.md before stopping.

The agent must write a run log for every task attempted.

The agent must not rely on chat history for project facts.

Stable project facts go in PROJECT_MEMORY.md.

Architecture decisions go in DECISIONS.md.

Temporary task status goes in HANDOFF.md and tasks.yaml.

Successful autonomous tasks must be marked `review`, not `done`.

---
*Version: 1.0*
*Effective: Sprint 1*
