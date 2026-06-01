# Run Log — 2026-05-22 — S1-005 (Regression Diff Engine)

## Objective
Implement a deterministic regression diff engine to compare baseline vs current results and produce:
new / removed / unchanged / changed-severity sets plus summary metrics.

## What Changed
- Added regression diff API in `aegis::reporting`:
  - `diff_baselines(baseline, current)`
  - `diff_baseline_against_violations(baseline, current_violations)`
- Diff outputs:
  - `newly_introduced`, `removed`, `unchanged`, `changed_severity`
  - Summary aggregates by rule id and severity (string form).
- Edge cases:
  - Duplicate identity keys: a canonical record is chosen deterministically, and a warning diagnostic is emitted.
  - Missing identity keys: ignored with warning diagnostics.
- Added unit tests covering basic diff semantics and the edge cases above.

## Files Touched
- `src/reporting/include/aegis/reporting/regression_diff.hpp`
- `src/reporting/src/regression_diff.cpp`
- `src/reporting/CMakeLists.txt`
- `tests/unit/reporting/test_regression_diff.cpp`
- `tests/CMakeLists.txt`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R RegressionDiff`

## Results
- Build: ✅
- `RegressionDiff` tests: ✅

