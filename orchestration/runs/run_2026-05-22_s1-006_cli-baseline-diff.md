# Run Log — 2026-05-22 — S1-006 (CLI Baseline + Diff)

## Objective
Add CLI subcommands to:
- write a baseline snapshot after running checks
- diff current results against a baseline with regression-oriented exit codes

## What Changed
- CLI interface (`aegis-perc-cli`):
  - Added `baseline` command to emit baseline JSON (`baseline_schema_version`, `records`).
  - Added `diff` command with `--baseline FILE` to emit diff JSON and return an explicit regression exit code.
  - Added parsing for `--baseline` flag.
- Scripting workflow:
  - Added `CliWorkflow::baseline_project(...)` and `CliWorkflow::diff_project(...)`.
  - Added `CliExitCode::SuccessWithRegressions = 5`.
- Tests:
  - Added BatchCLI regression test covering:
    - baseline generation
    - diff against same inputs (no regressions)
    - diff after introducing an additional floating net (regression exit code 5)

## Files Touched
- `app/cli_main.cpp`
- `src/scripting/include/aegis/scripting/cli_workflow.hpp`
- `src/scripting/src/cli_workflow.cpp`
- `tests/unit/scripting/test_batch_cli.cpp`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R "aegis-perc-cli"`

## Results
- Build: ✅
- CLI tests: ✅

