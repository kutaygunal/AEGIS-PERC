# GDSII / OASIS Layout Ingestion

## Overview
AEGIS-PERC currently ingests physical layout only through LEF (cell/via
technology geometry) and a simplified DEF-like format (placed instances,
routed nets, die area). Neither carries raw mask-level polygon data from a
tapeout-grade layout database. The 2026-08-14 commercial gap analysis flagged
this as the largest unscheduled gap relative to Calibre/Assura-class
signoff tools: without GDSII/OASIS ingestion, no physical/reliability check
(antenna, guard-ring presence, density, spacing) can ever be built, because
there is no polygon data in the graph to check.

This increment (S3-001) adds a native **GDSII** binary stream reader for a
scoped record subset sufficient to pull named structures containing
boundary/polygon geometry, on numbered layers, at correct physical scale,
into the existing `LayoutIR` model. It follows the same incremental
philosophy as DEC-008 (declarative condition rules): implement the subset
that unlocks real value now, document what's deliberately deferred, and
avoid speculative generality (e.g. a full hierarchical instancing engine)
before there's a concrete consumer that needs it.

**OASIS is explicitly out of scope for this increment** — see "Non-goals".

## Scope

### In scope (this increment)
A native binary-record reader for the following GDSII Stream record types,
sufficient to extract flat boundary/polygon geometry from named structures:

| Record | Purpose | Data type |
| --- | --- | --- |
| `HEADER` | Stream format version | 2-byte signed int |
| `BGNLIB` | Begin library (mod/access timestamps) | 2-byte signed int (12 values) |
| `LIBNAME` | Library name | ASCII string |
| `UNITS` | Physical scale: db-units-per-user-unit, db-units-in-meters | 8-byte real (2 values) |
| `BGNSTR` | Begin a named structure ("cell") | 2-byte signed int (12 values) |
| `STRNAME` | Structure name | ASCII string |
| `BOUNDARY` | Begin a closed-polygon element | none |
| `LAYER` | GDSII layer number for the current element | 2-byte signed int |
| `DATATYPE` | GDSII datatype number for the current element | 2-byte signed int |
| `XY` | Polygon vertex list for the current element | 4-byte signed int (pairs) |
| `ENDEL` | End the current element | none |
| `ENDSTR` | End the current structure | none |
| `ENDLIB` | End the library / stream | none |

Everything else in the stream (see Non-goals) is read (so the byte cursor
stays correctly positioned — GDSII records are self-delimiting via their
length prefix) but not interpreted: it is skipped with a `Warning`
diagnostic, deduplicated per record type so a real design with thousands of
`PATH` records doesn't flood the diagnostic list.

### Out of scope (deferred, tracked separately)
- **OASIS** — see dedicated section below.
- `PATH`, `SREF`, `AREF`, `TEXT`, `BOX`, `NODE`, and all remaining GDSII
  element/property records — see "Non-goals".

## GDSII binary stream format

GDSII ("Calma Stream Format", GDSII) is a flat sequence of fixed-header
binary records, big-endian throughout:

```
+----------------+----------------+----------------+-----------------------+
| length (u16)   | record type    | data type      | payload (length - 4)  |
| big-endian     | (u8)           | (u8)           | bytes                 |
+----------------+----------------+----------------+-----------------------+
```

- `length` is the **total** record length in bytes, including the 4-byte
  header itself, and is always even.
- `record type` identifies the record (`HEADER`, `BOUNDARY`, `XY`, ...).
- `data type` identifies the payload encoding: `0` no data, `1` bit array,
  `2` signed 16-bit int, `3` signed 32-bit int, `4` 4-byte real (unused by
  any record in this subset), `5` GDSII 8-byte real, `6` ASCII string.
- A stream is a flat concatenation of such records; there is no
  higher-level framing. Structures (`BGNSTR ... ENDSTR`) and elements
  (`BOUNDARY ... ENDEL`) are simply well-known record-nesting conventions,
  not separate container formats — the parser tracks "am I inside a
  structure" / "am I inside an element" as state while walking the flat
  record stream.

### The GDSII 8-byte real (`data type 5`)
`UNITS` is the only record in this subset that uses it, and it is not
IEEE-754. It is sign/exponent/mantissa, base 16, "excess-64":

```
byte 0:   bit 7 = sign, bits 6..0 = biased exponent (bias 64, base 16)
bytes 1-7: 56-bit unsigned mantissa, magnitude in [1/16, 1)
value = (-1)^sign * (mantissa / 2^56) * 16^(biased_exponent - 64)
```

The parser implements this decoder directly (`decode_gds_real8`); there is
no dependency on an external GDSII library.

