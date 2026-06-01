# Run Log — 2026-05-22 — S1-008 (Workspace Summary signoff signals)

## Objective
Surface signoff-relevant signals in the Workspace Summary UI:
- active vs waived counts
- optional baseline regression diff summary

## What Changed
- Workspace Summary UI:
  - Added baseline selection controls (Browse/Clear) and persisted the selected baseline path in workspace UI settings.
  - Workspace summary text now reports `Violations: X (active Y, waived Z)` using `metadata.waived`.
  - When a baseline path is set, the summary computes and displays `Baseline diff: new/removed/changed` using active violations (waived excluded).
- Added test hook: `MainWindow::set_signoff_baseline_path_for_tests(...)`.
- Updated `UiWorkflow` test to validate baseline diff text appears after selecting an empty baseline.

## Files Touched
- `src/ui/src/main_window_state.hpp`
- `src/ui/include/aegis/ui/main_window.hpp`
- `src/ui/src/main_window.cpp`
- `src/ui/src/main_window_setup.cpp`
- `tests/unit/ui/test_ui_workflow.cpp`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R UiWorkflow`

## Results
- Build: ✅
- `UiWorkflow` tests: ✅

