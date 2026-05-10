# AEGIS-PERC

**Electrical Rule Verification & Root Cause Analysis Platform**

> A commercial-grade desktop and distributed platform for semiconductor electrical reliability verification, combining high-performance C++ graph analytics and interactive multi-layer visualization into a unified engineering workflow.

---

## Product Vision

AEGIS-PERC is being developed as a commercial-grade EDA verification platform for semiconductor electrical reliability analysis. The focus is on production-quality architecture, scalable data ingestion, and deterministic verification workflows for real customer design environments.

---

## Elevator Pitch

AEGIS-PERC analyzes semiconductor layout and connectivity data, detects electrical reliability violations (floating nets, electromigration risks, power domain mismatches), and presents results through an interactive desktop visualization environment.

The system is built for:
- **Chip designers** who need rapid feedback on rule violations
- **Verification leads** who manage large distributed regression runs
- **Methodology teams** who automate checks via Python scripting APIs

---

## Commercial Product Features

### 1. Layout & Netlist Import Engine
- **Formats:** Native LEF, real DEF, Verilog gate-level netlists, hardened SPICE/CDL netlists, JSON intermediate, extensible parser abstraction
- **Architecture:** Streaming parser with incremental indexing, background async loading, multi-threaded parsing
- **Goals:** Memory optimization, large-file handling (>100GB layouts), and sustained performance under production workloads

### 2. Connectivity Graph Engine
- Sparse graph representations of net relationships, power domains, signal propagation, and dependency chains
- CSR (Compressed Sparse Row) storage with custom adjacency structures optimized for IC-scale graphs
- Fast path tracing, domain traversal, and subgraph extraction
- **Tech:** Custom graph engine with Boost.Graph extensions

### 3. Interactive Visualization Engine
- **Qt 6** desktop shell with dockable workspace and plugin-based tool panels
- **OpenGL/Vulkan** multi-layer layout renderer with zoom/pan, violation highlighting, connectivity tracing
- Heatmaps, failure propagation animation, interactive graph exploration
- Multi-view synchronization (layout ↔ schematic ↔ graph)

### 4. Distributed Job Execution
- **Enterprise architecture:** Job scheduler, worker node pool, incremental processing, remote rule execution
- **Communication:** gRPC + FlatBuffers for efficient structured data; ZeroMQ for streaming; Redis-backed job queue
- Supports local single-user mode and multi-node enterprise cluster mode

### 5. Embedded Python Automation API
```python
import aegis

session = aegis.Session()
session.load_layout("chip.def", tech_file="65nm.lef")
session.load_constraints("perc_rules.xml")
violations = session.run_analysis(targets=["floating_net", "em_risk"])

# Export
session.export_report("executive_report.html", format="html", template="corporate_style")
```

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    DESKTOP FRONTEND (Qt 6)                       │
│  ┌──────────┐ ┌──────────┐ ┌────────────────────┐              │
│  │ Layout   │ │ Graph    │ │ Dockable Workspace │              │
│  │ Viewer   │ │ Explorer │ │ & Plugin Tools     │              │
│  └──────────┘ └──────────┘ └────────────────────┘              │
└────────────────────┬────────────────────────────────────────────┘
                     │ C++20 / IPC
┌────────────────────▼────────────────────────────────────────────┐
│                      CORE ENGINE                                 │
│  ┌────────────┐ ┌───────────┐ ┌──────────┐                    │
│  │ Parser     │ │ Graph     │ │ Rule     │                    │
│  │ Engine     │ │ Engine    │ │ Engine   │                    │
│  └────────────┘ └───────────┘ └──────────┘                    │
│  ┌────────────┐ ┌───────────┐ ┌──────────┐                    │
│  │ Visual    │ │ Report   │ │ Python   │                    │
│  │ Engine    │ │ Engine   │ │ API      │                    │
│  └────────────┘ └───────────┘ └──────────┘                    │
│                                                                  │
│  • Event-driven architecture    • Async task scheduling         │
│  • Service registry / DI        • Thread pool orchestration     │
│  • Import manifest + preflight  • Local job pipeline stages     │
│  • Embedded Python interpreter (pybind11)                       │
└────────────────────┬────────────────────────────────────────────┘
                     │ gRPC / REST / WebSocket
┌────────────────────▼────────────────────────────────────────────┐
│                   BACKEND SERVICES                               │
│  ┌────────────┐ ┌────────────┐                                  │
│  │ Job        │ │ Report     │                                  │
│  │ Scheduler  │ │ Generator  │                                  │
│  └────────────┘ └────────────┘                                  │
│  (Distributed host/worker execution is currently documented,    │
│   with local job-pipeline execution implemented in code.)       │
└─────────────────────────────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────────┐
│                      DATA LAYER                                  │
│  PostgreSQL (enterprise)  /  Parquet/Arrow  /  SQLite (local)  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Technology Stack

