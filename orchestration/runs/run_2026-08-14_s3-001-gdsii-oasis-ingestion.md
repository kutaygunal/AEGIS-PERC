# Run Log: S3-001 — GDSII/OASIS Ingestion (design + first increment)

**Date:** 2026-08-14
**Task:** S3-001 — GDSII/OASIS layout ingestion: design doc + a first native GDSII parser increment
**Branch:** `feat/s3-001-gdsii-oasis-ingestion` (uncommitted at time of writing this section — see commit for final SHA)

## Goal
Close the largest unscheduled gap flagged by the 2026-08-14 commercial gap
analysis (see `HANDOFF.md`, `roadmap.yaml` Sprint 2 notes): AEGIS-PERC has
no way to ingest raw mask-level polygon data (GDSII/OASIS), which blocks
every physical/reliability rule check from ever being built. This is the
first task of a new, not-yet-registered Sprint 3; no prior agent has
touched this area. Followed the same incremental-delivery philosophy as
DEC-008 (declarative condition rules): implement a real, useful record
subset now, document what's deliberately deferred, avoid speculative
generality (hierarchy, OASIS) before there's a concrete consumer for it.

## Changes

### Design documentation
- **`docs/design/gdsii_oasis_ingestion.md`** (new): GDSII binary record
  header layout, the GDSII 8-byte "real" (excess-64, base-16) encoding used
  by `UNITS`, the exact record subset implemented, the `LayoutIR` mapping
  (including an explicitly documented limitation: individual `Geometry`
  entries are not attributed back to their owning GDSII structure, since
  `LayoutIR` has no such field and this increment does not extend that
  schema), a diagnostics-code table (mirroring `docs/design/lef-coverage.md`
  conventions), and a "Non-goals" section scoping **OASIS out entirely**
  for this increment (with the specific reasons it's materially harder:
  varint-based delta encoding, compressed cell hierarchy, repetition
  primitives baked into the grammar) plus `PATH`, `SREF`/`AREF`, `TEXT`,
  and all other unhandled GDSII record types.

### Core implementation
- **`src/parsing/include/aegis/parsing/gdsii_parser.hpp`** (new):
  - `gdsii_record` namespace: named constants for the record-type and
    data-type byte codes this parser understands (`HEADER`, `BGNLIB`,
    `LIBNAME`, `UNITS`, `BGNSTR`, `STRNAME`, `BOUNDARY`, `LAYER`,
    `DATATYPE`, `XY`, `ENDEL`, `ENDSTR`, `ENDLIB`), plus the four data-type
    codes actually used (`INT16`, `INT32`, `REAL64`, `ASCII`).
  - `GdsiiDiagnostic` (severity/code/message/byte_offset) — parallels
    `LefDiagnostic`, but with a byte offset instead of a line number since
    GDSII is binary.
  - `GdsiiBoundary` / `GdsiiStructure` / `GdsiiLibraryData` — a typed
    intermediate model parallel to `LefLibraryData`. Reuses
    `aegis::parsing::Point`/`Polygon` directly from `layout_ir.hpp` for
    boundary geometry — no parallel geometry type was introduced.
  - `GdsiiParser : public IParser` — implements the same streaming
    callback contract as `DefParser` (`parse()`), plus a typed
    `parse_to_library()` (parallels `LefParser::parse_string`/`parse_file`)
    and a `parse_to_layout_ir()` convenience conversion (parallels
    `DefParser::parse_to_layout_ir()`).
- **`src/parsing/src/gdsii_parser.cpp`** (new): the binary record walker.
  Reads one 4-byte record header + payload at a time from an `ifstream`
  opened in binary mode (no whole-file buffering), decodes big-endian
  int16/int32 and the GDSII 8-byte real format, tracks
  structure/element nesting state, and emits `GdsiiDiagnostic`s (recorded
  internally, and forwarded through `IParserCallbacks::on_error` when a
  callback is present) instead of throwing or crashing on malformed input.
  Cancellation is polled every 16 records. Unsupported record types are
  skipped and warned about, deduplicated per record type so a real,
  `PATH`-heavy design doesn't flood the diagnostics list.
- **`src/parsing/CMakeLists.txt`**: registered the new header/source pair.

