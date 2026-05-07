# AEGIS-PERC

**AI-Assisted Electrical Rule Verification & Root Cause Analysis Platform**

> A commercial-grade desktop and distributed platform for semiconductor electrical reliability verification, combining high-performance C++ graph analytics, machine learning classification, explainable AI, and interactive multi-layer visualization into a unified engineering workflow.

---

## Target Alignment

**Role:** Principal Software Engineer — EDA Verification & AI/ML Integration

This project is architected as a production-caliber EDA verification platform. It demonstrates system design maturity, performance engineering at scale, classical EDA workflows, and modern AI/ML integration — the intersection of physical design verification and intelligent systems engineering.

---

## Elevator Pitch

AEGIS-PERC analyzes semiconductor layout and connectivity data, detects electrical reliability violations (floating nets, electromigration risks, power domain mismatches), clusters failure patterns using ML, and delivers **explainable root-cause recommendations** with interactive visualization and an agentic verification copilot.

The system is built for:
- **Chip designers** who need rapid, explainable feedback on rule violations
- **Verification leads** who manage large distributed regression runs
- **Methodology teams** who automate checks via Python scripting APIs

---

## Commercial Product Features

### 1. Layout & Netlist Import Engine
- **Formats:** DEF, LEF, SPICE netlists, JSON intermediate, extensible custom parser abstraction
- **Architecture:** Streaming parser with incremental indexing, background async loading, multi-threaded parsing
- **Demonstrates:** Memory optimization, large-file handling (>100GB layouts), performance engineering

### 2. Connectivity Graph Engine
- Sparse graph representations of net relationships, power domains, signal propagation, and dependency chains
- CSR (Compressed Sparse Row) storage with custom adjacency structures optimized for IC-scale graphs
- Fast path tracing, domain traversal, and subgraph extraction
- **Tech:** Custom graph engine with Boost.Graph extensions

### 3. AI/ML Violation Classification
- **Rule targets:** Floating nets, electromigration risks, power domain mismatch, suspicious routing structures, timing-risk clusters, anomalous pattern detection
- **Models:** XGBoost / LightGBM for tabular violation features; Graph Neural Networks (GNN) embeddings for structural pattern recognition; FAISS similarity search for historical pattern matching
- **Inference:** ONNX Runtime for high-throughput desktop inference; optional GPU batch processing

### 4. Explainable AI (XAI) Engine
Instead of "Violation detected," the system produces:
- **SHAP-based** feature attribution per violation
- Confidence scores and contributing physical features
- Similar historical pattern retrieval
- Remediation suggestions ranked by past fix success

### 5. Interactive Visualization Engine
- **Qt 6** desktop shell with dockable workspace and plugin-based tool panels
- **OpenGL/Vulkan** multi-layer layout renderer with zoom/pan, violation highlighting, connectivity tracing
- Heatmaps, failure propagation animation, interactive graph exploration
- Multi-view synchronization (layout ↔ schematic ↔ graph)

### 6. Agentic AI Verification Copilot
- Natural language interface powered by local LLM (Ollama) with RAG over violation databases
- **Example workflows:**
  - "Show the highest-risk power domains in this block."
  - "Why did violation V-2047 occur and what is the confidence?"
  - "Find violations structurally similar to V-1042."
  - "Generate a remediation plan for the top 20 EM risks."
  - "Summarize reliability concerns for the executive report."

### 7. Distributed Job Execution
- **Enterprise architecture:** Job scheduler, worker node pool, incremental processing, remote rule execution
- **Communication:** gRPC + FlatBuffers for efficient structured data; ZeroMQ for streaming; Redis-backed job queue
- Supports local single-user mode and multi-node enterprise cluster mode

### 8. Embedded Python Automation API
```python
import aegis

session = aegis.Session()
session.load_layout("chip.def", tech_file="65nm.lef")
session.load_constraints("perc_rules.xml")
violations = session.run_analysis(targets=["floating_net", "em_risk"])

# Explain and export
session.explain(violations[0])
session.export_report("executive_report.html", format="html", template="corporate_style")
```

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    DESKTOP FRONTEND (Qt 6)                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────────┐  │
│  │ Layout   │ │ Graph    │ │ Copilot  │ │ Dockable Workspace │  │
│  │ Viewer   │ │ Explorer │ │ Chat UI  │ │ & Plugin Tools     │  │
│  └──────────┘ └──────────┘ └──────────┘ └────────────────────┘  │
└────────────────────┬────────────────────────────────────────────┘
                     │ C++20 / IPC
┌────────────────────▼────────────────────────────────────────────┐
│                      CORE ENGINE                                 │
│  ┌────────────┐ ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ Parser     │ │ Graph     │ │ Rule     │ │ ML Inference   │  │
│  │ Engine     │ │ Engine    │ │ Engine   │ │ Engine (ONNX)  │  │
│  └────────────┘ └───────────┘ └──────────┘ └────────────────┘  │
│  ┌────────────┐ ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ XAI        │ │ Visual    │ │ Report   │ │ AI Assistant   │  │
│  │ Engine     │ │ Engine    │ │ Engine   │ │ Engine (RAG)   │  │
│  └────────────┘ └───────────┘ └──────────┘ └────────────────┘  │
│                                                                  │
│  • Event-driven architecture    • Async task scheduling         │
│  • Service registry / DI        • Thread pool orchestration     │
│  • Embedded Python interpreter (pybind11)                       │
└────────────────────┬────────────────────────────────────────────┘
                     │ gRPC / REST / WebSocket
