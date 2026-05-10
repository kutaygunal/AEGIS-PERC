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
    ├── tasks.yaml          # Task registry
    └── runs/               # Development run logs
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
| Python scripting | Planned automation API (pybind11 target) |
| Plugin architecture | Tool registry, dynamic panel loading, extensible rule engine |
| Distributed processing | gRPC microservices, worker pools, incremental job execution |
| System design maturity | Event-driven core, DI container, service registry, clean boundaries |

---

---

## Roadmap

### Implemented

- **Foundation** — Project architecture skeleton with 10 modules, CMake targets, CI-ready builds on Windows/Linux/macOS, core service registry, structured rotating logs, configuration system with schema validation and migration, diagnostic bundle export, application shell with Qt6 dockable workspace, QSettings persistence, synthetic sample dataset, Catch2 v3 integration with per-module test auto-discovery.
- **Verification Core** — Layout/netlist import abstraction (streaming parsers with cancellation), versioned JSON intermediate representation, connectivity graph engine (adjacency lists, DFS/BFS visitors, topological sort), typed device/net/pin data model, registerable rule engine with dependency ordering, basic electrical rule checks (floating net, open/short circuit), violation result model with serializable filter/group/paginate, power/signal domain tagging.
- **Visualization Core** — LayoutIR adapter to `UiScene`, QWidget/QPainter 2D canvas, zoom/pan/grid/selection workflow, layer visibility panel, severity-colored violation overlays and dockable explorer, connectivity tracing and graph exploration, violation filtering/heatmap/report preview, workspace actions/toolbar/shortcuts, canvas performance instrumentation and large-scene coverage.
- **UI Functional Hardening** — Functional Help/About/documentation, real session activity log, bundled sample browser, `Run Checks` wired to rule engine, shared QAction state hardening, persistent workspace state, trace workflow hardening, graph explorer empty-state and LOD, report preview refresh/copy, unified empty/error-state language, notification routing, comprehensive headless UI regression suite.
- **Workflow UX** — Native import entry flow with file/folder picker, interactive import review artifact table, `Load Project` action with blocking-diagnostics guard, imported rule-pack execution in desktop mode, current/power CSV enrichments, async `LocalJobPipeline` with stage progress and cancellation, job monitor/history dock, HTML/JSON export from Report Preview, workspace summary panel, recent projects and reopen-last-session, actionable diagnostics panel with filtering, violation bulk actions and context menus, filter presets and saved workspace views, first-run onboarding, cross-probing, and accessibility polish.
- **Real Format Ingestion** — Native LEF parser for technology/library abstracts, real DEF parser with physical normalization, Verilog gate-level netlist parser with connectivity normalization, hardened SPICE/CDL parser with include/model/parameter constructs.
- **LEF Signoff-Grade Hardening** — Expanded LEF 5.x grammar coverage, advanced geometry forms, typed technology-rule semantics, deterministic fuzzing and stress validation, integrated LEF normalization across import/storage/CLI/desktop.
- **Post-Import Design Activation** — Unified `ImportedDesignSession` model linking LEF/DEF/netlist/rule-pack/power data with stable object IDs and provenance, and interactive 2D scene realization from imported DEF/LEF content.

### Remaining Work

- **Post-Import Activation (continued)** — Hierarchy and object browser, rich object properties panel, full cross-probing, viewport presets, layer semantics and physical visibility presets, import completeness coverage, session persistence, async realization pipeline, commercial smoke corpus.
- **Platform and Extensibility** — OpenGL/Vulkan renderer, durable storage backends (SQLite/PostgreSQL), real report generation engine, CSR-oriented graph backend with benchmarking, dynamic plugin SDK, distributed host/worker execution, Dockerized CI/developer flows.

---

## License

MIT License.

---

**AEGIS-PERC is intended to evolve into a commercial-grade platform for electrical rule verification and workflow automation.**
