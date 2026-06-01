# Handoff

## Last Updated
2026-06-01

## Current Status
S1-014 first-launch workspace simplification is complete and under review.

## Last Completed Task
S1-014 — UI: simplify first-launch workspace (hide optional docks + toolbar groups)

## Current Task
No implementation task in progress.

## Ready Tasks
- `S1-009` ? UI: violation explorer supports 'Active vs Waived' filter pill

## Blocked Tasks
- None recorded.

## Important Notes
- README source-code synchronization (2026-05-23) is still valid.
- S1-014 implementation:
  - `WorkspaceState` enum (`Empty`, `DesignLoaded`, `ChecksRun`) drives default dock visibility.
  - On clean first launch (no QSettings): only Workspace Summary, Log, and the center canvas are visible.
  - 8 optional docks are hidden: Layers, Graph Explorer, Hierarchy, Trace, Report Preview, Violations, Diagnostics, Jobs.
  - DesignLoaded state reveals Layers/Graph Explorer/Hierarchy/Properties/Trace (triggered by `set_scene`, `set_connectivity_graph`, `apply_import_package`, `load_bundled_sample`).
  - ChecksRun state reveals Report Preview/Violations/Diagnostics/Jobs (triggered by `JobWorkflowController::finalize_active_job`).
  - User-driven dock hides are tracked per-session in `user_hidden_docks` (in-memory); auto-show respects these overrides.
  - View menu now groups dock toggles under `View → Panels`.
  - Tests: `ctest --test-dir build/windows-release -C Release -R UiWorkflow` passes (32/32 UI tests).
  - Pre-existing segfault in test #156 (WorkspacePersistence persists recent imported projects) confirmed unrelated.

## Next Recommended Action
Start `S1-009` by adding an "Active/Waived" toggle in the violation explorer filter model so users can hide waived findings in the main table without changing exports.
