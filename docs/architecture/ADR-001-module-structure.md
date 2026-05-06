# ADR-001: Module Structure and Dependency Boundaries

## Status
Accepted

## Context
AEGIS-PERC requires a maintainable, commercial-grade C++20 desktop codebase.
Subsystems must be independently testable, replaceable, and evolve on different
schedules (e.g., UI can change without recompiling the rule engine).

## Decision
Adopt a modular `src/` layout with one CMake static-library target per subsystem.

| Target | Responsibility | Must Not Depend On |
|--------|----------------|--------------------|
| `aegis_core` | Service registry, configuration, diagnostics, base types | Qt Widgets |
| `aegis_parsing` | Parser abstractions and intermediate representation | Qt Widgets, rule engine, ML |
| `aegis_graph` | Connectivity model, device/net/pin topology | Qt Widgets, ML |
| `aegis_rules` | Rule checking engine, violation detection | ML, Qt Widgets |
| `aegis_ml` | Feature extraction, inference, clustering, explainability | Qt Widgets, rule execution internals |
| `aegis_ui` | Windows, docks, canvas, property panels, visualization | Parser internals, rule execution internals, ML training |
| `aegis_scripting` | Python bindings, batch CLI | Qt Widgets internals |
| `aegis_reporting` | HTML/PDF/diagnostic export | Qt Widgets (headless preferred) |
| `aegis_storage` | Local DB, project/session persistence, enterprise upload | Qt Widgets |

## Consequences
- **Positive**
  - Core libraries compile and test without Qt.
  - ML is optional at link time; core verification runs standalone.
  - Plugin authors can link against `aegis_graph` + `aegis_rules` without pulling in Qt.
- **Neutral**
  - Increased CMake surface area; mitigated by consistent target naming.
- **Risk**
  - Discipline is required to prevent circular dependencies.
  - The `task_auditor` prompt enforces acyclic dependency checks.

## References
- `AGENTS.md`, Section 2 (Architectural Boundaries)
- `orchestration/tasks.yaml`, P1-001 acceptance criteria
