# Customer Import Workflow and Project Manifest

## Status
Draft

## Purpose
Define the Sprint 6 MVP contract for importing real customer design packages into AEGIS-PERC.
The contract is tool-neutral and models exports typically produced by Cadence, Synopsys,
Siemens EDA / Calibre, OpenROAD, or internal design scripts.

## Goals
- Support a realistic semiconductor customer onboarding workflow.
- Separate **source artifacts** from the **normalized internal project model**.
- Allow the same package to be imported from desktop UI, drag-and-drop folder ingestion, or CLI.
- Provide actionable validation errors before analysis starts.
- Version the package format so future additions do not break old projects.

## Non-Goals (MVP)
- Native parsing of proprietary foundry PERC/DRC/LVS rule decks.
- Full GDSII/OASIS signoff geometry import.
- Full UPF/CPF semantic support.
- Multi-node distributed execution protocol.

## Customer Workflow
Typical customer flow:
1. Export design artifacts from implementation / signoff tools.
2. Gather them into a project folder or create an AEGIS manifest.
3. Import through UI or CLI.
4. Run preflight validation.
5. Normalize data into AEGIS internal models.
6. Run deterministic checks.
7. Export reports and optional ML-ready metadata.

## MVP Input Contract

### Required artifacts
| Role | Accepted formats | Purpose |
|------|------------------|---------|
| Technology/library | `.lef` | Layer, via, macro/library abstraction |
| Design layout | `.def` | Placement, routing, net and pin context |
| Netlist | `.v`, `.sv`, `.sp`, `.spi`, `.cdl` | Logical or transistor-level connectivity |
| Rule pack | `.yaml`, `.yml`, `.json` | AEGIS customer-configurable checks |

### Optional artifacts
| Role | Accepted formats | Purpose |
|------|------------------|---------|
| Power intent | `.csv` (MVP), `.upf`/`.cpf` reserved | Domain and voltage mapping |
| Current/activity | `.csv` | EM-style thresholds and stress inputs |
| Existing reports | `.rpt`, `.txt`, `.csv`, `.json`, `.xml` | Imported external violations/history |
| Waivers | `.csv`, `.yaml`, `.json` | Accepted or suppressed findings |
| Manifest | `.yaml`, `.json` | Explicit file-role mapping |

## Supported import modes

### 1. Explicit file selection
User selects files one-by-one in the desktop app.

### 2. Project folder ingestion
User drops a folder. AEGIS scans for known files, proposes roles, and asks for confirmation.

### 3. Manifest-based import
A manifest explicitly maps file roles and avoids heuristic ambiguity.

### 4. CLI-driven import
```bash
aegis-perc-cli import \
  --project FalconCPU \
  --lef tech.lef \
  --def top.def \
  --netlist top.v \
  --rules aegis_rules.yaml \
  --power power_domains.csv \
  --current current_report.csv
```

## Manifest format
Manifest versioning is mandatory.

### YAML example
```yaml
manifest_version: 1
project:
  name: FalconCPU
  description: CPU block reliability verification package
  customer: ExampleSemi
  design_stage: place_route

artifacts:
  technology:
    - path: layout/technology.lef
      role: lef
  layout:
    - path: layout/top.def
      role: def
  netlist:
    - path: netlist/top.v
      role: verilog
  rules:
    - path: rules/aegis_rules.yaml
      role: aegis_rule_pack
  power:
    - path: power/power_domains.csv
      role: power_domains_csv
      optional: true
  current:
    - path: reports/current_report.csv
      role: current_csv
      optional: true
  waivers:
    - path: reports/waivers.csv
      role: waiver_csv
      optional: true
  external_reports:
    - path: reports/erc_report.rpt
      role: imported_report
      optional: true
```

### JSON shape
```json
{
  "manifest_version": 1,
  "project": {
    "name": "FalconCPU"
  },
  "artifacts": {
    "technology": [{"path": "layout/technology.lef", "role": "lef"}],
    "layout": [{"path": "layout/top.def", "role": "def"}],
    "netlist": [{"path": "netlist/top.v", "role": "verilog"}],
    "rules": [{"path": "rules/aegis_rules.yaml", "role": "aegis_rule_pack"}]
  }
}
```

## Manifest schema rules
- `manifest_version` is required.
- `project.name` is required.
- Paths are relative to the manifest file unless absolute.
- Unknown top-level keys are warnings in v1, not fatal errors.
- Each artifact entry must contain `path` and `role`.
- The same file may not satisfy two required roles unless explicitly declared as a multi-role artifact in a future version.
- Missing optional artifacts do not block import.

## File-role detection rules
If no manifest is provided, AEGIS may infer roles using:
1. File extension
2. Content markers
3. Directory hints (`layout/`, `netlist/`, `rules/`, `reports/`)
4. User overrides in UI/CLI

Examples:
- `.lef` → likely technology/library
- `.def` → likely design layout
- `.v` / `.sv` → likely gate-level netlist
- `.sp` / `.spi` / `.cdl` → likely SPICE/CDL netlist
- `.yaml` with `rules:` root → likely AEGIS rule pack
- `.csv` with `instance,domain,voltage` → likely power-domain mapping
- `.csv` with `net_name,current_mA,...` → likely current/activity input

Heuristic detection must never silently auto-accept ambiguous inputs without surfacing warnings.

## Validation rules

### Import blockers
- No LEF found
- No DEF found
- No supported netlist found
- No AEGIS rule pack found
- Manifest version unsupported
- Referenced artifact path missing
- Duplicate required-role assignment with conflicting files
- Rule pack schema invalid

### Non-fatal warnings
- Unknown file types present
- Optional files missing
- Multiple candidate netlists discovered
- CSV columns partially recognized
- External reports imported but not yet mapped to internal violation schema

## Normalization contract
Imported artifacts must normalize into these conceptual groups:
- **Physical context**: layout, layers, placement, routes
- **Logical/electrical context**: nets, pins, devices, instances
- **Policy context**: rules, thresholds, severities
- **Power context**: domain and voltage assignments
- **Stress context**: current/activity inputs
- **History context**: prior reports and waivers

Every normalized object should preserve provenance when possible:
- source file path
- parser/importer name
- line/row/record reference
- import timestamp

## Reserved future extensions
The v1 contract intentionally leaves room for:
- GDSII / OASIS geometry packages
- SPEF / DSPF / SPF extracted netlists
- UPF / CPF power intent
- packaged design archives
- object-storage backed remote artifacts
- proprietary deck adapter metadata

## Recommended customer folder layout
```text
my_chip_project/
  aegis-project.yaml
  layout/
    technology.lef
    top.def
  netlist/
    top.v
  power/
    power_domains.csv
  reports/
    current_report.csv
    erc_report.rpt
    waivers.csv
  rules/
    aegis_rules.yaml
```

## Example MVP upload set
Required:
- `technology.lef`
- `top.def`
- `top.v` or `top.sp`
- `aegis_rules.yaml`

Optional:
- `power_domains.csv`
- `current_report.csv`
- `waivers.csv`
- `erc_report.rpt`

## Acceptance mapping to P6-001
- Required and optional customer inputs are defined.
- MVP package includes LEF, DEF, gate/SPICE netlist, rules, optional power CSV, optional current CSV, optional waivers/reports.
- A versioned manifest schema maps physical, logical, power, current, and report artifacts.
- Missing files, duplicate roles, unsupported combinations, and versioning behavior are documented.
