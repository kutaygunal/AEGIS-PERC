# Run Log: S2-001 — Declarative Condition Rules

**Date:** 2026-08-14
**Task:** S2-001 — Rules: declarative `type: condition` rule pack entries (field/operator/value checks)
**Commit:** (uncommitted at time of writing — see working tree)

## Goal
Close the top-priority gap from the 2026-08-14 commercial gap analysis (AEGIS-PERC vs. Calibre PERC-class tools): rule packs could only select and parameterize a fixed set of four built-in C++ rule classes, not define a new check without a recompile. Add a minimal declarative rule format on top of the existing `RuleEngine`/`RulePack` architecture, short of a full rule DSL, per the analysis's own suggested sequencing.

## Changes

### Core Implementation
- **`src/rules/include/aegis/rules/rule_pack.hpp`**
  - Added `RuleCondition` struct (`field`, `op`, optional `value`).
  - Extended `RulePackRuleDefinition` with `target`, `conditions`, `message`.
  - Documented the four supported rule pack `type`s on `RulePackLoader`.

- **`src/rules/src/rule_pack.cpp`**
  - Added `DeclarativeConditionRule` (`IRule`): iterates device/net/pin nodes, ANDs all `conditions`, emits a `Violation` with template or generated message.
  - Added field resolvers for device (`name`, `device_type`, arbitrary properties), net (`name`, arbitrary properties), pin (`name`, `direction`, `layer`, `x`, `y`).
  - Added operator evaluation: `equals`, `not_equals`, `greater_than`, `greater_or_equal`, `less_than`, `less_or_equal`, `contains`, `exists`, `not_exists`.
  - Extended JSON parsing (`rule_def_from_json`) to read `target`/`conditions`/`message`.
  - **Rewrote the YAML loader** (`load_yaml_rule_pack`) from a single-pass streaming state machine to a line-indexed parser (`tokenize_yaml_lines` + `parse_rule_item` + `parse_conditions_block`), so it can parse the nested `conditions:` list-of-maps in addition to the existing flat `parameters:` map. Behavior for existing rule packs (id/type/severity/description/parameters) is unchanged; verified via the pre-existing `test_rule_pack.cpp` suite.
  - Extended `validate_pack` to validate `condition`-type rules: `target` must be device/net/pin, `conditions` non-empty, each operator in the supported set, each comparison operator (all but exists/not_exists) requires a `value`.
  - Extended `instantiate_rules` to dispatch `type: condition` to `DeclarativeConditionRule`.
  - Minor DRY refactor: factored the parameter-value string→bool/int/double coercion (previously inline in `property_map_from_yaml_pairs`) into `coerce_yaml_scalar`, reused for condition `value` parsing.

### Tests
- **`tests/unit/rules/test_declarative_rules.cpp`** (new, 10 test cases):
  - JSON and YAML parsing of `type: condition` entries, including a pack mixing built-in and declarative rules.
  - Validation rejection: missing target, empty conditions, unsupported operator, missing value for a comparison operator; `exists` confirmed exempt from requiring a value.
  - Execution semantics: AND combination across net-target conditions, `exists`/`not_exists`, device-target fields (`device_type`, arbitrary property), pin-target fields (`direction`, `layer`), default message generation, and full end-to-end integration through `RuleEngine` alongside a built-in `floating_net` rule.
- **`tests/CMakeLists.txt`**: registered the new test file.

### Documentation
- **`docs/design/declarative_rules.md`** (new): schema, operators, field resolution, combination semantics, message templating, and explicit non-goals (no OR/NOT, no cross-node predicates, no geometry predicates).

### Orchestration
- **`orchestration/roadmap.yaml`**: added Sprint 2 ("Rule Engine Programmability") phase, sourced from the 2026-08-14 gap analysis.
- **`orchestration/tasks.yaml`**: added `S2-001` with acceptance criteria, implementation rules, test commands; status `review`.
- **`orchestration/DECISIONS.md`**: added `DEC-008` recording the incremental (declarative-first, not DSL-first) approach to rule programmability.
- **`orchestration/PROJECT_MEMORY.md`**: added a "Rule Engine" section documenting the four rule pack types and the YAML loader's scope.
- **`orchestration/HANDOFF.md`**: updated (see below).

## Test Results

