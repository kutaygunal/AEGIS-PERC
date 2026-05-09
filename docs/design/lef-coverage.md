# LEF 5.x parser coverage

This document tracks the current grammar/block coverage of the native LEF parser used by AEGIS-PERC.

## Supported and normalized

| Scope | Coverage |
| --- | --- |
| Top-level metadata | `VERSION`, `DIVIDERCHAR`, `BUSBITCHARS`, `NAMESCASESENSITIVE`, `MANUFACTURINGGRID`, `FIXEDMASK`, `CLEARANCEMEASURE`, `NOWIREEXTENSIONATPIN`, `USEMINSPACING`, `MAXVIASTACK` |
| Top-level blocks | `UNITS`, `PROPERTYDEFINITIONS`, `SITE`, `LAYER`, `VIA`, `MACRO` |
| `UNITS` | Raw unit statements are captured under `UNITS.*` properties |
| `PROPERTYDEFINITIONS` | Property definitions are captured under `PROPERTYDEF.<scope>.<name>` |
| `SITE` | `CLASS`, `SIZE`, additional raw properties, and typed normalization to `LefSiteClass` + symmetry list |
| `LAYER` | `TYPE` plus common LEF 5.x technology statements retained as properties (`WIDTH`, `SPACING`, `SPACINGTABLE`, `PITCH`, `OFFSET`, `DIRECTION`, `RESISTANCE`, `CAPACITANCE`, `EDGECAPACITANCE`, `AREA`, `THICKNESS`, `HEIGHT`, `WIREEXTENSION`, `SHRINKAGE`, `CAPMULTIPLIER`, `MINWIDTH`, `MAXWIDTH`, `MINSTEP`, `PROTRUSIONWIDTH`, `PROPERTY`) and typed normalization for layer kind, routing direction, width, pitch, offset, and electrical values |
| `VIA` | Header qualifiers plus common properties (`DEFAULT`, `RESISTANCE`, `FOREIGN`, `TOPOFSTACKONLY`, `VIARULE`, `CUTSIZE`, `CUTSPACING`, `ENCLOSURE`, `ROWCOL`, `ORIGIN`, `OFFSET`, `PATTERN`, `PROPERTY`) and mixed per-layer geometry (`RECT`, `POLYGON`, `PATH`, nested `VIA` placements, width hints), with typed `is_default` and `resistance` semantics |
| `VIARULE` | Parsed into typed `LefViaRule` models with name, `generate`, raw property capture, and source provenance |
| `NONDEFAULTRULE` | Parsed into typed `LefNonDefaultRule` models with name, `hard_spacing`, raw property capture, and source provenance |
| `MACRO` | `CLASS`, `FOREIGN`, `ORIGIN`, `SIZE`, `PIN`, `OBS` plus common properties (`SOURCE`, `EEQ`, `LEQ`, `SITE`, `SYMMETRY`, `POWER`, `PROPERTY`) and typed normalization for class, source, site, and symmetries |
| `PIN` | `DIRECTION`, `USE`, `PORT` plus common electrical/antenna/property statements (`SHAPE`, `CAPACITANCE`, `ANTENNAMODEL`, `ANTENNAGATEAREA`, `ANTENNADIFFAREA`, `ANTENNAPARTIALMETALAREA`, `ANTENNAPARTIALMETALSIDEAREA`, `ANTENNAPARTIALCUTAREA`, `ANTENNAMAXAREACAR`, `ANTENNAMAXSIDEAREACAR`, `ANTENNAMAXCUTCAR`, `MUSTJOIN`, `NETEXPR`, `SUPPLYSENSITIVITY`, `GROUNDSENSITIVITY`, `TAPERRULE`, `PROPERTY`) and typed normalization for direction, use, tristate, shape, and common antenna/capacitance values |
| `PORT` / `OBS` geometry | Mixed per-layer `RECT`, `POLYGON`, `PATH`, nested `VIA` placements, and width hints |
| Geometry provenance | Every emitted primitive (`RECT`, `POLYGON`, `PATH`, nested `VIA`) carries `source_line` provenance |

## Explicitly classified as unsupported

Unsupported constructs are not silently dropped. They emit structured diagnostics.

| Scope | Current behavior |
| --- | --- |
| Top-level unsupported statements | `LEF_UNSUPPORTED_TOP_LEVEL` |
| Top-level unsupported named blocks | `VIARULE`, `NONDEFAULTRULE` are skipped with `LEF_UNSUPPORTED_BLOCK` |
| Unsupported `LAYER` statements | `LEF_UNSUPPORTED_LAYER_STATEMENT` |
| Unsupported `VIA` statements | `LEF_UNSUPPORTED_VIA_STATEMENT` |
| Unsupported `MACRO` statements | `LEF_UNSUPPORTED_MACRO_STATEMENT` |
| Unsupported `PIN` statements | `LEF_UNSUPPORTED_PIN_STATEMENT` |
| Unsupported `PORT` statements | `LEF_UNSUPPORTED_PORT_STATEMENT` |
| Unsupported `OBS` statements | `LEF_UNSUPPORTED_OBS_STATEMENT` |

## Geometry validation diagnostics

Malformed geometry is rejected with structured diagnostics instead of being approximated.

| Diagnostic | Meaning |
| --- | --- |
| `LEF_GEOMETRY_WITHOUT_LAYER` | Geometry was emitted outside an active `LAYER` context |
| `LEF_RECT_INVALID` | `RECT` did not provide exactly four coordinates |
| `LEF_POLYGON_INVALID` | `POLYGON` did not provide at least three coordinate pairs |
| `LEF_PATH_INVALID` | `PATH` did not provide at least two coordinate pairs |
| `LEF_VIA_GEOMETRY_INVALID` | Nested geometry `VIA` did not provide `x y via_name` |
| `LEF_WIDTH_INVALID` | Geometry width hint was malformed |

## Semantic validation

The parser now runs a semantic-normalization pass after raw grammar capture.

Examples of validated conditions:
- missing or unknown `SITE CLASS`
- non-positive or missing `SITE SIZE`
- `ROUTING` layers missing typed `WIDTH`, `PITCH`, or `DIRECTION`
- `VIA` blocks without any layer geometry
- `VIARULE` blocks without typed `GENERATE`
- pins missing typed `DIRECTION`, missing `PORT`s, or carrying invalid typed electrical values
- degenerate normalized geometry primitives

This keeps raw properties available while allowing downstream code to consume validated typed fields separately.

## Robustness, stress, and pre-release validation

Current deterministic robustness coverage includes:
- malformed fixed corpus inputs
- deterministic mutation-based fuzz corpus derived from industrial-like LEF text
- oversized synthetic library stress parsing with many layers, macros, pins, and geometry blocks

Current developer/CI expectations:
- malformed inputs must return diagnostics rather than crash or throw from `parse_string`
- diagnostic growth is expected to stay bounded for deterministic fuzz corpus tests
- large synthetic stress parse should complete within a realistic developer-machine envelope (currently validated with a conservative 5-second unit-test budget)

Current practical parser limits and behavior:
- parsing is statement-buffered in memory; extremely large single-file inputs scale with input size
- progress reporting remains available for long parses
- cancellation is polled periodically during statement traversal
- malformed geometry and incomplete typed semantics are surfaced as explicit diagnostics

## Notes

- The parser supports multiline logical statements terminated by `;`, which is required by common LEF 5.x formatting.
- Geometry normalization is no longer `RECT`-only; richer abstract-shape coverage is available while preserving the existing rectangle data path for downstream compatibility.
- Raw parsed properties remain preserved alongside typed semantics so downstream consumers can distinguish capture from validated normalization.
