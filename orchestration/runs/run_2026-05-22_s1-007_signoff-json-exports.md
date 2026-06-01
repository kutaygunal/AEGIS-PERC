# Run Log — 2026-05-22 — S1-007 (Baseline + Diff JSON Exports)

## Objective
Centralize baseline and regression-diff JSON serialization under `aegis::reporting` so CLI and future UI/orchestration exports share a deterministic, test-covered format including run metadata.

## What Changed
- Added `RunMetadata` and exports:
  - `export_baseline_json(baseline, meta)`
  - `export_regression_diff_json(diff, meta)`
- Exports include:
  - `run_metadata` (created_at_utc, tool_version, optional project_name and rule_pack_id)
  - stable summary structures and deterministic arrays
- Updated `aegis-perc-cli` baseline/diff commands to reuse reporting exports (removed ad-hoc JSON assembly).
- Added reporting tests validating stable structure and deterministic output.

## Files Touched
- `src/reporting/include/aegis/reporting/signoff_exports.hpp`
- `src/reporting/src/signoff_exports.cpp`
- `src/reporting/CMakeLists.txt`
- `src/scripting/src/cli_workflow.cpp`
- `tests/unit/reporting/test_signoff_exports.cpp`
- `tests/CMakeLists.txt`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R Reporting`

## Results
- Build: ✅
- Reporting tests: ✅