```
cmake --build build/windows-release --config Release --target aegis_unit_tests
=> build succeeded, no warnings/errors in aegis_rules or the new test file

ctest --test-dir build/windows-release -C Release --output-on-failure -L rules
=> 100% tests passed, 0 tests failed out of 38 (10 new + 28 pre-existing)

ctest --test-dir build/windows-release -C Release --output-on-failure   (full suite)
=> 496/498 passed. 2 failures, both pre-existing and unrelated to this change:
   - Test #156 WorkspacePersistence ... reopen-last-session preference (SEGFAULT)
   - Test #158 WorkspacePersistence ... named filter presets and workspace views (SEGFAULT)
```

## Known Issues / Notes
- **Pre-existing UI segfaults (#156, #158)**: both are in `WorkspacePersistence`, an async import-restore race condition already documented as pre-existing in the S1-014 run log (`run_2026-06-01_s1-014-first-launch-simplification.md`) for #156. This change touches only `src/rules/*`, `tests/unit/rules/*`, `tests/CMakeLists.txt`, and `docs/design/*` — there is no code path from this change into UI/workspace-persistence, so these were not re-verified via `git stash` this session (unlike S1-014, which did do that check because the change *did* touch UI code). Still open as a separate bug-fix item.
- **Scope boundary**: this is deliberately *not* a rule DSL. No OR/NOT combinators, no cross-node predicates (e.g. "net driven by >1 output pin" stays a purpose-built rule), no geometry-based predicates (blocked on GDSII/OASIS ingestion — a separate, larger gap). See docs/design/declarative_rules.md "Non-goals".
- **Existing sample rule packs unchanged**: `data/import_packages/*/rules/aegis_rules.yaml` were not modified — this is a new capability, not a requirement on existing packages.

## Post-PR CI Fixups (2026-08-14, same day)
PR #1's `linux-release` / `linux-coverage` / `linux-sanitizers` checks failed. Root-caused as **two pre-existing bugs already on `main`** (confirmed by checking the last several CI runs on `main`, all failing the same way since at least 2026-05-21) — neither introduced by S2-001, but fixed here since they blocked the PR:

1. **`-Wsign-conversion` on `for (unsigned char x : some_string)`** in `src/rules/src/waivers.cpp` and `src/rules/src/violation.cpp` (3 call sites). GCC flags the implicit `char`→`unsigned char` conversion in the range-for element declaration itself. Fixed by iterating `char` and casting explicitly (`static_cast<unsigned char>(...)`) only at the point of use (`std::isspace`, `std::isalnum`, `%02X` formatting) — matches how `rule_pack.cpp`'s existing `to_lower`/`trim` already do it via lambda parameters.
2. **`gmtime_s` unconditionally used** in `src/storage/src/session_cache.cpp:147` — an MSVC-only "secure CRT" function, not available under glibc (Linux). Fixed by adding the same `#ifdef _WIN32 ... gmtime_s ... #else ... gmtime_r ... #endif` guard already used correctly in `src/core/src/diagnostic_bundle.cpp`.

**Verification:** since Qt6 isn't installed in this environment and CI needed the *exact* `linux-release` preset flags (`-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wconversion -Wsign-conversion -Werror`), verification used WSL Ubuntu (GCC 13.3.0, matching CI's Ubuntu runner exactly) with portable `cmake`/`ninja` binaries (no `sudo`/apt access available) against a scratch copy of the repo with only the Qt-dependent `ui` subdirectory and the `aegis-perc` GUI target excluded. Result: **every non-UI module (core, parsing, graph, rules, ml, scripting, reporting, storage, orchestration) plus `aegis-perc-cli` builds clean** with zero errors/warnings under the real CI compiler and flags. `ui` itself could not be verified this way (no Qt6 available without `sudo apt`); a repo-wide grep confirmed no further occurrences of either bug pattern anywhere under `src/` or `app/`, including `ui`. Windows build + full `rules`/`storage` test suites (51 tests) re-verified green after the fix.

## Post-PR CI Fixups, part 2: the "pre-existing WorkspacePersistence segfault" was two bugs, not one
Windows CI kept failing on `WorkspacePersistence` tests #156/#158, previously documented (S1-014 run log, HANDOFF.md) as one pre-existing, unrelated flaky segfault. Investigating properly (Windows Debugging Tools `cdb.exe`, a Debug build with real PDB symbols) showed this is actually **two distinct bugs**:

### Bug A — fixed here: use-after-free on `MainWindow` teardown (test #156, and the deterministic part of #158)
Root cause: `MainWindow`'s only direct member is `std::unique_ptr<Impl> m_impl` (PIMPL). C++ destroys a derived class's own members *before* running base-class destructors, so `m_impl` — and with it `Impl::loaded_import_session` — was destroyed *before* `QMainWindow`'s base-class destructor tears down child widgets. `HierarchyBrowserPanel`/`PropertiesPanel` (Qt-owned children, torn down later) hold a raw, non-owning `const ImportedDesignSession*` into that session. A deferred Qt event during window close (a `QTreeView` layout/paint pass observed in the crash stack: `QWidget::~QWidget → QWindow::close → … → QHeaderView::resizeSections → QStyledItemDelegate::sizeHint → QAbstractItemModel::multiData → QSortFilterProxyModel::data → HierarchyBrowserModel::object_at`) then dereferenced the dangling pointer — confirmed via a `0xFEEEFEEE`-patterned (MSVC debug-heap "freed") value at the fault address.

**Fix** (`src/ui/src/main_window.cpp`): gave `MainWindow::~MainWindow()` an explicit body that calls `hierarchy_browser->set_session(nullptr)` / `properties_panel->set_session(nullptr)` *first*, before `m_impl` (and thus the session) is destroyed. `HierarchyBrowserModel::set_session(nullptr)` already does a proper `beginResetModel()/endResetModel()`, so this safely invalidates any in-flight model state.

**Verification:** reproduced under `cdb.exe` with a symbolized Debug build (exact stack trace above), confirmed fixed (10/10 clean runs across Debug), then the full local Windows suite went **498/498 passing** — the first fully green run of this repo's test suite across every historical CI run checked (back to 2026-05-21).

### Bug B — found, not fixed: a second, timing-sensitive crash in test #158 only
While hunting Bug A, `WorkspacePersistence persists named filter presets and workspace views across restart` (test #158) kept crashing intermittently *even after Bug A's fix*, including on a run with **no import/session data involved at all** — so it cannot be the same root cause.

Characterized as thoroughly as available tooling allowed:
- **Debug build**: 0/15 crashes across two batches (Debug + Debug-under-`cdb`).
- **Release build, standalone (no debugger)**: ~7/8 crashes.
- **RelWithDebInfo, standalone**: ~4/6 crashes.
- **RelWithDebInfo, *under* `cdb`**: 0/15 crashes — the debugger's presence itself suppresses the bug (classic heisenbug: timing- or memory-layout-sensitive undefined behavior, e.g. an uninitialized read or a race, where the debugger's overhead changes the outcome).
- AddressSanitizer (`windows-asan-debug` preset) was attempted but did not actually instrument the binary on this toolchain (`/fsanitize=address` was silently dropped by the linker — `LNK4044`); Application Verifier page-heap (`gflags.exe`) was attempted but requires admin rights not available in this environment, so it was not force-escalated.

**Not fixed.** I could not get a debugger-confirmed stack trace for this one, so I'm not willing to guess at a fix. Given `linux-sanitizers` is a real CI job (ASan/UBSan on Linux) that this PR's other fixes should now let run to completion for the first time, that CI job may catch this with a clean, precise report — worth checking before spending more local time on it. Recorded here rather than silently left as "still flaky" so the next session doesn't have to rediscover this from scratch.

## Acceptance Criteria Verification
| Criterion | Status |
|---|---|
| `type: condition` with target + conditions, no new C++ class | ✅ `test_declarative_rules.cpp` |
| 9 operators supported | ✅ exercised across tests (equals/not_equals/contains/greater_than/less_than/exists/not_exists directly; greater_or_equal/less_or_equal share the same code path) |
| Conditions ANDed | ✅ "flags nets matching all ANDed conditions" test |
| Field resolution for device/net/pin | ✅ dedicated test per target |
| Message templating + default fallback | ✅ two dedicated tests |
| Validation rejects malformed entries | ✅ "rejects malformed declarative condition rules" test |
| Existing rule types unaffected | ✅ full `rules` label suite (38/38) passes |
| Documented | ✅ `docs/design/declarative_rules.md` |
