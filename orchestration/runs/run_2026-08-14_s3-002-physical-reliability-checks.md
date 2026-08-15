# Run Log: S3-002 — Physical/Reliability Checks (Antenna Ratio, first increment)

**Date:** 2026-08-14
**Task:** S3-002 — one real physical/reliability rule check (first increment), Sprint 3 (not yet registered in `roadmap.yaml`/`tasks.yaml` per this task's constraints — a separate consolidation pass owns those files)
**Branch:** `feat/s3-002-physical-reliability-checks`

## Goal
`roadmap.yaml`'s Sprint 2 notes listed physical/reliability check types (antenna, guard-ring presence) as a "follow-on, not yet scheduled" gap, annotated as blocked on "geometry data" existing — implicitly GDSII/OASIS ingestion. Per the task brief, this was re-verified against the actual code rather than taken at face value before scoping the work.

## Finding: the "blocked on geometry data" premise, checked against the code
LEF/DEF ingestion (already implemented, already shipping) does carry real physical data into the pipeline:
- `src/parsing/src/lef_parser.cpp` / `def_parser.cpp` populate `LayoutIR::geometries` (real `Rectangle`/`Polygon` shapes with layer), `Port::location`/`Port::layer` (DEF `PINS` placement), and `Device::properties["x"/"y"]` (DEF `COMPONENTS` placement).
- `src/parsing/src/spice_parser.cpp`'s `M`-element (MOSFET) parsing captures arbitrary `key=value` SPICE parameters — including real transistor gate sizing (`W`/`L`) when the netlist specifies them — into `Device::properties`.
- `ConnectivityGraph::from_layout_ir()` (`src/graph/src/connectivity_graph.cpp`) carries `Device::properties` straight into `DeviceNode::properties`, and external port placement (`x`/`y`/`layer`) into `PinNode`.

So GDSII/OASIS ingestion (the parallel S3-001 task) is **not** required to build a first physical check — confirmed by reading the code, not by trusting the roadmap note.

One real gap *does* exist, and is documented explicitly rather than worked around: `ConnectivityGraph::from_layout_ir()` never consumes `LayoutIR::geometries` — raw routing shapes are parsed and preserved on the session's `LayoutIR`, but nothing associates a shape with an owning net, and no stage aggregates shape area by net. A *complete* antenna check (real accumulated routed-metal polygon area per net) is therefore not buildable from today's graph. This is named as the primary non-goal below rather than faked.

## Check implemented: Antenna Ratio (`type: antenna_ratio`)
Chosen per the task's own suggested default (most commonly requested EDA physical check) and because it maps directly onto data that is genuinely on the graph today:
- **Gate area** (real): computed per net by walking `DeviceToPin` edges with `terminal_name == "gate"` (configurable) to find connected MOSFETs, then reading `DeviceNode::properties["w"]` × `["l"]` (or a direct `gate_area_um2` override) — real values when SPICE ingestion supplied them.
- **Accumulated metal area** (property-map read, same pattern as `em_current_limit`): read from `NetNode::properties["metal_area_um2"]` (configurable key). **No existing importer populates this property today** — stated explicitly in the design doc rather than silently assumed. The rule reads whatever is present, exactly like `CurrentLimitRule` reads `current_mA` without caring how it got there (that, too, comes from a separate enrichment stage — current-activity import — not computed inline).

Guard-ring presence was considered and rejected as the first check: it needs proximity/enclosure geometry that has zero representation anywhere in `ConnectivityGraph` today, so it could not be implemented without fabricating a data path. Antenna ratio does not require that — every field it reads either already exists on the graph or is read via the pack's existing "read a property, don't care how it got there" convention.

## Changes

### Design doc
- **`docs/design/physical_reliability_checks.md`** (new): overview, the geometry-availability finding above, why antenna ratio over guard-ring presence, exact data sourcing table, parameterization schema, execution semantics, and an explicit non-goals list (net-level metal-area aggregation from raw geometry, diode-count credit, multi-layer stacked accumulation, guard-ring presence, DRC spacing/width checks, auto-fix).

### Core implementation
- **`src/rules/include/aegis/rules/physical_rules.hpp`** (new) — declares `AntennaRatioRule : IRule`, constructed directly from a `RulePackRuleDefinition` (same shape as `CurrentLimitRule`/`em_current_limit`, not the parameterless `ConfiguredRuleAdapter` path used by `floating_net`/`power_domain_mismatch`).
- **`src/rules/src/physical_rules.cpp`** (new) — implementation:
  - Per net: walks `NetToPin` → `DeviceToPin` edges to find gate-connected devices, sums gate area (`gate_area_um2` override, else `w * l`), reads net `metal_area_um2`, flags when `metal_area / gate_area_sum > max_ratio` (strict `>`, matching `em_current_limit`'s semantics — exactly-at-threshold is not flagged).
  - Missing/unparsable numeric data (device `w`/`l`, `gate_area_um2`, `metal_area_um2`) is treated as non-triggering for that node/net, never as a crash or fatal violation — matches the existing convention in `CurrentLimitRule` and `DeclarativeConditionRule`.
  - All field-name parameters (`gate_terminal`, `gate_width_field`, `gate_length_field`, `gate_area_field`, `metal_area_field`) are configurable via rule-pack `parameters`, with sane defaults.
- **`src/rules/src/rule_pack.cpp`** / **`rule_pack.hpp`**: added `antenna_ratio` to the supported-type set, `validate_pack` (requires numeric `max_ratio` parameter, mirroring `em_current_limit`'s `*_max_mA` requirement), `instantiate_rules` dispatch, `default_rule_type_for_id` symmetry entry, and updated the `RulePackLoader` doc comment listing built-in types.
- **`src/rules/CMakeLists.txt`**: registered `physical_rules.hpp`/`physical_rules.cpp` as a new translation unit (kept separate from `electrical_rules.cpp`/`rule_pack.cpp` per the task's explicit instruction).

### Tests
- **`tests/unit/rules/test_physical_rules.cpp`** (new, 18 Catch2 test cases):
  - Core execution: empty graph, threshold exceeded/not-exceeded, exact-threshold boundary (not flagged), multi-device gate-area summation, independent multi-net evaluation.
  - Missing/edge-case data: net with no `metal_area_um2` (skipped), net with no gate-connected devices (skipped), a gate-connected device missing `w`/`l` entirely (contributes zero, no crash), unparsable `w` value (non-triggering, no crash), unparsable `metal_area_um2` (non-triggering, no crash).
  - Parameterization: `gate_area_um2` override wins over `w*l`; all four field-name override parameters (`gate_terminal`, `gate_width_field`, `gate_length_field`, `metal_area_field`) respected together.
  - Metadata: `id()`/`category()` (`"physical"`)/`description()`.
  - `RulePackLoader` integration: YAML parse, JSON parse, validation rejection when `max_ratio` is missing, and full `instantiate_rules` → `RuleEngine::run_all` end-to-end alongside a `floating_net` rule in the same pack.
- **`tests/CMakeLists.txt`**: registered the new test file.

## Files touched
```
 docs/design/physical_reliability_checks.md          (new)
 src/rules/include/aegis/rules/physical_rules.hpp    (new)
 src/rules/src/physical_rules.cpp                     (new)
 tests/unit/rules/test_physical_rules.cpp             (new)
 src/rules/include/aegis/rules/rule_pack.hpp          (modified: doc comment)
 src/rules/src/rule_pack.cpp                           (modified: antenna_ratio wiring)
 src/rules/CMakeLists.txt                              (modified: new sources)
 tests/CMakeLists.txt                                  (modified: new test file)
 orchestration/runs/run_2026-08-14_s3-002-physical-reliability-checks.md (new, this file)
```
No changes to `src/parsing/`, `src/graph/`, or `src/ui/` — the required geometry/property fields (`DeviceNode::properties`, `NetNode::properties`, the existing `DeviceToPin`/`NetToPin` edge structure) already existed and were sufficient; no additive field was needed. `orchestration/tasks.yaml`, `orchestration/roadmap.yaml`, `orchestration/HANDOFF.md`, `orchestration/DECISIONS.md`, and `README.md` were **not** modified, per this task's explicit constraint (a separate consolidation pass owns those).

## Build & Test Results

Environment: Windows, MSVC 19.44 (VS 2022 Community) via the `windows-release` CMake preset (Visual Studio 17 2022 generator, x64), Qt 6.8.2 at `C:/Qt/6.8.2/msvc2022_64`. `FETCHCONTENT_BASE_DIR` was pointed at the sibling main-repo build's already-fetched `_deps` (Catch2/spdlog/nlohmann_json/zip) to avoid a redundant network fetch; sources were unmodified, only reused.

```
cmake --preset windows-release -DFETCHCONTENT_BASE_DIR=<main-repo>/build/windows-release/_deps
=> Configure succeeded (Qt6 found, MSVC 19.44 detected)

cmake --build build/windows-release --config Release --target aegis_unit_tests
=> Build succeeded. aegis_rules (including physical_rules.cpp) compiled clean, no warnings/errors,
   under /W4 /permissive- with AEGIS_WARNINGS_AS_ERRORS=ON.

cmake --build build/windows-release --config Release --target aegis-perc-cli
=> Build succeeded (needed separately for the BatchCLI subprocess tests below).

ctest --test-dir build/windows-release -C Release --output-on-failure -L PhysicalRules
=> 100% tests passed, 0 failed out of 18 (all new)

ctest --test-dir build/windows-release -C Release --output-on-failure -L ElectricalRules
=> 100% tests passed, 0 failed out of 26 (pre-existing, unaffected)

ctest --test-dir build/windows-release -C Release --output-on-failure   (full suite, 516 tests)
=> 99% tests passed, 1 failed: Test #158 "WorkspacePersistence persists named filter presets
   and workspace views across restart" (SEGFAULT).
```

515/516 passing. The one failure is **pre-existing and already documented**, unrelated to this change: `orchestration/runs/run_2026-08-14_s2-001-declarative-condition-rules.md` ("Bug B — found, not fixed") characterizes this exact test as a timing-sensitive heisenbug (~70-90% standalone reproduction, suppressed under a debugger) unrelated to any rules code — this task touched only `src/rules/*`, `tests/unit/rules/test_physical_rules.cpp`, `tests/CMakeLists.txt`, `src/rules/CMakeLists.txt`, and `docs/design/*`, none of which are on any code path into `WorkspacePersistence`/UI state restore. Not re-investigated here; still open as a separate, previously-tracked bug-fix item.

An earlier full-suite run (before `aegis-perc-cli` was built) additionally showed all 4 `BatchCLI`/`aegis-perc-cli` scripting tests failing with "The system cannot find the path specified." — this was purely an artifact of only having built the `aegis_unit_tests` target (those tests spawn `aegis-perc-cli.exe` as a subprocess, which didn't exist yet); building the `aegis-perc-cli` target resolved all four with no code changes, confirming they were not a real regression.

## Acceptance / scope checklist
| Item | Status |
|---|---|
| Confirmed via code (not roadmap note) whether LEF/DEF geometry is sufficient | Done — see "Finding" above; partially sufficient, gap documented explicitly |
| One real physical/reliability check implemented | AntennaRatioRule (`type: antenna_ratio`) |
| New `IRule` subclass, separate translation unit from `electrical_rules.*` | `src/rules/include/aegis/rules/physical_rules.hpp` + `src/rules/src/physical_rules.cpp` |
| Wired into `rule_engine`/`rule_pack` selection/parameterization | `rule_pack.cpp`/`rule_pack.hpp`, same mechanism as `em_current_limit` |
| Registered in `src/rules/CMakeLists.txt` | Done |
| Design doc matching `declarative_rules.md` style, with non-goals | `docs/design/physical_reliability_checks.md` |
| Tests: triggers, non-triggers, threshold parameterization, missing-data edge cases | `tests/unit/rules/test_physical_rules.cpp`, 18 cases |
| No `rules` → ML/Qt Widgets dependency introduced | Confirmed — `physical_rules.{hpp,cpp}` only include `rule_engine.hpp`/`rule_pack.hpp`/`<sstream>`/`<optional>`/etc. |
| No edits to `src/parsing/`, `src/ui/`, `src/graph/` | Confirmed — none needed |
| No edits to `tasks.yaml`/`roadmap.yaml`/`HANDOFF.md`/`DECISIONS.md`/`README.md` | Confirmed — read-only this session |
| Build + full test suite run, results reported honestly | Done — 515/516, one documented pre-existing failure |

## Known limitations / explicitly deferred (see design doc "Non-goals" for full detail)
- **No net-level metal-area aggregation from raw LEF/DEF/GDSII polygon geometry.** This is the main honest gap: `LayoutIR::geometries` exists but nothing associates a shape with an owning net, and `ConnectivityGraph::from_layout_ir()` doesn't consume it. Until a future enrichment stage (analogous to the existing current-activity enrichment for `current_mA`) populates `metal_area_um2` on real imports, `AntennaRatioRule` will not fire on data imported from raw LEF/DEF alone — it is fully functional and tested, and requires no changes to work the moment that data exists.
- **No diode-count antenna-protection credit.**
- **No multi-layer stacked antenna accumulation** (real signoff tools track ratio incrementally per routing layer; this is a single net-level ratio against one accumulated-area figure).
- **Guard-ring presence not implemented this round** — considered and rejected as the first check because no proximity/enclosure geometry exists in the graph today.
- **No DRC-style spacing/width checks** — different check family, would also need the geometry-aggregation gap closed first.
- **Not an auto-fixer** — detector only, per AGENTS.md §5 (no destructive automated fix without explicit user confirmation).

## Next recommended action
Two independent options for a follow-on task:
1. Build the geometry→net area-aggregation enrichment stage (`LayoutIR::geometries` → per-net `metal_area_um2`, keyed by net ownership resolved from routing connectivity) so `AntennaRatioRule` fires on real LEF/DEF imports without further rule-code changes. This is the natural, larger next step and would touch `src/parsing/` and/or `src/graph/`.
2. A second physical/reliability check type (guard-ring presence, once/if proximity geometry becomes representable in the graph) or DRC-style spacing/width checks, once the same aggregation gap is closed.

This task (S3-002) and its parent Sprint 3 registration are not yet reflected in `orchestration/tasks.yaml`/`roadmap.yaml` — per this task's explicit constraint, a separate consolidation pass should add them alongside marking this run's task `review` (not `done`), consistent with DEC-003.