| Layer | Technology |
|-------|------------|
| **Frontend** | Qt 6, C++20, QML (optional), OpenGL/Vulkan |
| **Core Engine** | C++20, Boost, pybind11, thread pools, async I/O |
| **Graph Analytics** | Custom CSR engine, Boost.Graph |
| **Distributed** | gRPC, ZeroMQ, Redis, FlatBuffers/Protobuf |
| **Data** | SQLite, PostgreSQL, Apache Arrow, Parquet |
| **Build** | CMake, vcpkg, Conan (optional), Docker |
| **Testing** | Catch2 / GoogleTest, pytest, CI/CD via GitHub Actions |

---

## Repository Structure

```
AEGIS-PERC/
├── CMakeLists.txt          # Root build + aegis_warnings + CPack
├── CMakePresets.json       # Windows/Linux presets + CI workflows
├── .clang-tidy             # Static analysis rules
├── .github/workflows/ci.yml  # GitHub Actions CI
├── README.md
├── LICENSE
├── docs/
│   ├── architecture/
│   └── BUILD.md            # Build + warnings-as-errors policy
├── src/
│   ├── core/               # ServiceRegistry, Logger, Config, ConfigMigrator, DiagnosticBundle
│   ├── graph/              # ConnectivityGraph, data model, domain tagging
│   ├── ml/                 # FeatureExtractor placeholder (future expansion)
│   ├── orchestration/      # LocalJobPipeline and staged execution primitives
│   ├── parsing/            # Parser interface, LayoutIR, native LEF/DEF/Verilog/SPICE-CDL parsers + CSV import helpers
│   ├── reporting/          # HTML/JSON report generation primitives
│   ├── rules/              # RuleEngine, electrical rules, violation model, rule-pack loading
│   ├── scripting/          # PythonApi + headless CLI workflow / aegis-perc-cli
│   ├── storage/            # ProjectPackage, import validation, storage engine
│   └── ui/                 # Dockable Qt workspace, canvas, filters, tracing, graph explorer, heatmap, report preview, import wizard
├── tests/
│   ├── unit/               # Catch2 v3 tests per module
│   └── CMakeLists.txt      # catch_discover_tests auto-registration
├── scripts/
│   └── validate_samples.py # Synthetic data schema validator
├── data/
│   └── sample_designs/     # inverter, nand2, ring_oscillator
├── models/
│   └── pretrained/
└── orchestration/
    ├── tasks.yaml          # Sprint task registry
    └── runs/               # Sprint run logs
```

---

## Development Setup

### Prerequisites
- C++20 compiler (MSVC 2022 / GCC 12 / Clang 15+)
- CMake 3.25+
- Qt 6.5+ (Core, Widgets)
- Python 3.10+

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

---

## Why This Project Matters

| Capability | Demonstrated Here |
|------------|-------------------|
| Modern C++ architecture at scale | Custom graph engine, memory-optimized parsers, async I/O |
| Commercial desktop software | Qt 6, dockable UI, plugin architecture, professional UX |
| Large-scale graph processing | CSR representations, IC-scale connectivity analysis |
| Engineering workflows | EDA-style rule checking, report generation, regression runs |
| Data pipelines | Streaming parsers, Arrow/Parquet, distributed job queues |
| Interactive visualization | OpenGL multi-layer rendering, heatmaps, propagation animation |
| Performance optimization | Multi-threaded parsing, thread pools |
| GPU acceleration | Vulkan renderer (planned) |
| Qt expertise | Complex desktop shell, custom widgets, OpenGL integration |
| Python scripting | Embedded automation API mirroring commercial EDA tools |
| Plugin architecture | Tool registry, dynamic panel loading, extensible rule engine |
| Distributed processing | gRPC microservices, worker pools, incremental job execution |
| System design maturity | Event-driven core, DI container, service registry, clean boundaries |

---

## Roadmap

