# Run Log: P8-013 Workspace Presets

## Date
2026-05-08

## Task
`P8-013` — Persist named filter presets and saved workspace views.

## Files Changed
- `src/ui/include/aegis/ui/main_window.hpp`
  - Added APIs for saving/applying/renaming/deleting named filter presets and workspace views.
- `src/ui/src/main_window.cpp`
  - Persisted named presets/views in existing workspace `QSettings` storage.
  - Added View/Tools menu entry points for saving and managing presets/views.
  - Added restore/apply logic for dock-layout snapshots and key workspace toggles.
- `tests/unit/ui/test_violation_filter.cpp`
  - Added preset CRUD/apply coverage.
- `tests/unit/ui/test_workspace_persistence.cpp`
  - Added restart-persistence coverage for named presets/views.
- `orchestration/tasks.yaml`
  - Marked `P8-013` as `review`.
- `orchestration/HANDOFF.md`
  - Updated current project handoff state.

## Validation
```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -R WorkspaceState
ctest --test-dir build -C Release --output-on-failure -R ViolationFilter
build/tests/Release/aegis_unit_tests.exe [WorkspacePersistence]
build/tests/Release/aegis_unit_tests.exe [ViolationFilter]
```

## Results
- Build passed.
- `WorkspaceState` ctest selection passed.
- `ViolationFilter` ctest selection was blocked by a pre-existing generated CTest parse error in `build/tests/aegis_unit_tests-f1069ee_tests-Release.cmake`.
- Direct Catch2 fallback runs passed for both relevant tags:
  - `[WorkspacePersistence]`
  - `[ViolationFilter]`

## Notes
- Named presets and named workspace views extend the existing `mainWindow/workspaceUi` persistence branch instead of introducing a parallel store.
- Serialization remains human-inspectable through `QSettings` variant maps/lists.

## Status
review
