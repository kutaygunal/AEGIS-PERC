# Handoff

## Last Updated
2026-05-23

## Current Status
`README.md` has been re-synchronized against the current source code:
removed claims about CSR storage, Boost.Graph, OpenGL/Vulkan rendering,
FlatBuffers/gRPC/ZeroMQ/Redis, Arrow/Parquet, PostgreSQL, dynamic plugin SDK,
and pybind11 bindings (none of which are present in the source today).
Added accurate sections for the Sprint 1 signoff workflow (waivers, baselines,
regression diff, CI gating CLI), the actual adjacency-list graph engine, the
LEF 5.x coverage scope, the JSON-based session cache, the `aegis-perc-cli`
subcommand set, and the explicit list of remaining/planned work.

## Last Completed Task
README source-code synchronization

## Current Task
No implementation task in progress

## Ready Tasks
- `S1-009` ? UI: violation explorer supports 'Active vs Waived' filter pill

## Blocked Tasks
- None recorded in the current trimmed `tasks.yaml`

## Important Notes
- `orchestration/tasks.yaml` has been replaced with a new Sprint 1 task set targeting signoff value (waivers + baselines + regression diff + CI gating).
- `orchestration/roadmap.yaml` has been replaced to match the new product focus and Sprint 1 exit criteria.
- `aegis::rules::Violation::identity_key()` is now serialized into JSON exports as `identity_key`.
- Docs: `docs/design/signoff_workflow.md` defines the v1 contract (inputs, normalization, point quantization).
- Tests: `ctest --test-dir build -C Release -R ViolationIdentity` passes.
- Waivers parsing now lives in `aegis::rules` (`WaiverEntry`, `WaiverParseResult`) with file parsers for CSV/JSON/YAML.
- Tests: `ctest --test-dir build -C Release -R Waivers` passes.
- Waiver application now tags each violation with `metadata.waived` and optional waiver fields (`waiver_comment`, `waiver_owner`, etc.).
- JSON reports now include `active_violation_count` and `waived_violation_count` in `summary` (CLI + job pipeline JSON export paths).
- Tests: `ctest --test-dir build -C Release -R WaiverApplication` passes.
- Baseline IO now lives in `aegis::reporting`:
  - Baseline JSON includes `baseline_schema_version` and a deterministically sorted `records` array.
  - Records store `identity_key`, `rule_id`, `severity`, plus optional location context fields.
- Tests: `ctest --test-dir build -C Release -R BaselineIO` passes.
- Regression diff now lives in `aegis::reporting`:
  - `diff_baselines(baseline, current)` and `diff_baseline_against_violations(...)`
  - Outputs deterministic sets and summary aggregates by rule and severity.
  - Duplicate identity keys and missing identity keys produce warning diagnostics.
- Tests: `ctest --test-dir build -C Release -R RegressionDiff` passes.
- `aegis-perc-cli` now supports:
  - `baseline` command: writes baseline JSON snapshot (exit code 0 on successful baseline write)
  - `diff` command: writes diff JSON and returns exit code 5 (`SuccessWithRegressions`) when new/worse findings are detected
- Tests: `ctest --test-dir build -C Release -R \"aegis-perc-cli\"` passes.
- Reporting now provides stable exports for signoff artifacts:
  - `export_baseline_json(...)` and `export_regression_diff_json(...)` include `run_metadata` and deterministic structure.
  - CLI baseline/diff commands now reuse these exports (no duplicated JSON assembly).
- Tests: `ctest --test-dir build -C Release -R Reporting` passes.
- Workspace Summary now includes:
  - `Violations: X (active Y, waived Z)` derived from `metadata.waived`
  - Optional baseline controls (Browse/Clear) persisted in settings, and a computed `Baseline diff: new/removed/changed` line when a baseline is selected.
- Tests: `ctest --test-dir build -C Release -R UiWorkflow` passes.

## Next Recommended Action
Start `S1-009` by adding an "Active/Waived" toggle in the violation explorer filter model so users can hide waived findings in the main table without changing exports.
