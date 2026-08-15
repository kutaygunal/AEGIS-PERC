# Physical / Reliability Checks — Antenna Ratio (`type: antenna_ratio`)

## Overview
`roadmap.yaml` (Sprint 2 notes) listed physical/reliability check types
(antenna, guard-ring presence) as a "follow-on, not yet scheduled" gap,
annotated as blocked on "geometry data" existing — implicitly pointing at
GDSII/OASIS ingestion (S3-001, a separate, parallel task). Before scoping
this task that way, this increment re-verified the claim against the actual
code rather than the roadmap note, per the task brief. The finding is more
nuanced than "blocked": LEF/DEF ingestion (`src/parsing/src/lef_parser.cpp`,
`src/parsing/src/def_parser.cpp`) already carries real physical data into
`LayoutIR` — per-shape rectangles/polygons with layer (`LayoutIR::geometries`),
external port placement (`Port::location`, `Port::layer`), device placement
(`Device::properties["x"/"y"]`), and — critically for antenna checks — SPICE
netlist ingestion already captures transistor gate sizing (`W`/`L`) as
arbitrary `key=value` device properties (`src/parsing/src/spice_parser.cpp`,
the `M` element case). None of this requires GDSII/OASIS.

However, one real gap does exist: `ConnectivityGraph::from_layout_ir()`
(`src/graph/src/connectivity_graph.cpp`) never consumes `LayoutIR::geometries`
at all — raw shape data is parsed and preserved on the `LayoutIR`/session
object, but it is not linked to any net or device when the graph is built,
and no stage in the pipeline aggregates shape area by net. So a *complete*
antenna check (accumulated routed-metal polygon area per net, from real
LEF/DEF routing geometry) is not buildable from today's graph — that
specific aggregation is out of scope here and named as a non-goal below.

This increment ships a first, real, working `IRule` for antenna ratio that:
- Computes the **gate-area** side of the ratio from genuine data already on
  the graph today (`DeviceNode::properties["w"/"l"]`, sourced from real
  SPICE `M`-element parsing — not fabricated).
- Reads the **accumulated connected metal-area** side from a `NetNode`
  property (default key `metal_area_um2`), using the exact same "rule reads
  whatever is in the property map" pattern the existing `em_current_limit`
  check already uses for `current_mA` — that field, too, is populated by a
  separate enrichment stage (current-activity import), not computed inline
  by the rule. `metal_area_um2` is not yet populated by any existing
  importer/enrichment stage (see Non-goals) — this is stated explicitly
  rather than left implicit, since it is the honest boundary of what "LEF/DEF
  geometry is sufficient" means for this specific ratio today.

The rule is fully functional, tested, and wired into `RuleEngine`/`RulePack`
now; it fires immediately on any graph where a future net-metal-area
enrichment stage (or a hand-authored/imported fixture) populates
`metal_area_um2`, exactly as `em_current_limit` fires once `current_mA` is
present.

## Why antenna ratio (vs. guard-ring presence)
Antenna ratio (accumulated connected metal area vs. gate area, flagged past
a configurable multiple) is the most commonly requested EDA physical
reliability check and maps directly onto data already flowing through this
codebase's SPICE ingestion (gate `W`/`L`) and onto the existing per-net
property-map convention already used by `em_current_limit`. Guard-ring
presence, by contrast, needs proximity/enclosure geometry (is device X
enclosed by a ring structure within layout distance Y) that has no
representation anywhere in `ConnectivityGraph` today — it would need to be
faked entirely to produce anything. Antenna ratio does not require faking a
data path: it is real code operating on real graph fields, honestly scoped
around the one aggregation step that genuinely does not exist yet.

## What it reads, and from where
| Data | Source | Status |
|---|---|---|
| Gate width (`w`) / length (`l`) | `DeviceNode::properties`, populated by `src/parsing/src/spice_parser.cpp`'s `M`-element parsing of `key=value` SPICE parameters | Real, populated today by SPICE netlist ingestion when the netlist specifies `W=`/`L=` |
| Gate terminal identification | `EdgeType::DeviceToPin` edges with `terminal_name == "gate"` (configurable) | Real, existing graph structure (same edges `OpenCircuitRule`/`ShortCircuitRule` already traverse) |
| Direct gate area override | `DeviceNode::properties["gate_area_um2"]` (configurable key) | Optional; used instead of `w * l` when present |
| Accumulated connected metal area | `NetNode::properties["metal_area_um2"]` (configurable key) | **Not populated by any existing importer today** — see Non-goals |

