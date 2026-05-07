# AEGIS-PERC Synthetic Sample Designs

This directory contains versioned synthetic layout/netlet test data for unit and integration tests.

## Format version

Current format: **1.0.0**

## Schema

See `schema.json` for the formal JSON schema. The top-level object contains four required sections:

| Section | Description |
|---------|-------------|
| `design` | Metadata: `name`, `technology`, optional `description`. |
| `layout` | Abstract geometric primitives (`rectangle`, `polygon`) on named layers. Units are abstract nanometers (nm). |
| `netlist` | Devices, nets, and ports that describe circuit connectivity. |
| `format_version` | Semantic version string of the file format itself. |

## Extending samples

1. Copy an existing sample (e.g. `inverter.json`) as a template.
2. Update `design.name` and `design.description`.
3. Add new shapes to `layout.shapes`. `primitive` must be `"rectangle"` (use `bbox`) or `"polygon"` (use `points`).
4. Add devices, nets, and ports to `netlist`.
5. Run validation:
   ```bash
   python scripts/validate_samples.py
   ```
6. Add a line to `tests/unit/data/test_sample_data.cpp` if the new sample requires a structured assertion.

## Conventions

- `technology` is an abstract string (e.g. `"abstract_cmos_45nm"`). It is not tied to a real PDK.
- Layer names are lowercase, underscore-separated (e.g. `"diffusion"`, `"poly_silicon"`, `"metal_1"`).
- Device type strings are lowercase (e.g. `"pmos"`, `"nmos"`, `"resistor"`).
- Net/port names are lowercase except power/ground (`vdd`, `vss`).

## Files

| File | Description |
|------|-------------|
| `schema.json` | JSON schema for validation. |
| `inverter.json` | Single-stage CMOS inverter (2 devices). |
| `nand2.json` | 2-input NAND gate (4 devices). |
| `ring_oscillator.json` | 5-stage ring oscillator (10 devices). |
