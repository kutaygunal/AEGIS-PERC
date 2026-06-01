# Run Log — 2026-05-22 — S1-001 (Violation Identity Key)

## Objective
Introduce a stable, deterministic identity key for violations to enable waivers and regression diffs.

## What Changed
- Added `aegis::rules::Violation::identity_key()` (versioned `v1|...`) based on:
  - `rule_id`
  - structured location fields (`layer`, `net_name`, `pin_name`, `device_name`)
  - optional `point` quantized to 1e-3 units
- JSON serialization now includes `identity_key` for each violation.
- Added contract documentation in `docs/design/signoff_workflow.md`.
- Added unit tests covering stability across message/id changes and point quantization behavior.

## Files Touched
- `src/rules/include/aegis/rules/violation.hpp`
- `src/rules/src/violation.cpp`
- `tests/unit/rules/test_violation_model.cpp`
- `docs/design/signoff_workflow.md`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R ViolationIdentity`

## Results
- Build: ✅
- `ViolationIdentity` tests: ✅

## Notes
- The identity contract intentionally ignores `message` and `id` so cosmetic text changes do not break waivers/baselines.
- Follow-on work (`S1-002`/`S1-003`) should match waivers on `identity_key` (and optionally `rule_id`) and keep an audit trail for waived vs active findings.