┌────────────────────▼────────────────────────────────────────────┐
│                   BACKEND SERVICES                               │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌──────────────┐  │
│  │ Job        │ │ Embedding  │ │ Inference  │ │ Vector       │  │
│  │ Scheduler  │ │ Service    │ │ Service    │ │ Search (FAISS)│  │
│  └────────────┘ └────────────┘ └────────────┘ └──────────────┘  │
│  ┌────────────┐ ┌────────────┐                                  │
│  │ Report     │ │ Streaming  │                                  │
│  │ Generator  │ │ Gateway    │                                  │
│  └────────────┘ └────────────┘                                  │
└─────────────────────────────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────────┐
│                      DATA LAYER                                  │
│  SQLite (local)  /  PostgreSQL (enterprise)  /  Parquet/Arrow  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Technology Stack

| Layer | Technology |
|-------|------------|
| **Frontend** | Qt 6, C++20, QML (optional), OpenGL/Vulkan |
| **Core Engine** | C++20, Boost, pybind11, thread pools, async I/O |
| **Graph Analytics** | Custom CSR engine, Boost.Graph, future GNN integration |
| **ML / AI** | Python, PyTorch, ONNX Runtime, XGBoost, FAISS, Ollama |
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
│   ├── core/               # ServiceRegistry, Logger, Config, DiagnosticBundle
│   ├── graph/              # GraphModel stub
│   ├── ml/                 # FeatureExtractor stub
│   ├── parsing/            # ParserInterface stub
│   ├── reporting/          # ReportGenerator stub
│   ├── rules/              # RuleEngine + IRule stub
│   ├── scripting/          # PythonApi stub
│   ├── storage/            # StorageEngine stub
│   └── ui/                 # MainWindow (dockable workspace)
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
ctest --preset ci-test
```

---

## Why This Project Matters

| Capability | Demonstrated Here |
|------------|-------------------|
| Modern C++ architecture at scale | Custom graph engine, memory-optimized parsers, async I/O |
| Commercial desktop software | Qt 6, dockable UI, plugin architecture, professional UX |
| AI/ML integration | ONNX inference, XGBoost, GNN embeddings, similarity search |
| Large-scale graph processing | CSR representations, IC-scale connectivity analysis |
| Engineering workflows | EDA-style rule checking, report generation, regression runs |
| Data pipelines | Streaming parsers, Arrow/Parquet, distributed job queues |
| Interactive visualization | OpenGL multi-layer rendering, heatmaps, propagation animation |
| Performance optimization | Multi-threaded parsing, thread pools, GPU inference batching |
| GPU acceleration | Vulkan renderer, CUDA/ROCm inference path (planned) |
| Qt expertise | Complex desktop shell, custom widgets, OpenGL integration |
| Python scripting | Embedded automation API mirroring commercial EDA tools |
| Agentic AI workflows | LLM copilot with RAG, tool use, explainable outputs |
| Explainable AI | SHAP attribution, feature importance, historical pattern linking |
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
- ✅ Catch2 v3 integration with per-module test discovery (52 CTest tests)

### Phase 2 — Verification Core (Sprint 2)
- [ ] Layout/netlist import abstraction
- [ ] JSON intermediate layout representation
- [ ] Basic DEF-like parser prototype
- [ ] Connectivity graph model
- [ ] Net/device/pin data model
- [ ] Rule engine abstraction
- [ ] Basic electrical rule checks
- [ ] Violation result model
- [ ] Power/signal domain tagging
- [ ] SPICE-like netlist parser prototype

### Phase 3 — Visualization Core (Sprint 3)
- [ ] 2D layout canvas (QPainter-based)
- [ ] Multi-layer rendering
- [ ] Violation highlighting
- [ ] Interactive zoom/pan

### Phase 4 — AI/ML Core (Sprint 4)
- [ ] Feature extraction from violations
- [ ] XGBoost / LightGBM training pipeline
- [ ] ONNX Runtime inference integration
- [ ] SHAP explainability engine

### Phase 5 — Automation & Extensibility (Sprint 5)
- [ ] Batch analysis CLI (headless)
- [ ] Embedded Python automation API
- [ ] Plugin SDK for third-party rules
- [ ] Report generation (HTML/JSON)

### Phase 6 — Enterprise Scale (Sprint 6)
- [ ] Distributed job execution design
- [ ] gRPC services + Redis job queue
- [ ] Worker node pool
- [ ] Enterprise PostgreSQL backend

---

## License

MIT License — Portfolio demonstration project.

*This is an independent educational and portfolio project.*

---

**Built to demonstrate principal-engineer thinking: architecture ownership, production infrastructure, and the intersection of classical EDA workflows with modern AI/ML systems.**
