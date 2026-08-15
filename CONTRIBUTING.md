# Contributing to AEGIS-PERC

Thanks for looking at AEGIS-PERC. This is a solo, AI-assisted project (see
`orchestration/` for how development is tracked), so process here is
intentionally light — but a few things keep the codebase coherent.

## Where to look first

- **`README.md`** — what the project does, architecture diagram, feature
  list, and the "Repository Structure" section for a map of the tree.
- **`AGENTS.md`** — the operating rules this project (including its AI
  agents) is held to: scope discipline, module boundaries, concurrency,
  quality gates. Treat it as the source of truth for "is this change
  appropriate" questions; this file only summarizes it.
- **`orchestration/`** — `PROJECT_MEMORY.md` (stable facts), `DECISIONS.md`
  (architecture decisions, numbered `DEC-###`), `roadmap.yaml` /
  `tasks.yaml` (what's planned/in-progress), `runs/` (a dated log per
  completed task). If you're picking up work, skim `HANDOFF.md` first.

## Building

Full instructions live in `docs/BUILD.md` and the README's "Quick Build"
section. Short version:

```bash
# Windows
cmake --preset windows-release
cmake --build build/windows-release --config Release

# Linux / macOS
cmake --preset linux-release
cmake --build build/linux-release --config Release
```

Requires a C++20 compiler, CMake 3.25+, and Qt 6.5+ (Core/Widgets/Test).
Python 3.10+ is only needed for `scripts/validate_samples.py` and the
sample-design fixtures.

## Running tests

```bash
ctest --preset ci-test        # or windows-test / linux-test
```

Catch2 v3 auto-discovers tests per module (`tests/unit/<module>/`). Filter
by label or name, e.g. `ctest --test-dir build/windows-release -C Release -L rules`
or `-R SomeTestName`. New behavior should come with a new or updated test —
see `AGENTS.md` §6 (Quality Gates).

## Code style and architecture boundaries

AEGIS-PERC's module boundaries are load-bearing, not a suggestion. Summary
(full table and reasoning in `AGENTS.md` §2 and §9):

| Module | Owns | Must not depend on |
|---|---|---|
| `core` | Service registry, config, diagnostics | Qt Widgets |
| `parsing` | Layout/netlist import, `LayoutIR` | rule engine, ML, Qt Widgets |
| `graph` | Connectivity graph (device/net/pin) | Qt Widgets, ML |
| `rules` | Rule engine, violation detection | ML, Qt Widgets |
| `ml` | Feature extraction, inference | Qt Widgets, rule internals |
| `ui` | Windows, docks, canvas, panels | parser/rule internals, ML training |
| `scripting` | Python bindings, headless CLI | Qt Widgets internals |
| `reporting` | HTML/PDF/diagnostic export | Qt Widgets (prefer headless) |
| `storage` | Local DB, session/project persistence | Qt Widgets |

A few rules worth internalizing before sending a PR:
- **UI observes, core owns state.** Widgets never mutate analysis models
  directly.
- **Parser / rule engine / ML stay decoupled.** Core verification must run
  with no ML components loaded.
- **Graph engine has zero Qt dependency** — it's a pure C++20 library.
- **No new global/singleton state** without explicit justification (prefer
  dependency injection / the service registry).
- **Long-running operations support cancellation or progress reporting**;
  large-file parsing must not block the UI thread.
- **Preserve Qt6/C++20 compatibility and Windows-primary cross-platform
  design** — no unwrapped platform-specific APIs, no hardcoded absolute
  paths.
- Zero-tolerance warnings policy on CI/preset builds (`-Werror` / `/WX`) —
  see `docs/BUILD.md`.

## Proposing a change

- **Branch naming**: `feat/<short-slug>` for features, `fix/<short-slug>`
  for bug fixes, matching the pattern used in recent history (e.g.
  `feat/s3-004-product-quick-wins`). Never commit directly to `main`.
- **Keep changes PR-sized.** One task, one focused changeset — don't bundle
  unrelated fixes or pull forward work from a later sprint (`AGENTS.md` §1).
- **PR expectations**: state what changed and why, list the test commands
  you ran (and their results), and flag any deviation from acceptance
  criteria or known risk explicitly rather than silently. If your change
  touches shared orchestration files (`tasks.yaml`, `roadmap.yaml`,
  `DECISIONS.md`, `HANDOFF.md`), say so — those are meant to be a single
  source of truth, not duplicated per-branch.
- **Human review required.** Per `AGENTS.md` §10, autonomous/AI-assisted
  work is marked `review`, not `done`, in `orchestration/tasks.yaml` —
  only a human maintainer marks a task `done`.
- Architecture decisions (a new module dependency, a new third-party
  library, a change to a boundary in the table above) belong in a new
  `orchestration/DECISIONS.md` entry (`DEC-###`, sequential), not just a
  commit message.

## Data and licensing

- Sample/test fixtures live under `data/sample_designs/` (schema-validated
  synthetic designs — see `scripts/validate_samples.py`) and
  `data/import_packages/` (LEF/DEF/Verilog/rules fixtures used by the CLI
  and integration tests). Don't invent new fixtures if an existing one
  already covers the case you need.
- The project is MIT-licensed today (`LICENSE`); see
  `orchestration/DECISIONS.md` DEC-009 for an open, not-yet-accepted
  question about whether that should change.