### Tests
- **`tests/unit/parsing/test_gdsii_parser.cpp`** (new, 13 test cases):
  builds real synthetic GDSII binary streams in-test via a small
  `GdsBuilder` helper (byte-level record construction, including an
  independent implementation of the GDSII 8-byte real encoder — written
  from the public format spec, not derived from the parser's own decoder,
  so the tests double as a correctness check on that decoder rather than a
  round-trip tautology). Covers:
  - `format_name()`, default `has_errors()`.
  - A minimal valid stream round-trips into the expected typed
    `GdsiiLibraryData` (library name, version, `UNITS` scale, structure
    name, boundary layer/datatype/points, including verifying the closing
    duplicate vertex is dropped) and into the expected `LayoutIR`
    (design name, one `Layer` named `L1D0`, one `Geometry` polygon).
  - Streaming `parse()` emits `on_cell` (one per structure) and
    `on_geometry` (one per boundary) events correctly.
  - Malformed record length (`< 4` or odd) → `GDS_RECORD_LENGTH_INVALID`
    diagnostic, `parse()` returns `false`, no crash.
  - Truncated record payload → `GDS_RECORD_TRUNCATED` diagnostic, no
    crash.
  - Unsupported record types (`PATH`, `TEXT`) are skipped with a
    `GDS_UNSUPPORTED_RECORD` **Warning** (parsing still succeeds), and a
    repeated unsupported record type is only warned about once
    (deduplication).
  - A degenerate boundary (fewer than 3 vertices) is dropped with a
    `GDS_BOUNDARY_INVALID` warning rather than corrupting the structure.
  - Cancellation: a 200-structure synthetic stream is cancelled partway
    through via the progress callback (mirrors the existing
    `LefParser` cancellation test pattern); parsing stops well before all
    200 structures complete, with a `GDS_PARSE_CANCELLED` diagnostic.
  - Missing `ENDLIB` is a non-fatal warning.
  - Missing file → diagnostic, not an exception, from `parse()`.
  - `parse_to_layout_ir()` throws `LayoutIRError` on a fatal parse
    failure.
- **`tests/CMakeLists.txt`**: registered the new test file in the
  `aegis_unit_tests` executable's source list (this repo does not
  auto-discover test files; `catch_discover_tests` only auto-discovers
  *test cases* within the already-registered executable — matches the
  existing pattern for every other parser test file).

## Explicit scope boundary (see design doc "Non-goals" for full detail)
- **OASIS is not implemented at all in this increment** — a dedicated,
  deferred follow-on. Reasoning (materially more complex than GDSII, not
  a small extension of this reader): compressed/delta cell-hierarchy
  encoding, variable-length integer encoding throughout, repetition
  ("array reference") compression built into the format grammar itself,
  string interning tables, optional CRC trailer.
- GDSII record types **not** implemented: `PATH` (routed wire geometry),
  `SREF`/`AREF` (cell instance references — the actual hierarchy/placement
  mechanism; this is the single biggest thing deferred, since without it
  this increment can only ingest flat per-structure geometry, not
  assembled full-chip layouts), `TEXT`, `BOX`, `NODE`, property records,
  and everything else. All are skipped with a deduplicated
  `GDS_UNSUPPORTED_RECORD` warning rather than treated as fatal.
- `LayoutIR::Geometry` has no field to attribute a polygon back to its
  owning GDSII structure; `parse_to_layout_ir()` flattens every
  structure's boundaries into one document. Documented as a known
  limitation in the design doc, not silently glossed over — this is only
  non-lossy because `SREF`/`AREF` (the only mechanism that would make
  cross-structure placement meaningful) are also out of scope here.
- No wiring of GDSII-derived geometry into `src/rules/` — this task only
  gets polygon data as far as `LayoutIR`, matching what
  `docs/design/declarative_rules.md`'s own non-goals section already
  flagged as blocked on this gap.

## Build & Test Results (Windows / `windows-release` preset, MSVC `/W4 /WX`)

```
cmake --preset windows-release
=> configured clean (Qt6 6.8.2 msvc2022_64, VS 2022 generator found)

cmake --build build/windows-release --config Release --target aegis_parsing
=> aegis_parsing.vcxproj built with zero warnings/errors under /W4 /WX
   (includes gdsii_parser.cpp)

cmake --build build/windows-release --config Release --target aegis_unit_tests
=> full unit test executable built clean, zero warnings/errors under /W4 /WX
   (includes test_gdsii_parser.cpp)

cmake --build build/windows-release --config Release --target aegis-perc-cli
=> built clean (needed separately; not a dependency of aegis_unit_tests)

ctest --test-dir build/windows-release -C Release --output-on-failure -R Gdsii
=> 100% tests passed, 0 failed out of 13 (all new)

ctest --test-dir build/windows-release -C Release --output-on-failure -L parsing
=> 100% tests passed, 0 failed out of 99 (13 new Gdsii + 86 pre-existing,
   all pre-existing parsing tests still pass unchanged)

ctest --test-dir build/windows-release -C Release  (full suite, cli built)
=> 510/511 passed. 1 failure:
   - Test #158 WorkspacePersistence "persists named filter presets and
     workspace views across restart" (SEGFAULT)
```

## Known Issues / Notes

