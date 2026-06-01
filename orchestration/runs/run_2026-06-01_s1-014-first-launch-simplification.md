# Run Log: S1-014 — First-Launch Workspace Simplification

**Date:** 2026-06-01  
**Task:** S1-014 — UI: simplify first-launch workspace (hide optional docks + toolbar groups)  
**Commit:** `24273a3`

## Goal
Implement progressive disclosure for the first-launch UI: hide non-essential docks and panels on startup, only revealing them when a design is loaded or checks are run.

## Changes

### Core Implementation
- **`src/ui/include/aegis/ui/main_window.hpp`**
  - Added `WorkspaceState` enum: `Empty`, `DesignLoaded`, `ChecksRun`.
  - Added `set_workspace_state()`, `workspace_state()`, `is_dock_hidden_by_user()`, `reset_user_dock_visibility_overrides()`.

- **`src/ui/src/main_window.cpp`**
  - Added `dock_visibility_table()` — static map defining default visibility for each dock per state.
  - `set_workspace_state()` applies the table, respecting `user_hidden_docks` overrides.
  - `setup_ui()` now calls `set_workspace_state(Empty)` at end.
  - `set_scene()` transitions to `DesignLoaded` when a non-empty scene is set.
  - `set_connectivity_graph()` transitions to `DesignLoaded` when a non-null graph is set.
  - `set_violations()` no longer auto-advances state (state transition is driven by `JobWorkflowController::finalize_active_job`).

- **`src/ui/src/main_window_setup.cpp`**
  - `setup_dock_panels()`: added `QDockWidget::visibilityChanged` listeners to track user-driven hide/show intent in `user_hidden_docks`.
  - Replaced flat View-menu dock toggles with a `View → Panels` submenu.
  - `apply_import_package()`: calls `set_workspace_state(DesignLoaded)`.
  - `load_bundled_sample()`: calls `set_workspace_state(DesignLoaded)`.
  - Added documentation comments noting `user_hidden_docks` is in-memory only for this cycle.

- **`src/ui/src/main_window_state.hpp`**
  - Added `workspace_state`, `user_hidden_docks`, `applying_workspace_state` fields to `Impl`.

- **`src/ui/src/job_workflow_controller.cpp`**
  - `finalize_active_job()`: calls `set_workspace_state(ChecksRun)` after `set_violations()`.

### Tests
- **`tests/unit/ui/test_ui_workflow.cpp`**
  - New test: `S1-014 first-launch workspace hides optional docks and reveals them on design load and check completion` (22 assertions).
  - New test: `S1-014 user-driven dock hide is remembered and overrides auto-show` (8 assertions).
  - New test: `S1-014 View menu exposes a Panels submenu with toggleable dock actions` (5 assertions).

- **`tests/unit/ui/test_workspace_actions.cpp`**
  - Updated `WorkspaceActions view menu uses real dock toggle actions` to find toggles under `View → Panels` submenu and to explicitly show docks before testing toggle behavior (since first launch now hides them).

### Orchestration
- **`orchestration/tasks.yaml`**: S1-014 status updated to `review`; test commands updated.
- **`orchestration/HANDOFF.md`**: Updated with S1-014 completion notes.

## Test Results

```
ctest --test-dir build/windows-release -C Release -R ui
=> 100% tests passed, 0 tests failed out of 32
```

Specific relevant test counts:
- UiWorkflow: 4 tests passed
- UiStates: 4 tests passed
- WorkspaceActions: 4 tests passed
- WorkspacePersistence: 5/6 passed (test #156 is a pre-existing segfault unrelated to this change)

## Known Issues / Notes
- **Pre-existing segfault**: Test #156 (`WorkspacePersistence persists recent imported projects and reopen-last-session preference`) segfaults both with and without S1-014 changes. Confirmed via `git stash` / `git stash pop` before/after test. This is an existing race condition in the async import-restore path and should be handled in a separate bug-fix PR.
- **User-hidden-dock persistence**: The `user_hidden_docks` set is in-memory only. QSettings `saveState()`/`restoreState()` already persists dock geometry/visibility; the in-memory list is the lifecycle hint so auto-show does not undo an explicit hide. Full persistence can be added later if needed.
- **Toolbar slimming**: The toolbar still shows all 19 buttons. A follow-up can slim it at empty-state to only show Import / Samples / Run Checks.

## Acceptance Criteria Verification
| Criterion | Status |
|---|---|
| Clean first launch hides optional docks | ✅ Tested in `test_ui_workflow.cpp` |
| Auto-show on DesignLoaded | ✅ `set_scene`, `set_connectivity_graph`, `apply_import_package`, `load_bundled_sample` trigger it |
| Auto-show on ChecksRun | ✅ `JobWorkflowController::finalize_active_job` triggers it |
| `View → Panels` submenu | ✅ Tested in `test_ui_workflow.cpp` |
| User-driven hide overrides auto-show | ✅ Tested in `test_ui_workflow.cpp` |
| Headless tests cover | ✅ 3 new test cases, 35 assertions total |