## Parameterization
Follows the `em_current_limit` pattern: a built-in C++ `IRule` subclass
(`AntennaRatioRule`) constructed directly from the `RulePackRuleDefinition`,
not the generic `ConfiguredRuleAdapter` used for parameterless rules
(`floating_net`, `power_domain_mismatch`).

```yaml
- id: ANTENNA_RATIO_M1
  type: antenna_ratio
  severity: high
  parameters:
    max_ratio: 400            # required; ratio threshold (metal_area / gate_area)
    gate_terminal: gate       # optional, default "gate"
    gate_width_field: w       # optional, default "w"
    gate_length_field: l      # optional, default "l"
    gate_area_field: gate_area_um2   # optional, default "gate_area_um2"
    metal_area_field: metal_area_um2 # optional, default "metal_area_um2"
```

`max_ratio` is required at validation time (mirrors `em_current_limit`
requiring at least one `*_max_mA` parameter). All other parameters are
optional field-name overrides so rule-pack authors can point the check at
whatever property keys their import/enrichment pipeline actually uses.

## Execution semantics
For each net in the graph:
1. Walk the net's connected pins (`NetToPin` edges); for each pin, find its
   owning device via the `DeviceToPin` edge whose `terminal_name` matches
   `gate_terminal`.
2. For each such device, resolve a gate area: prefer
   `properties[gate_area_field]` if present and parseable; otherwise
   `properties[gate_width_field] * properties[gate_length_field]`. A device
   missing both — or with an unparsable value in either — contributes zero
   and is skipped, **not** treated as an error (matches the existing
   `em_current_limit` convention: unparsable numeric data is non-triggering,
   not a crash or a fatal violation).
3. Sum gate areas across all gate-connected devices on the net (multi-finger
   / multi-transistor nets accumulate correctly).
4. If the net has no gate-connected devices, or total gate area is zero,
   the net is skipped (a ratio against zero area is not a meaningful antenna
   metric).
5. Read `properties[metal_area_field]` from the net; if absent or
   unparsable, the net is skipped (not flagged) — same non-triggering
   convention.
6. `ratio = metal_area / total_gate_area`. Flagged (violation emitted) only
   when `ratio > max_ratio` (strict, matching `em_current_limit`'s `>`
   semantics — a net exactly at the threshold is not flagged).

Violations carry `net_name` location and metadata (`ratio`, `gate_area_um2`,
`metal_area_um2`, `max_ratio`) for downstream reporting/waiver tooling.

## Non-goals (deliberately out of scope here)
- **Net-level metal-area aggregation from raw LEF/DEF/GDSII polygon
  geometry.** `LayoutIR::geometries` (Rectangle/Polygon + layer) exists and
  is real, but nothing today associates a geometry entry with an owning net,
  and `ConnectivityGraph::from_layout_ir()` does not consume `geometries` at
  all. Building that association + area-summation pipeline is a
  substantially larger, separate task (would touch `src/parsing/` and
  `src/graph/`, both explicitly out of scope for this task unless minimal —
  this is not minimal). This check is designed so that once such an
  enrichment stage exists and populates `metal_area_um2`, it works
  immediately with no changes to this rule.
- **Full antenna diode-count analysis.** Real antenna checks reduce ratio
  violations when a protection diode is present on the net; this
  implementation does not model diode protection credit at all.
- **Multi-layer stacked antenna accumulation.** Real signoff tools track
  antenna ratio incrementally per routing layer as metal is added
  layer-by-layer (partial-area rules per `ANTENNAPARTIALMETALAREA` etc.,
  which the LEF parser already tokenizes as raw macro/pin properties but
  does not interpret). This check computes one net-level ratio against a
  single accumulated metal-area figure, not a per-layer stack.
- **Guard-ring presence.** Considered as the alternative first check (see
  above); not implemented in this increment — no proximity/enclosure
  geometry exists in the graph to check against today.
- **DRC-style spacing/width checks.** Different check family entirely
  (geometric minimum-distance/width rules over polygon geometry); would
  also need the same geometry-to-graph aggregation gap closed first.
- **Antenna protection via jumper/routing modification suggestions.**
  This is a detector, not an auto-fixer — consistent with `AGENTS.md` §5
  ("no safety-critical or destructive automated fix should run without
  explicit user confirmation"); it reports violations only.

If a future increment closes the geometry-aggregation gap (LEF/DEF or
GDSII/OASIS shapes → per-net accumulated area), the natural next step is a
dedicated enrichment pass (analogous to the existing current-activity
enrichment for `current_mA`) that populates `metal_area_um2` on real
imports — no change to `AntennaRatioRule` itself should be required.