- **Test #158 is the pre-existing, already-documented heisenbug** ("Bug B
  — found, not fixed") from
  `orchestration/runs/run_2026-08-14_s2-001-declarative-condition-rules.md`
  and `HANDOFF.md`: a timing-sensitive UI-teardown crash, characterized
  there as reproducing ~7/8 times in a standalone Release build but 0/15
  under a debugger (classic heisenbug). Confirmed here, not caused by this
  change:
  - This task's diff touches only `src/parsing/*` (new files),
    `src/parsing/CMakeLists.txt` (+2 lines), `tests/unit/parsing/*` (new
    file), `tests/CMakeLists.txt` (+1 line), and `docs/design/*` — there is
    no code path from this change into UI/workspace-persistence code.
  - Re-ran test #158 standalone twice (`ctest -R "WorkspacePersistence
    persists named filter presets"`) and it **passed both times**,
    consistent with the documented intermittent/heisenbug nature (fails
    under full-suite load, passes standalone) rather than a new
    deterministic break.
  - Still open; not attempted here — out of scope for S3-001 and already
    tracked in `HANDOFF.md`.
- **First full-suite run showed 6 failures, not 1** (test #158, test #159,
  and 4 `aegis-perc-cli`/`BatchCLI` tests). Root-caused before concluding
  anything was actually broken:
  - The 4 `BatchCLI` failures (`aegis-perc-cli import/report/run/baseline`)
    all failed with `std::system()` returning a nonzero/error exit and "The
    system cannot find the path specified." — because the initial full-suite
    run only had the `aegis_unit_tests` target built, not the separate
    `aegis-perc-cli` app target those tests `std::system()`-invoke as a
    subprocess (`tests/unit/scripting/test_batch_cli.cpp` constructs the
    path from `AEGIS_BINARY_DIR`/`bin`/`aegis-perc-cli.exe`, which didn't
    exist yet). Building the `aegis-perc-cli` target and re-running: **4/4
    pass**. Not a regression; an artifact of an incomplete build in this
    session, not of this change.
  - Test #159 (`WorkspacePersistence` "uses tolerant defaults for partial
    saved values") failed once under full-suite load and passed cleanly on
    an immediate standalone re-run — consistent with the same class of
    UI-teardown flakiness as test #158, not a new deterministic failure.
- **Diagnostic byte-offset truncation**: `GdsiiDiagnostic::byte_offset` is
  `std::size_t` but `IParserCallbacks::on_error`'s `line` parameter is
  `std::optional<int>`; for files larger than `INT_MAX` bytes the reported
  offset could wrap. Not expected to matter in practice (GDSII streams
  this large are not a realistic near-term input), but noted rather than
  silently ignored — a real fix would need `IParserCallbacks::on_error` to
  take a wider type, which is a cross-cutting interface change out of
  scope here.
- **No hierarchy validation beyond flat balance-checking**: `BGNSTR`
  appearing before a matching `ENDSTR`, and similar malformed nesting, are
  handled leniently (warn and best-effort continue) rather than being
  fully spec-validated against every possible malformed-nesting shape.
  Consistent with the project's "diagnostics, not crashes" convention, but
  worth knowing this is not an exhaustive GDSII conformance checker.

## Acceptance Criteria Verification
| Criterion (from the task prompt) | Status |
|---|---|
| Design doc in `docs/design/declarative_rules.md` style (Overview/Scope/format/LayoutIR mapping/non-goals) | ✅ `docs/design/gdsii_oasis_ingestion.md` |
| OASIS explicitly scoped out, documented as deferred follow-on | ✅ dedicated section, same pattern as declarative_rules.md deferring OR/NOT |
| Reuses existing LayoutIR geometry representation (no parallel type) | ✅ `GdsiiBoundary` uses `aegis::parsing::Polygon`/`Point` directly |
| `gdsii_parser.hpp`/`.cpp` implementing `IParser` (streaming, cancellation, diagnostics, provenance) | ✅ `GdsiiParser : public IParser`; byte-offset provenance documented as the binary analog of line numbers |
| Record subset: `HEADER`, `BGNLIB`, `LIBNAME`, `UNITS`, `BGNSTR`, `STRNAME`, `BOUNDARY`, `LAYER`, `DATATYPE`, `XY`, `ENDEL`, `ENDSTR`, `ENDLIB` | ✅ all implemented; every other record type explicitly skipped+warned |
| `PATH`/`SREF`/`AREF`/`TEXT` explicitly out of scope, documented | ✅ design doc "Non-goals" |
| Registered in `src/parsing/CMakeLists.txt` | ✅ |
| `tests/unit/parsing/test_gdsii_parser.cpp`, Catch2 v3, matching existing style | ✅ 13 test cases |
| Round-trip / malformed length / unsupported records skipped / cancellation tests | ✅ all four explicitly covered (see Tests section) |
| `tests/CMakeLists.txt` updated per existing (non-auto-discovering) pattern | ✅ |
| No edits to `src/rules/`, `src/ui/`, or other unrelated modules | ✅ diff confined to `docs/design/`, `src/parsing/`, `tests/` |
| No edits to `orchestration/tasks.yaml`/`roadmap.yaml`/`HANDOFF.md`/`DECISIONS.md`/`README.md` | ✅ none touched (per instructions — a separate consolidation pass updates those) |
| Build & test verification, honestly reported | ✅ see Build & Test Results above |
