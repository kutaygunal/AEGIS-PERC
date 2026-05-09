# Run Log: P8-015 Cross-Probing

## Date
2026-05-08

## Task
`P8-015` — Add cross-probing between import artifacts, graph nodes, and violations.

## Files Changed
- `src/ui/include/aegis/ui/main_window.hpp`
  - Added test hooks for diagnostic/artifact cross-probing.
- `src/ui/src/main_window.cpp`
  - Added cross-probing actions from diagnostics and import artifacts to related violations.
  - Added violation navigation back to graph/import context using stable metadata.
- `src/ui/include/aegis/ui/violation_explorer_panel.hpp`
  - Added visible-ID selection helper and related-navigation signal/action support.
- `src/ui/src/violation_explorer_panel.cpp`
  - Added `Open Related Graph/Import Data` context action.
- `tests/unit/ui/test_ui_workflow.cpp`
  - Added cross-probing coverage for successful and unresolved navigation paths.
- `orchestration/tasks.yaml`
  - Marked `P8-015` as `review`.
- `orchestration/HANDOFF.md`
  - Updated project handoff state.

## Validation
```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -R UiWorkflow
```

## Results
- Build passed.
- `UiWorkflow` test selection passed.

## Notes
- Cross-probing uses stable identifiers where available: violation IDs, artifact IDs, artifact paths, net names, pin names, and device names.
- When current filters hide a related violation, the UI reports that no related visible violation was found instead of mutating filters implicitly.

## Status
review
