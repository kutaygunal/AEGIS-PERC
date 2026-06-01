# Run Log — 2026-05-22 — S1-003 (Waiver Application)

## Objective
Apply waivers to rule results during results assembly so waived findings are suppressed deterministically while remaining auditable in exports.

## What Changed
- Added waiver application engine:
  - `apply_waivers_in_place(violations, waivers)` tags each violation with:
    - `metadata.waived` (bool)
    - `waiver_match` ("identity_key" or "rule_id")
    - optional audit fields (`waiver_comment`, `waiver_owner`, `waiver_expires_on`, etc.)
  - Matching semantics are deterministic: identity_key (and optional rule_id) first, then rule_id-only waivers.
- Wired waiver parsing + application into:
  - CLI run path (`CliWorkflow::run_project`)
  - local job pipeline (`LocalJobPipeline` rule execution stage)
- Updated JSON report summaries (CLI + job pipeline) to include:
  - `active_violation_count`
  - `waived_violation_count`
- Added tests for identity-key matching, rule-id matching, and imported-design-derived identifiers.

## Files Touched
- `src/rules/include/aegis/rules/waivers.hpp`
- `src/rules/src/waivers.cpp`
- `src/scripting/src/cli_workflow.cpp`
- `src/orchestration/src/job_pipeline.cpp`
- `tests/unit/rules/test_waiver_application.cpp`
- `tests/CMakeLists.txt`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R WaiverApplication`

## Results
- Build: ✅
- `WaiverApplication` tests: ✅

