# Run Log: P8-014 Onboarding and Empty-State Actions

## Date
2026-05-08

## Task
`P8-014` — Add first-run onboarding and guided empty-state actions.

## Files Changed
- `src/ui/include/aegis/ui/main_window.hpp`
  - Added test accessors for onboarding visibility/text/dismissal.
- `src/ui/src/main_window.cpp`
  - Added a non-modal onboarding panel in the Workspace Summary area.
  - Wired direct actions to import, bundled samples, run checks, and documentation.
  - Persisted onboarding dismissal with `QSettings`.
- `tests/unit/ui/test_ui_states.cpp`
  - Added onboarding visibility, documentation reachability, and dismissal persistence coverage.
- `tests/unit/ui/test_sample_workflow.cpp`
  - Added onboarding guidance checks in the empty-workspace sample workflow.
- `orchestration/tasks.yaml`
  - Marked `P8-014` as `review`.
- `orchestration/HANDOFF.md`
  - Updated current handoff state.

## Validation
```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure -R UiStates
ctest --test-dir build -C Release --output-on-failure -R SampleWorkflow
```

## Results
- Build passed.
- `UiStates` test selection passed.
- `SampleWorkflow` test selection passed.

## Notes
- Onboarding reuses the existing Workspace Summary surface so it remains non-modal after dismissal.
- Guidance text explicitly points users toward sample loading, import review, documentation, and the run-check workflow.

## Status
review