### Physical scale (`UNITS`)
`UNITS` carries two 8-byte reals:
1. `db_unit_in_user_units` — how many *user units* one database unit (one
   raw `XY` integer step) represents. Commonly `0.001` (1 database unit =
   1 nanometer, when the user unit is the micron).
2. `db_unit_in_meters` — the absolute size of one database unit, in meters.
   Commonly `1e-9`.

Every raw `XY` integer is multiplied by `db_unit_in_user_units` before it
is stored as a `Point` in `LayoutIR`, so downstream consumers see the same
"user unit" convention (micron-scale doubles) that DEF/LEF coordinates
already use in this codebase — no separate unit-conversion step is needed
downstream. If `UNITS` is missing or malformed, the parser defaults
`db_unit_in_user_units` to `1.0` (pass raw integers through unscaled) and
emits a `Warning` diagnostic, rather than silently collapsing all geometry
to the origin by defaulting to `0.0`.

## Mapping to `LayoutIR`

No new geometry type is introduced. `GdsiiParser` reuses the existing
`aegis::parsing::Point` / `Polygon` / `Geometry` / `Layer` types from
`layout_ir.hpp` — the same representation LEF `PORT`/`OBS` geometry and DEF
`RECT`/`POLY` statements already normalize into.

| GDSII concept | `LayoutIR` mapping |
| --- | --- |
| `LIBNAME` | `LayoutIR::design_name` |
| `BGNSTR` / `STRNAME` ("structure") | Recorded structure names go into `LayoutIR::metadata["gds_structure_names"]` (comma-joined) and `metadata["gds_structure_count"]`; there is currently no per-`Geometry` field to attribute an individual polygon back to its owning structure (see "Known limitation" below). |
| `BOUNDARY` + its `LAYER`/`DATATYPE`/`XY` | One `Geometry{ layer, Polygon{points} }` per boundary. `points` are the `XY` coordinates scaled by `db_unit_in_user_units`, with a duplicated closing vertex (first == last, as GDSII boundaries conventionally repeat the start point) dropped so the stored polygon matches the non-repeating-vertex convention already used by `Polygon` elsewhere in this codebase. |
| GDSII `(layer, datatype)` pair | A synthetic `Layer.name` of the form `L<layer>D<datatype>` (e.g. `L1D0`), since GDSII carries no layer *names*, only numbers — unlike LEF, which names layers explicitly. One `Layer` entry is registered per distinct `(layer, datatype)` pair encountered, in first-seen order (matching how `DefParser::ensure_layer` registers layers on first use). |
| `UNITS` | `LayoutIR::metadata["gds_db_unit_in_user_units"]`, `metadata["gds_db_unit_in_meters"]` (both also used internally to scale `XY`). |

