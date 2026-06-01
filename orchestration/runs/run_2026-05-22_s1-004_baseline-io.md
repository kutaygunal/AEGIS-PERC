# Run Log — 2026-05-22 — S1-004 (Baseline JSON + Schema Versioning)

## Objective
Introduce a versioned baseline JSON format and deterministic read/write helpers for regression diffing.

## What Changed
- Added baseline domain model in `aegis::reporting`:
  - `BaselineFile` with `baseline_schema_version` (v1) and `records`
  - `BaselineRecord` includes `identity_key`, `rule_id`, `severity`, plus optional location context fields
- Implemented baseline IO:
  - `build_baseline(violations, project_name)` produces deterministically ordered records
  - `write_baseline_json_file(...)` / `read_baseline_json_file(...)`
  - Schema mismatches throw `std::invalid_argument`
- Added unit tests for round-trip and schema mismatch handling.

## Files Touched
- `src/reporting/include/aegis/reporting/baseline.hpp`
- `src/reporting/src/baseline.cpp`
- `src/reporting/CMakeLists.txt`
- `tests/unit/reporting/test_baseline_io.cpp`
- `tests/CMakeLists.txt`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R BaselineIO`

## Results
- Build: ✅
- `BaselineIO` tests: ✅

