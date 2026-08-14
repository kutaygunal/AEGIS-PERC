# Declarative Condition Rules (`type: condition`)

## Overview
Rule packs previously could only *select and parameterize* one of a small,
fixed set of built-in C++ checks (`floating_net`, `power_domain_mismatch`,
`em_current_limit`) — defining a genuinely new check required writing a new
`IRule` subclass and recompiling. `type: condition` closes that gap for the
common case: a check that flags any device, net, or pin whose fields satisfy
a set of simple predicates, expressed entirely as data in a JSON/YAML rule
pack.

This is intentionally **not** a general-purpose rule language (no SVRF/TVF-
style expression grammar, no OR/NOT combinators, no cross-node predicates).
It covers the "flag nodes where field X compares to value Y" shape, which is
the most common request short of full programmability. See the commercial
gap analysis for how this scopes against Calibre PERC-class rule decks.

## Schema
```yaml
- id: <string>            # required, unique within the pack
  type: condition          # required
  target: device|net|pin   # required
  severity: critical|high|medium|low   # optional, default "high" (Error)
  description: <string>    # optional
  message: <string>        # optional; supports {field} and {name} substitution
  conditions:               # required, at least one; ANDed together
    - field: <string>       # required
      operator: <op>         # required, see below
      value: <string|number|bool>  # required unless operator is exists/not_exists
```

The same shape is accepted as JSON (`conditions` as an array of objects with
`field` / `operator` / `value` keys).

## Operators
`equals`, `not_equals`, `greater_than`, `greater_or_equal`, `less_than`,
`less_or_equal`, `contains` (substring match), `exists`, `not_exists`.

Numeric operators parse both sides as `double`; if either side fails to
parse, that condition evaluates to `false` for that node (the node is not
flagged) rather than raising an error, matching how `em_current_limit`
already treats unparsable data.

## Field resolution
- `name` always resolves to the node's name, for every target.
- `target: device` — `device_type`, plus any key in the device's property
  map (e.g. `w`, `l`, `model`).
- `target: net` — any key in the net's property map (e.g. `current_mA`,
  `layer`, `domain`).
- `target: pin` — `direction`, `layer`, `x`, `y`.

A condition on a field the node doesn't have evaluates to `false` (except
`not_exists`, which is exactly for testing absence).

## Combination semantics
All entries in `conditions` must hold for a node to be flagged (AND only).
Express alternatives ("A or B") as two separate rule entries instead of a
single rule with an OR — there is no OR/NOT combinator in this version.

## Message templating
If `message` is omitted, a default is generated listing the node, rule id,
and each condition. If provided, `{name}` and `{<field>}` tokens are
substituted with the node's name / resolved field value; an unresolved
token is left verbatim rather than raising an error.

## Example
```yaml
rules:
  - id: HOT_NET_M2
    type: condition
    target: net
    severity: high
    conditions:
      - field: current_mA
        operator: greater_than
        value: 100
      - field: layer
        operator: equals
        value: M2
    message: "Net '{name}' at {current_mA}mA exceeds budget on {layer}"
```

## Non-goals (deliberately out of scope here)
- OR / NOT combinators, nested groups.
- Cross-node predicates (e.g. "net driven by more than one OUTPUT pin" —
  that shape stays a purpose-built `IRule`, as `ShortCircuitRule` already
  is).
- Geometry-based predicates (no polygon/layer-stack data exists in the
  graph yet; see the GDSII/OASIS ingestion gap).

If a future need outgrows this, the natural next step is a small expression
grammar (`(A and B) or C`) layered on the same `RuleCondition` primitives,
not a rewrite.
