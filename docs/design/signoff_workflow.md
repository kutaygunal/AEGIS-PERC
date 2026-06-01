# Signoff Workflow (Baselines, Waivers, Regression Diff)

## Overview
The commercial "signoff loop" for AEGIS-PERC is:

1. Run checks
2. Apply waivers (suppress known findings with audit trail)
3. Diff vs a baseline (new / removed / changed)
4. Gate regressions in automation (e.g., CI)
5. Export audit-ready reports

This document defines the foundational identity contract used by waivers and regression diff.

## Violation Identity Key (v1)
`Violation::identity_key()` produces a deterministic, versioned string key intended for:
- waiver matching
- baseline storage
- regression diffing

### Stability guarantees
- Stable across changes to `Violation::message` and `Violation::id`.
- Stable across insignificant floating point noise in `ViolationLocation::point` via quantization.
- Changes when the underlying issue meaningfully changes (rule id and/or structured location fields).

### Inputs (v1)
- `Violation::rule_id`
- `ViolationLocation::{layer, net_name, pin_name, device_name}`
- `ViolationLocation::point` (if present), quantized to 1e-3 units and encoded as integer `(x,y)`

### Encoding and versioning
- The key is prefixed with `v{N}` where `N` is the identity schema version.
- Components are normalized by trimming and collapsing whitespace, then percent-encoding non-alphanumeric characters for unambiguous parsing.

### Notes
If future work requires identity stability across refactors that change naming (e.g., display name changes), prefer adding explicit stable object identifiers to the violation location context rather than weakening matching rules.

