# AEGIS-PERC

**Electrical Rule Verification & Signoff Platform**

> A commercial-grade desktop platform for semiconductor electrical reliability verification and signoff workflow automation, built on production-quality C++20 / Qt 6 with a cohesive run → waive → diff → gate loop.

---

## Product Vision

AEGIS-PERC is being developed as a commercial-grade EDA verification platform for semiconductor electrical reliability analysis. The focus is on production-quality architecture, deterministic verification workflows, and a real signoff loop (run checks → apply waivers → diff vs baseline → gate regressions → export audit-ready reports) for production design environments.

---

## Elevator Pitch

AEGIS-PERC analyzes semiconductor layout and connectivity data, detects electrical reliability violations (floating nets, electromigration risks, power domain mismatches), and presents results through an interactive Qt 6 desktop visualization environment with a headless CLI workflow for CI integration.

The system is built for:
- **Chip designers** who need rapid feedback on rule violations
- **Verification leads** who manage regression runs against a committed baseline
- **Methodology teams** who automate checks via the headless CLI workflow

---

## Implemented Features

### 1. Layout & Netlist Import Engine
- **Formats:** Native LEF (5.x grammar, see `docs/design/lef-coverage.md`), DEF (line-oriented subset with physical normalization), Verilog gate-level netlists (`.v`/`.sv`), hardened SPICE/CDL netlists (`.sp`/`.spi`/`.cdl`), JSON intermediate representation (`LayoutIR`), power-intent CSV, current/activity CSV
- **Architecture:** Streaming parser abstraction (`IParser` + cancellation tokens), versioned `LayoutIR` document, source-provenance tracking through to a unified `ImportedDesignSession`
- **Project ingestion:** Explicit file selection, project-folder scan with heuristic role detection, versioned manifest (JSON/YAML) based import, drag-and-drop folder ingestion in the desktop app
- **Power & current enrichment:** Dedicated CSV parsers (`PowerIntentParser`, `CurrentActivityParser`) with diagnostics and application onto the connectivity graph

### 2. Connectivity Graph Engine
- Typed node model: `Device`, `Net`, `Pin` variants with stable dense IDs
- **Adjacency-list storage** (outgoing + incoming edge lists) — pure C++20 STL, no external graph library
- BFS / DFS visitors, topological order over the Device→Pin→Net subgraph
- Power/signal domain tag propagation with conflict detection
- Batch construction from `LayoutIR` with explicit unresolved-net reporting
- No Qt or ML dependencies

### 3. Verification Core (Rules Engine)
- Registerable rule engine with topological dependency ordering
- Built-in electrical rules:
  - `ELEC-001` — Floating net detection
  - `ELEC-002` — Open-circuit / missing-pin detection
  - `ELEC-003` — Short-circuit / driver contention detection
  - `DOMAIN-001` — Power/signal domain conflict detection
- Typed violation model with `Severity`, structured `ViolationLocation`, `PropertyMap` metadata
- `ViolationCollection` with filter / group / paginate / sort / JSON round-trip
- **Stable violation identity keys** (schema-versioned) for waivers and regression diffs
- **Waivers:** parse CSV / JSON / YAML; apply in place with audit trail (`metadata.waived`, `waiver_comment`, `waiver_owner`, `waiver_expires_on`)
- **Customer-configurable rule packs** loaded from JSON / YAML with typed parameters

### 4. Signoff Workflow (Sprint 1)
- **Baseline IO:** versioned `BaselineFile` JSON format (`baseline_schema_version`); deterministic record ordering; safe schema mismatch handling
- **Regression diff engine:** deterministic sets for `new` / `removed` / `unchanged` / `changed_severity`; aggregation by rule and by severity
- **Signoff exports:** `export_baseline_json(...)` and `export_regression_diff_json(...)` with run metadata (timestamp, tool version, project, rule-pack id) and stable field ordering
- **Headless CLI:** `aegis-perc-cli` with `import` / `run` / `report` / `baseline` / `diff` subcommands; exit codes distinguish `SuccessNoViolations` (0), `SuccessWithViolations` (3), `SuccessWithRegressions` (5), and error conditions
- **CI gating:** "no new errors" mode for `diff`; deterministic output suitable for build logs

