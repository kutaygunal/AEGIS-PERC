# Run Log: README Source-Code Synchronization

## Date
2026-05-23

## Task
Repository maintenance: re-synchronize `README.md` against the current source
code. Remove claims for features/technologies that are not implemented and add
features that are implemented but were missing or under-emphasized.

## Files Changed
- `README.md` — full rewrite of product description, feature list,
  architecture diagram, technology stack table, repository structure,
  development setup, "Why This Project Matters" table, and roadmap.
- `orchestration/HANDOFF.md` — updated `Last Updated` date,
  `Current Status`, and `Last Completed Task` to reflect this work.

## What was removed (not in source)
- "CSR (Compressed Sparse Row) storage" — graph uses `std::vector<EdgeId>`
  adjacency lists (`outgoing_edges` / `incoming_edges`); no CSR.
- "Custom graph engine with Boost.Graph extensions" — no Boost dependency
  is declared or used.
- "OpenGL/Vulkan multi-layer layout renderer" — `LayoutCanvas` uses
  `QPainter` 2D rendering only.
- "Failure propagation animation" — not implemented.
- Distributed execution section (gRPC, ZeroMQ, Redis, FlatBuffers,
  worker pool) — only `LocalJobPipeline` exists; multi-node design
  lives in `docs/design/distributed.md` as a draft.
- Data layer (PostgreSQL / Parquet / Arrow / SQLite) — only JSON files
  (`SessionCache`, manifests, exports) and a `StorageEngine` stub.
- Tech-stack rows for Boost, pybind11, OpenGL/Vulkan, gRPC, ZeroMQ,
  Redis, FlatBuffers/Protobuf, SQLite, PostgreSQL, Apache Arrow,
  Parquet, vcpkg/Conan, Docker.
- "Dynamic plugin SDK" / "tool registry" — modules are static libs;
  rule engine is extensible through the `IRule` interface, not a
  dynamic plugin loader.

## What was added (in source but missing/under-emphasized)
- Sprint 1 signoff workflow: violation identity keys, waiver parsing
  (CSV / JSON / YAML), waiver application with audit trail, baseline
  IO with `baseline_schema_version`, regression diff engine, signoff
  JSON exports with run metadata, CI gating exit codes.
- `aegis-perc-cli` subcommands: `import`, `run`, `report`, `baseline`,
  `diff` and their exit-code semantics.
- Real LEF 5.x coverage (with link to `docs/design/lef-coverage.md`),
  DEF / Verilog / SPICE-CDL parsers, project manifest, power-intent
  and current-activity CSV.
- Power / current enrichment onto the graph.
- Workspace summary with active vs waived counts and baseline diff
  summary.
- Coverage summary, hierarchy browser, cross-probing between panels,
  filter presets, saved workspace views, viewport presets, viewport
  history, performance metrics.
- Drag-and-drop project folder ingestion, bundled sample browser,
  recent projects / reopen-last-session, onboarding / post-import
  guidance.
- JSON-based session cache (`.aegis/session_cache.json`).
- CPack packaging (NSIS / ZIP / DragNDrop / TGZ / DEB).
- All 10 modules and their actual responsibilities, with the
  recognition that `ML`, `PythonApi`, and `StorageEngine` are still
  placeholders.

## Validation
This is a documentation-only change. Build and test commands from
`docs/BUILD.md` remain the verification path:
```bash
cmake --preset windows-release
cmake --build build/windows-release --config Release
ctest --preset ci-test
```
(Not executed in this maintenance pass; no source files were modified.)

## Notes
- The "Why This Project Matters" table now distinguishes implemented
  capabilities from the dynamic plugin SDK and multi-node distributed
  execution, which are still in the planned bucket.
- The Roadmap's "Implemented" list has been expanded to cover the
  full set of P-series / S1 work that has reached `review` status in
  `orchestration/tasks.yaml`.
- The Roadmap's "Remaining" list explicitly notes the
  `docs/design/distributed.md` draft and the placeholder status of
  ML / PythonApi / StorageEngine.

## Status
review
