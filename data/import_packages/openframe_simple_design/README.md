# OpenFrame Simple Design Import Package

This sample package is curated for AEGIS-PERC import testing.

## Provenance
- Physical and netlist seed files were copied from:
  - `C:/Users/kutay/openframe_simple_example`
- Source artifacts used:
  - `lef/simple_design.lef` -> `layout/technology.lef`
  - `def/simple_design.def` -> `layout/top.def`
  - `verilog/rtl/simple_design.v` -> `netlist/top.v`

## Package Contents
- `aegis-project.yaml` - versioned manifest
- `layout/technology.lef` - required LEF artifact
- `layout/top.def` - required DEF artifact
- `netlist/top.v` - required Verilog netlist artifact
- `rules/aegis_rules.yaml` - required AEGIS rule pack
- `power/power_domains.csv` - optional power-domain mapping example
- `reports/current_report.csv` - optional current/activity example
- `reports/waivers.csv` - optional waiver example
- `reports/erc_report.rpt` - optional imported-report example

## Import Recommendation
Import the folder root:
- `data/import_packages/openframe_simple_design`

This package is intended for import/validation workflow testing, not signoff-quality analysis.