### 5. Reporting & Reporting Outputs
- Stable JSON exports for baselines and regression diffs (signoff-ready)
- HTML report export through `ReportGenerator` (basic shell — full HTML report content is produced via the CLI workflow's `report` subcommand)
- JSON report export with active vs waived counts

### 6. Storage & Project Packaging
- **`ProjectPackage`** — versioned manifest model with metadata, source-artifact declarations, normalized view, diagnostics, and validation status
- **`ImportPreflightValidator`** — heuristic file-role detection, project-folder scan, structured validation diagnostics with severity
- **`ImportedDesignSession`** — unified in-memory model linking LEF / DEF / netlist / rule-pack / power / current data with stable object IDs and source provenance
- **Session cache** — JSON-based (`SessionCache`) keyed on artifact size+mtime for fast reopens
- **StorageEngine** — placeholder interface (no SQLite / PostgreSQL / Arrow / Parquet backend wired in yet)

### 7. Orchestration
- **`LocalJobPipeline`** — observable staged execution: `parse → normalize → graph_build → rule_execution → report_export`; per-stage progress snapshots, cancellation, JSON+HTML output
- Job monitor / history dock in the desktop app with re-openable result reports
- Imported-design realization runs asynchronously with stage progress (`ImportedDesignRealization`)

### 8. Desktop Visualization & Workspace
- **Qt 6** desktop shell with dockable workspace and per-panel QSettings persistence
- **QPainter 2D canvas** (CPU rendering, not OpenGL/Vulkan) with zoom / pan / grid / selection / LOD / viewport history / viewport presets / jump-to-coordinate / performance metrics
- Layer visibility panel with category-aware layer semantics and visibility presets (`All`, `RoutingOnly`, `MacrosOnly`, `PinsAndPorts`, `PowerFocused`, `ViolationReview`)
- Violation overlays (severity-colored) on the canvas, with heatmap overlay (visible / opacity) and heatmap empty-state text
- Connectivity tracing by stable name, from current selection, or from current violation; trace clear / focus
- Graph explorer with search, level-of-detail (LOD) slider, cross-probing
- **Hierarchy browser** for imported design objects (search, status, cross-probing)
- Violation explorer with filter (severity / rule / layer / net / search), context actions (trace, center, copy id, copy details, navigate related, copy selected, export selected), filter presets, and "active vs waived" toggle
- Properties panel, activity log, drag-and-drop project import, bundled sample browser, onboarding / post-import guidance
- **Workspace summary** surfacing active vs waived counts and (optionally) baseline / regression diff summaries
- **Coverage summary** (rendered / skipped / fallback counts and session diagnostics)
- Diagnostics panel with severity filter and "show related violations" navigation
- Recent projects + reopen-last-session
- HTML / JSON export from the report preview

### 9. Core Platform
- **Service registry** with type-safe named services, factory-based lazy registration, and a clean `IService` interface
- **Structured rotating logger** (file + console sinks) via spdlog with per-call module tagging
- **Configuration** with JSON load, environment variable overrides (`AEGIS_*`), schema validation, change callbacks, and `ConfigMigrator` (v0 → v1 → v2 chain)
- **Diagnostic bundle export** — timestamped zip with logs, redacted config snapshot, build info, and OS info
- **ML module** — `FeatureExtractor` placeholder (no inference wired in yet)
- **Python API** — `PythonApi` placeholder (no pybind11 binding compiled in yet)

### 10. Build, Packaging & Quality
- CMake 3.25+ with `cmake-presets` (Windows MSVC, Linux/macOS Ninja)
- Configurable sanitizer presets (ASan + UBSan on Linux/macOS, ASan on Windows) and coverage preset
- Optional `clang-tidy` integration
- **CPack packaging** (NSIS / ZIP on Windows, DragNDrop / TGZ on macOS, TGZ / DEB on Linux)
- Catch2 v3 with per-module `catch_discover_tests` auto-registration
- GitHub Actions CI workflow (`ci-windows-release`, `ci-linux-release`)

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    DESKTOP FRONTEND (Qt 6)                       │
│  ┌──────────────┐ ┌──────────────┐ ┌────────────────────────┐  │
│  │ Layout       │ │ Graph        │ │ Hierarchy / Properties  │  │
│  │ Canvas       │ │ Explorer     │ │ / Violation / Report    │  │
│  │ (QPainter)   │ │              │ │ Preview / Activity Log  │  │
│  └──────────────┘ └──────────────┘ └────────────────────────┘  │
│  Dockable workspace · QSettings persistence · drag-and-drop     │
└────────────────────┬────────────────────────────────────────────┘
                     │ C++20 core libraries
┌────────────────────▼────────────────────────────────────────────┐
│                      CORE ENGINE                                 │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐                  │
│  │ Parsing    │ │ Graph      │ │ Rules      │                  │
│  │ (LEF/DEF/  │ │ Engine     │ │ Engine     │                  │
│  │  Verilog/  │ │ (adjacency │ │ (IRule +   │                  │
│  │  SPICE +   │ │  list,     │ │  Waivers + │                  │
│  │  IR)       │ │  DFS/BFS/  │ │  Rule Pack │                  │
│  │            │ │  Topo)     │ │  Loader)   │                  │
│  └────────────┘ └────────────┘ └────────────┘                  │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐                  │
│  │ Reporting  │ │ Storage    │ │ Orchestr.  │                  │
│  │ (Baseline, │ │ (Project   │ │ (Local Job │                  │
│  │  Diff,     │ │  Package,  │ │  Pipeline, │                  │
│  │  Signoff   │ │  Session,  │ │  Realiz.)  │                  │
│  │  Exports)  │ │  Cache)    │ │            │                  │
│  └────────────┘ └────────────┘ └────────────┘                  │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐                  │
│  │ Core       │ │ Scripting  │ │ ML         │                  │
│  │ (Registry, │ │ (CLI       │ │ (Feature   │                  │
│  │  Logger,   │ │  Workflow, │ │  Extract   │                  │
│  │  Config)   │ │  PythonApi │ │  stub)     │                  │
│  └────────────┘ └────────────┘ └────────────┘                  │
│                                                                  │
│  • Event-driven architecture    • Async task scheduling         │
│  • Service registry / DI        • Thread-pool-style staging     │
│  • Versioned manifest + preflight  • Cancellation tokens        │
└────────────────────┬────────────────────────────────────────────┘
                     │ stdio / file I/O
┌────────────────────▼────────────────────────────────────────────┐
│                      CLI WORKFLOW (aegis-perc-cli)                │
│   import · run · report · baseline · diff                       │
│   Exit codes: 0 no violations, 3 with violations,                │
│               5 with regressions, 4 execution error,             │
│               2 import failure, 64 usage error                   │
└─────────────────────────────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────────┐
│                      DATA / ARTIFACTS                            │
│  Versioned project manifests (JSON/YAML)                         │
│  Source artifacts (LEF/DEF/Verilog/SPICE/CDL/CSV/YAML)            │
│  JSON-based session cache (.aegis/session_cache.json)             │
│  JSON / HTML reports and baseline / diff exports                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Technology Stack

| Layer | Technology |
|-------|------------|
| **Frontend** | Qt 6 (Core, Widgets, Test), C++20, QPainter 2D canvas, QSettings |
| **Core Engine** | C++20, stdlib STL, thread-pool-style staged execution |
| **Graph Engine** | Custom adjacency-list engine (pure C++20 STL) |
| **Logging / IO** | spdlog (rotating file + console), nlohmann_json, miniz / zip (bundle export) |
| **Reporting** | nlohmann_json (signoff exports), basic HTML shell |
| **Scripting** | C++ headless CLI workflow (aegis-perc-cli); Python API placeholder |
| **Build** | CMake 3.25+ with `cmake-presets`, FetchContent for first-party deps, CPack |
| **Testing** | Catch2 v3 (per-module `catch_discover_tests`), Qt Test |
| **CI** | GitHub Actions (`ci-windows-release`, `ci-linux-release`) |

> The project intentionally has no Boost, no OpenGL/Vulkan, no gRPC/ZeroMQ/Redis, no SQLite/PostgreSQL/Arrow/Parquet, no pybind11, and no Docker / vcpkg / Conan dependency at this time. All first-party dependencies are pulled via CMake `FetchContent`.

---

## Repository Structure

```
AEGIS-PERC/
├── CMakeLists.txt              # Root build + aegis_warnings + CPack
├── CMakePresets.json           # Windows/Linux presets + CI workflows
├── .clang-tidy                 # Static analysis rules
├── .github/workflows/ci.yml    # GitHub Actions CI
├── README.md
├── LICENSE                     # MIT
├── AGENTS.md                   # AI agent operating rules
├── docs/
│   ├── BUILD.md                # Build + warnings-as-errors policy
│   ├── architecture/           # ADR-001 module structure
│   ├── design/                 # import-workflow, lef-coverage,
│   │                           # signoff_workflow, distributed (draft)
│   ├── api/
│   └── user_guide/
├── src/
│   ├── core/                   # ServiceRegistry, Logger, Config,
│   │                           # ConfigMigrator, DiagnosticBundle
│   ├── graph/                  # ConnectivityGraph (adjacency list),
│   │                           # DataModel, DomainTagging,
│   │                           # PowerIntent / CurrentActivity apply
│   ├── ml/                     # FeatureExtractor placeholder
│   ├── orchestration/          # LocalJobPipeline + staged execution
│   ├── parsing/                # Parser interface, LayoutIR, native
│   │                           # LEF / DEF / Verilog / SPICE-CDL parsers
│   │                           # + PowerIntent / CurrentActivity CSV
│   ├── reporting/              # Baseline IO, regression diff,
│   │                           # signoff exports, HTML shell
│   ├── rules/                  # RuleEngine, electrical rules,
│   │                           # RulePack, Violation model, Waivers
│   ├── scripting/              # PythonApi (stub) + headless CLI
│   │                           # workflow (aegis-perc-cli)
│   ├── storage/                # ProjectPackage, import validation,
│   │                           # ImportedDesignSession, SessionCache,
│   │                           # StorageEngine (stub)
│   └── ui/                     # Dockable Qt workspace, canvas, layer
│                               # panel, filters, tracing, graph
│                               # explorer, hierarchy browser, heatmap,
│                               # report preview, properties, activity
│                               # log, import wizard
├── app/
│   ├── main.cpp                # aegis-perc entry (QApplication)
│   └── cli_main.cpp            # aegis-perc-cli entry
├── tests/
│   ├── unit/                   # Catch2 v3 tests per module
│   └── CMakeLists.txt          # catch_discover_tests auto-registration
├── scripts/
│   └── validate_samples.py     # Synthetic data schema validator
├── data/
│   └── sample_designs/         # inverter, nand2, ring_oscillator
├── models/
│   └── pretrained/             # reserved (no models shipped yet)
└── orchestration/
    ├── tasks.yaml              # Task registry
    ├── roadmap.yaml            # Sprint 1 signoff-workflow roadmap
    ├── DECISIONS.md
    ├── PROJECT_MEMORY.md
    ├── HANDOFF.md
    └── runs/                   # Development run logs
```

---

## Development Setup

### Prerequisites
- C++20 compiler (MSVC 2022 / GCC 12 / Clang 15+)
- CMake 3.25+
- Qt 6.5+ (Core, Widgets; Test for the unit suite)
- Python 3.10+ (only required for `scripts/validate_samples.py`)

### Quick Build

The project provides CMake presets for common configurations:

```bash
cd C:/Users/kutay/Desktop/Projects/AEGIS-PERC

# Configure and build
cmake --preset windows-release
cmake --build build/windows-release --config Release

# Run tests (auto-discovers Catch2 tests per module boundary)
ctest --preset ci-test

# Or use the CI workflow preset for full configure + build + test
cmake --workflow --preset ci-windows-release
```

### Linux / macOS
```bash
cmake --preset linux-release
cmake --build build/linux-release --config Release
ctest --preset linux-test
```

### Headless CLI quick reference

```bash
# 1) Import a project (explicit file args)
aegis-perc-cli import \
    --project FalconCPU \
    --lef tech.lef --lef cells.lef \
    --def top.def \
    --netlist top.v \
    --rules aegis_rules.yaml \
    --power power_domains.csv \
    --current current_report.csv \
    --output import.json

# 2) Run checks
aegis-perc-cli run --manifest aegis-project.yaml --output run.json

# 3) Create a baseline snapshot
aegis-perc-cli baseline --manifest aegis-project.yaml --output baseline.json

# 4) Diff current run against the baseline (CI gate)
aegis-perc-cli diff --baseline baseline.json --manifest aegis-project.yaml --output diff.json
# exit code 5 = new regressions detected
```

### Validate sample designs
```bash
python scripts/validate_samples.py
```

### Packaging
```bash
cpack -C Release   # NSIS / ZIP (Windows), DragNDrop / TGZ (macOS), TGZ / DEB (Linux)
```

---

## Why This Project Matters

| Capability | Demonstrated Here |
|------------|-------------------|
| Modern C++ architecture at scale | Custom adjacency-list graph engine, memory-aware parsers, staged job pipeline |
| Commercial desktop software | Qt 6, dockable UI, persistent workspace, headless CI workflow |
| Real format ingestion | Native LEF 5.x, DEF, Verilog, SPICE/CDL with provenance, diagnostics, and stress-tested parsers |
| Signoff workflow | Identity-keyed violations, waivers with audit trail, baseline IO, regression diff, CI gating |
| Connectivity analysis | Typed device/net/pin graph, BFS/DFS/topo traversal, domain-tag propagation |
| Engineering workflows | EDA-style rule checking, rule packs, waivers, baselines, report generation |
| Data pipelines | Versioned project manifests, JSON intermediate, session cache, deterministic exports |
| Interactive visualization | QPainter 2D multi-layer canvas, layer visibility, heatmaps, viewport history / presets |
| Qt expertise | Complex desktop shell, custom widgets, dock panels, QSettings persistence |
| Python scripting | Headless C++ CLI workflow (aegis-perc-cli); Python API placeholder only |
| Plugin architecture | Rule engine extensible via `IRule` interface; dynamic plugin SDK is not implemented |
| Distributed processing | `LocalJobPipeline` only; multi-node / host-worker design documented as draft in `docs/design/distributed.md` |
| System design maturity | Service registry, configuration with migration, diagnostic bundle, clean module boundaries |

---

## Roadmap

### Implemented (current source)

- **Foundation** — Project architecture skeleton with 10 modules, CMake targets, CI-ready builds on Windows/Linux/macOS, core service registry, structured rotating logs, configuration system with schema validation and migration, diagnostic bundle export, application shell with Qt 6 dockable workspace, QSettings persistence, synthetic sample dataset, Catch2 v3 integration with per-module test auto-discovery, CPack packaging.
- **Verification Core** — Layout/netlist import abstraction (streaming parsers with cancellation), versioned JSON intermediate representation, connectivity graph engine (adjacency lists, DFS/BFS visitors, topological sort), typed device/net/pin data model, registerable rule engine with dependency ordering, basic electrical rule checks (floating net, open/short circuit, power domain conflicts), violation result model with serializable filter/group/paginate, power/signal domain tagging, power-intent and current-activity CSV enrichment onto the graph.
- **Real Format Ingestion** — Native LEF 5.x parser with extensive grammar coverage and geometry forms (rect/polygon/path/via placements, antenna/capacitance/tristate semantics), real DEF parser with physical normalization, Verilog gate-level parser with connectivity normalization, hardened SPICE/CDL parser with include / model / parameter / global / subckt constructs, versioned project manifest (JSON/YAML) for explicit role mapping, folder ingestion with heuristic role detection.
- **LEF Signoff-Grade Hardening** — Expanded LEF 5.x grammar coverage, advanced geometry forms, typed technology-rule semantics, deterministic fuzzing and stress validation, integrated LEF normalization across import / storage / CLI / desktop.
- **Visualization Core** — LayoutIR adapter to `UiScene`, QWidget / QPainter 2D canvas, zoom / pan / grid / selection workflow, layer visibility panel with category semantics and presets, severity-colored violation overlays and dockable explorer, connectivity tracing and graph exploration, violation filtering / heatmap / report preview, workspace actions / toolbar / shortcuts, canvas performance instrumentation, viewport history and viewport presets, large-scene coverage.
- **UI Functional Hardening** — Functional Help / About / documentation, real session activity log, bundled sample browser, `Run Checks` wired to rule engine, shared QAction state hardening, persistent workspace state, trace workflow hardening, graph explorer empty-state and LOD, report preview refresh / copy / export, unified empty / error-state language, notification routing, comprehensive headless UI regression suite.
- **Workflow UX** — Native import entry flow with file/folder picker, interactive import review artifact table, `Load Project` action with blocking-diagnostics guard, imported rule-pack execution in desktop mode, current / power CSV enrichments, async `LocalJobPipeline` with stage progress and cancellation, job monitor / history dock with re-openable reports, HTML / JSON export from report preview, workspace summary panel, recent projects and reopen-last-session, actionable diagnostics panel with filtering, violation bulk actions and context menus, filter presets and saved workspace views, first-run onboarding, post-import guidance, cross-probing between panels, hierarchy browser for imported design objects, coverage summary (rendered / skipped / fallback), and accessibility polish.
- **Post-Import Design Activation** — Unified `ImportedDesignSession` model linking LEF/DEF/netlist/rule-pack/power/current data with stable object IDs and provenance, interactive 2D scene realization from imported DEF/LEF content, async realization pipeline with stage progress.
- **Signoff Workflow (Sprint 1)** — Deterministic violation identity keys (schema-versioned), waiver model + parser (CSV / JSON / YAML) with line/row diagnostics, waiver application with audit trail (`metadata.waived`, owner / comment / expires_on), baseline JSON IO with `baseline_schema_version`, regression diff engine (`new` / `removed` / `unchanged` / `changed_severity`) with rule / severity aggregation, signoff JSON exports with run metadata, headless CLI subcommands (`import` / `run` / `baseline` / `diff` / `report`) with CI gating exit codes, workspace summary surfacing active vs waived counts and baseline diff summaries.

### Remaining / Planned

- **Post-Import Activation (continued)** — Full cross-probing maturity, viewport presets tied to objects, layer semantics and physical visibility presets, import completeness coverage, session persistence, commercial smoke corpus.
- **Platform and Extensibility** — Real report generation engine (HTML report content beyond the current shell), durable storage backends (SQLite / PostgreSQL), CSR-oriented graph backend with benchmarking, dynamic plugin SDK, distributed host/worker execution, GPU-accelerated renderer (OpenGL / Vulkan), ML inference and clustering wired into the verification loop, Python bindings (pybind11), Dockerized CI / developer flows.
- **Distributed Execution** — Multi-process host/worker split on localhost first (see `docs/design/distributed.md` for the design draft), then remote artifact store and queue-backed scheduling. Today only `LocalJobPipeline` is implemented.

---

## License

MIT License.

---

**AEGIS-PERC is intended to evolve into a commercial-grade platform for electrical rule verification, signoff workflow automation, and CI-gated regression control.**
