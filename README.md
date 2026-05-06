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
├── README.md
├── LICENSE
├── CMakeLists.txt
├── docs/
│   ├── architecture/
│   ├── api/
│   └── user_guide/
├── src/
│   ├── core/              # Parser, Graph, Rule, Inference engines
│   ├── ui/                # Qt desktop shell, OpenGL renderers
│   ├── python_api/        # pybind11 bindings
│   ├── distributed/       # gRPC services, job scheduler
│   └── ai/                # XAI, Copilot, RAG, training pipelines
├── tests/
│   ├── unit/
│   ├── integration/
│   └── benchmarks/
├── scripts/
│   ├── build.py
│   └── setup_env.sh
├── data/
│   └── sample_designs/    # Small open-source layouts for testing
└── models/
    └── pretrained/        # ONNX models, FAISS indices
```

---

## Development Setup

### Prerequisites
- C++20 compiler (MSVC 2022 / GCC 12 / Clang 15+)
- CMake 3.25+
- Qt 6.5+ (Core, Widgets, OpenGL)
- Python 3.10+
- vcpkg or Conan for dependency management
- Docker (for distributed services)
- Ollama (for local copilot LLM)

### Quick Build (Local Desktop Mode)
```bash
# Clone
cd C:/Users/kutay/Desktop/Projects/AEGIS-PERC

# Configure (example with vcpkg)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release -j

# Run tests
ctest --test-dir build --output-on-failure

# Launch
./build/bin/aegis-perc --mode=desktop --design=data/sample_designs/simple_block.def
```

### Distributed Mode (Docker Compose)
```bash
cd backend/
docker-compose up --build
# Launches: Redis, Job Scheduler, Embedding Service, Inference Service, Vector Search
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

### Phase 1 — Foundation (Weeks 1-4)
- [ ] DEF/LEF streaming parser with incremental indexing
- [ ] CSR Graph Engine with adjacency traversal APIs
- [ ] Qt 6 shell + OpenGL layout viewer skeleton
- [ ] CMake build system + CI pipeline

### Phase 2 — Rules & ML (Weeks 5-8)
- [ ] Rule engine framework (floating net, EM risk, power domain)
- [ ] Feature extraction pipeline for violation ML
- [ ] XGBoost training pipeline + ONNX export
- [ ] ONNX Runtime desktop inference integration

### Phase 3 — Explainability & Copilot (Weeks 9-12)
- [ ] SHAP explainability integration
- [ ] RAG pipeline over violation database
- [ ] Ollama-based verification copilot (local LLM)
- [ ] Natural language tool-use agents

### Phase 4 — Scale (Weeks 13-16)
- [ ] gRPC distributed job scheduler
- [ ] Worker node pool with Redis queue
- [ ] Enterprise PostgreSQL backend
- [ ] Python API polish + documentation

### Phase 5 — Polish (Weeks 17-20)
- [ ] Advanced OpenGL/Vulkan renderer (layers, heatmaps, animation)
- [ ] Plugin SDK for third-party rule developers
- [ ] Comprehensive test coverage + benchmarks
- [ ] Commercial-grade documentation and demo video

---

## License

MIT License — Portfolio demonstration project.

*This is an independent educational and portfolio project.*

---

**Built to demonstrate principal-engineer thinking: architecture ownership, production infrastructure, and the intersection of classical EDA workflows with modern AI/ML systems.**