### Phase 1 — Commercial Foundation (Sprint 1) ✅ **COMPLETE**
- ✅ Project architecture skeleton (10 modules, CMake targets, CI-ready)
- ✅ Core service registry (type-safe DI, circular-dependency detection)
- ✅ Structured rotating logs (spdlog, file rotation, module tags)
- ✅ Configuration system (JSON, schema validation, env overrides, callbacks)
- ✅ Diagnostic bundle export (zip with redaction, progress callbacks)
- ✅ CMake/CI/test hardening (presets, warnings-as-errors, clang-tidy, GitHub Actions)
- ✅ Application shell (Qt6 dockable workspace, QSettings persistence)
- ✅ Synthetic sample data (inverter, NAND2, ring oscillator with JSON schema)
- ✅ Config migration system (versioned chain migrations v0→v1→v2)
- ✅ Catch2 v3 integration with per-module test discovery (**332 CTest tests currently passing**)

### Phase 2 — Verification Core (Sprint 2) ✅ **COMPLETE**
- ✅ Layout/netlist import abstraction (IParser, IParserCallbacks, cancellation tokens)
- ✅ JSON intermediate layout representation (versioned, schema-validated, nlohmann/json)
- ✅ Basic DEF-like parser prototype (streaming, line/column diagnostics, 10K-line perf)
- ✅ Connectivity graph model (adjacency lists, DFS/BFS visitors, 100K-node scale)
- ✅ Net/device/pin data model (strongly typed, PropertyMap with coercion, immutable)
- ✅ Rule engine abstraction (registerable rules, dependency ordering, plugin-ready)
- ✅ Basic electrical rule checks (floating net, open circuit, short circuit)
- ✅ Violation result model (serializable, filter/group/paginate, stable IDs)
- ✅ Power/signal domain tagging (graph traversal propagation, conflict detection)
- ✅ SPICE-like netlist parser prototype (R/C/M/V/I + .SUBCKT/.ENDS, continuation, comments)

### Phase 3 — Visualization Core (Sprint 3) ✅ **COMPLETE**
- ✅ Read-only `LayoutIR` → `UiScene` adapter with stable IDs, bounds, colors, and metadata
- ✅ QWidget/QPainter 2D layout canvas installed as the central workspace view
- ✅ Stable zoom/pan/reset/fit controls, grid rendering, coordinates, and status updates
- ✅ Layer visibility/appearance dock with show-all, hide-all, and isolate workflows
- ✅ Shape/port/annotation selection model with properties-panel synchronization
- ✅ Severity-colored violation overlays with graceful handling of unresolved locations
- ✅ Dockable violation explorer with details, metadata, copyable IDs, and canvas centering
- ✅ UI-side violation filtering, search, serializable filter state, and overlay synchronization
- ✅ Connectivity tracing and graph exploration docks with selection sync and nonfatal unresolved feedback
- ✅ Violation density heatmap with opacity control, layer awareness, and empty-state messaging
- ✅ Interactive report preview dock with design summary, severity totals, selected-violation details, and snapshot hook
- ✅ Shared workspace actions/toolbar/shortcuts for sample loading, fit/reset, grid, overlays, run-checks hook, and clear selection
- ✅ Canvas performance instrumentation, developer metrics, offscreen culling, tiny-geometry simplification, and synthetic large-scene coverage
- ✅ End-to-end visualization workflow smoke tests plus a manual UI checklist under `orchestration/manual_ui_checklist_p3.md`

### Phase 7 — UI Functional Hardening (Sprint 7) ✅ **COMPLETE**
- ✅ Functional Help/About/documentation actions with headless-safe dialogs and graceful local-doc fallback
- ✅ Real session activity log dock with severity styling, bounded history, and UI-event routing
- ✅ Bundled sample browser with deterministic inverter/NAND2/ring-oscillator workflows
- ✅ Real `Run Checks` workspace command wired to the rule engine and connectivity graph generation
- ✅ Shared QAction state hardening across menus, toolbar, shortcuts, selection, violations, and trace state
- ✅ Persistent UI-owned workspace state for docks, grid, overlays, heatmap, metrics, and violation filters
- ✅ Trace workflow hardening with validation, consistent feedback, replacement handling, and synchronized control enablement
- ✅ Graph explorer empty-state, LOD, search, and reload/selection-stability polish
- ✅ Report preview refresh/copy actions, selection-aware summary updates, and snapshot-control behavior
- ✅ Unified actionable empty/error-state language across canvas, properties, violations, trace, graph explorer, and preview panels
- ✅ Lightweight notification routing to coordinate status-bar, activity-log, and panel feedback without duplicate trace-request noise
- ✅ Comprehensive Sprint 7 headless regression coverage via `ctest --test-dir build -C Release --output-on-failure -L P7` and broader `-R Ui`