### Known limitation: geometry is not attributed to its owning structure
`LayoutIR::Geometry` has no "owning cell/structure" field, and this
increment does not extend that schema (per the "reuse the existing
representation" constraint). `parse_to_layout_ir()` therefore flattens
**every** structure's `BOUNDARY` geometry from the stream into one
`LayoutIR::geometries` list. This is honest and non-lossy for the common
first-increment case (a single flat top structure, or several independent
un-referenced structures), but is **not** a substitute for real hierarchy:
because `SREF`/`AREF` (instance references) are out of scope (see
Non-goals), there is no way for one structure's geometry to be legitimately
placed inside another via a reference in this increment anyway — so
flattening loses no placement information yet, but this must be revisited
before hierarchy is added: at that point every `Geometry` (or a new
placement-aware layer above it) will need an owning-structure/instance
identity. The streaming `IParser` callback path (`GdsiiParser::parse()`)
does preserve structure identity at a coarser grain: one `on_cell(...)` is
emitted per completed structure (name + boundary count as properties),
interleaved with the `on_geometry(...)` calls for that structure's
boundaries, so a caller consuming the callback stream directly (not via
`parse_to_layout_ir()`) can reconstruct the association if it wants to.

## Diagnostics

Modeled on the LEF parser's diagnostic-code convention
(`docs/design/lef-coverage.md`): malformed input produces a structured
`GdsiiDiagnostic{severity, code, message, byte_offset}` rather than a
crash or thrown exception from the normal parse path. GDSII has no line
numbers (it's binary), so provenance is the **byte offset** of the record
in the stream, reported through `IParserCallbacks::on_error`'s `line`
parameter (documented on `GdsiiParser` as carrying a byte offset, not a
line number, for this parser).

| Diagnostic code | Meaning | Severity |
| --- | --- | --- |
| `GDS_FILE_OPEN_FAILED` | Could not open the input file | Error |
| `GDS_RECORD_LENGTH_INVALID` | Record length `< 4` or odd (GDSII record lengths are always even and include the 4-byte header) | Error (fatal — cursor position can no longer be trusted) |
| `GDS_RECORD_TRUNCATED` | Declared payload length exceeds remaining bytes in the file | Error (fatal) |
| `GDS_PARSE_CANCELLED` | Cancellation token was signalled | Error |
| `GDS_HEADER_INVALID` | `HEADER` payload was not a single 16-bit int | Warning |
| `GDS_UNITS_INVALID` | `UNITS` payload was not two 8-byte reals | Warning (falls back to unscaled `1.0`) |
| `GDS_UNSUPPORTED_RECORD` | A record type outside the supported subset (`PATH`, `SREF`, `AREF`, `TEXT`, ...) was skipped | Warning (deduplicated per record type) |
| `GDS_ELEMENT_OUTSIDE_STRUCTURE` | `BOUNDARY` seen outside `BGNSTR`/`ENDSTR` | Warning (element ignored) |
| `GDS_LAYER_OUTSIDE_ELEMENT` / `GDS_DATATYPE_OUTSIDE_ELEMENT` / `GDS_XY_OUTSIDE_ELEMENT` | `LAYER`/`DATATYPE`/`XY` seen outside a `BOUNDARY`/`ENDEL` pair | Warning (ignored) |
| `GDS_BOUNDARY_INVALID` | `ENDEL` reached with fewer than 3 distinct polygon vertices | Warning (element dropped, parse continues) |
| `GDS_ENDEL_WITHOUT_ELEMENT` / `GDS_ENDSTR_WITHOUT_STRUCTURE` | Unbalanced nesting | Warning |
| `GDS_MISSING_ENDLIB` | Stream ended without an `ENDLIB` record | Warning |

Only `GDS_FILE_OPEN_FAILED`, `GDS_RECORD_LENGTH_INVALID`,
`GDS_RECORD_TRUNCATED`, and `GDS_PARSE_CANCELLED` are treated as fatal
(the parser stops and `parse()`/`parse_to_layout_ir()` report failure).
Every other diagnostic is recorded and parsing continues — matching the
project's established "diagnostics, not crashes" convention from the LEF
parser.

## Non-goals (deliberately out of scope in this increment)

- **OASIS.** OASIS (Open Artwork System Interchange Standard, SEMI P39) is
  a different, materially more complex format: it uses compressed,
  delta-encoded cell hierarchy, variable-length integer ("unsigned/signed
  varint") encoding throughout, repetition ("array reference") compression
  primitives baked into the format grammar itself (not a simple `AREF`
  record), string interning tables, and an optional CRC/checksum trailer.
  It is not a small extension of the GDSII reader above — it needs its own
  varint decoder, its own repetition-block expansion, and its own modal
  (delta-from-previous) coordinate model. Same reasoning as declarative
  rules deferring OR/NOT combinators (DEC-008): building both formats'
  readers speculatively, before either has a concrete downstream consumer
  (a physical rule check) exercising it, would be premature generality.
  OASIS ingestion is tracked as an explicit deferred follow-on to S3-001,
  not started.
- **`PATH`** (routed wire geometry with width/endcap semantics) — real
  layouts are path-heavy; this is the most valuable near-term follow-on
  once boundary ingestion is validated end-to-end.
- **`SREF` / `AREF`** (cell instance references, including array
  references) — this is the actual hierarchy/placement mechanism in
  GDSII. Without it, this increment can only ingest flat geometry per
  structure, not assembled full-chip layouts. This is the single biggest
  thing deferred here, and is called out explicitly in the "Known
  limitation" note above.
- **`TEXT`** (annotation/label elements) — no consumer needs it yet.
- **`BOX`, `NODE`, property records (`PROPATTR`/`PROPVALUE`), structure
  classing (`STRCLASS`), and every other remaining GDSII record type** —
  skipped with a deduplicated `GDS_UNSUPPORTED_RECORD` warning; see the
  Diagnostics table.
- **Geometry-based rule predicates.** This increment only gets polygon
  data as far as `LayoutIR`; it does not wire that data into
  `src/rules/`. That was already noted as blocked-on-this-gap in
  `docs/design/declarative_rules.md`'s non-goals, and remains a separate,
  future task once ingestion lands.

If a future need outgrows flat boundary-only ingestion, the natural next
steps, in priority order, are: (1) `SREF`/`AREF` to get real hierarchy,
(2) `PATH`, (3) OASIS as its own reader sharing the `LayoutIR` mapping
conventions established here — not a rewrite of this parser.