### Phase 8 — Workflow UX Completion (Sprint 8) 🚧 **IN PROGRESS**
- ✅ P8-001: Native import entry flow now supports selecting either a project folder or multiple files from the desktop UI, with cancel-safe feedback and testable picker injection seams
- ✅ P8-002: Import review now presents an interactive artifact table with path/category/role/requirement/status/origin columns, UI role override controls, and diagnostics visible alongside the table
- ✅ P8-003: Import review now includes a guarded `Load Project` action that commits validated package metadata into the workspace, disables loading while blockers remain, and reports the committed result through status/log feedback
- ✅ P8-004: Desktop `Run Checks` now switches to imported rule-pack execution when a validated package is loaded, applies optional current/power enrichments onto the execution graph, reports import-vs-default execution mode, and surfaces parser/apply failures through activity-log and status feedback
- ✅ P8-005: Imported desktop `Run Checks` now executes asynchronously through `LocalJobPipeline`, surfaces stage progress in the workspace, feeds completion/failure results back into existing UI panels, and keeps built-in sample/demo workflows synchronous for deterministic tests
- [ ] Desktop report export, workspace summary, diagnostics navigation, and workflow polish

### Phase 9 — Real Format Ingestion (Sprint 9) ✅ **COMPLETE**
- ✅ Native LEF parser for technology/library abstracts with actionable diagnostics and sample-package coverage
- ✅ Real DEF parser replacing the toy DEF-like prototype, with physical normalization for graph/UI workflows
- ✅ Verilog gate-level netlist parser and connectivity normalization wired into import preflight, CLI, orchestration, and desktop `Run Checks`
- ✅ SPICE/CDL parser hardening for customer-import MVP coverage, including include/model/parameter constructs, explicit unsupported diagnostics, and representative CDL device syntax

### Phase 11 — Platform, Performance, and Extensibility (Sprint 11)
- [ ] OpenGL/Vulkan-backed renderer replacing the QPainter-only path
- [ ] Real pybind11 Python automation API
- [ ] Durable storage backends (SQLite/PostgreSQL) and analytical exports (Arrow/Parquet)
- [ ] Dockerized developer/CI flows and optional Conan/vcpkg integration
- [ ] Implemented distributed host/worker execution stack
- [ ] Dynamic plugin SDK for third-party rules
- [ ] Templated HTML/JSON report generation engine
- [ ] CSR-oriented graph backend and performance benchmarking

### Phase 12 — LEF Signoff-Grade Hardening (Sprint 12) ✅ **COMPLETE**
- ✅ Expanded LEF coverage toward broad 5.x grammar compatibility (`docs/design/lef-coverage.md`)
- ✅ Added advanced LEF geometry/forms and richer abstract-shape normalization (`docs/design/lef-coverage.md`)
- ✅ Implemented typed LEF technology-rule semantics and semantic validation (`docs/design/lef-coverage.md`)
- ✅ Added deterministic LEF fuzzing, stress, and performance validation (`docs/design/lef-coverage.md`)
- ✅ Integrated LEF normalization into import, storage, CLI, and desktop workflow paths

### Phase 5 — Automation & Extensibility (Sprint 5)
- [ ] Batch analysis CLI polishing beyond Sprint 6 customer-workflow scope
- [ ] Embedded Python automation API expansion
- [ ] Plugin SDK for third-party rules
- [ ] Rich report generation/templates

### Phase 6 — Customer Workflow Import & Scalable Execution (Sprint 6) ✅ **COMPLETE**
- ✅ Customer import contract and manifest documentation (`docs/design/import-workflow.md`)
- ✅ Versioned `ProjectPackage` intake model with normalized artifact references
- ✅ Preflight validation and heuristic file-role detection
- ✅ YAML/JSON rule-pack ingestion for configurable checks
- ✅ Power-domain CSV import with UPF-ready abstraction
- ✅ Current/activity CSV import for EM-style checks
- ✅ Desktop import wizard and drag-and-drop ingestion flow
- ✅ Headless CLI workflow: `import`, `run`, `report`
- ✅ Local job pipeline with staged progress, cancellation, and JSON/HTML export
- ✅ Distributed host/worker execution architecture documentation (`docs/design/distributed.md`)

> Current implementation snapshot: deterministic verification, desktop visualization, customer import workflow, headless CLI flow, local job-pipeline orchestration, Sprint 8 workflow UX upgrades, and Sprint 9 real-format ingestion are implemented. Imported customer projects now parse through native LEF, real DEF, Verilog gate-level connectivity normalization, and hardened SPICE/CDL ingestion with preflight diagnostics and execution-path integration. Remaining gaps span rendering, storage, scripting, reporting, plugins, and distributed execution.

---

## License

MIT License.

---

**AEGIS-PERC is intended to evolve into a commercial-grade platform for electrical rule verification and workflow automation.**
