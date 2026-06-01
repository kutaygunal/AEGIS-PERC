# Medium Design Import Package

Moderate-complexity handcrafted corpus fixture for AEGIS-PERC smoke testing.

## Complexity Profile
- **Medium**: multiple instances (6), nets (8), and 4 metal layers.
- Die area: 200 x 200 um.
- Layers: M1, M2, M3, M4 with 3 via definitions.
- Contains sequential elements (DFFs) and combinational gates.
- Intended for moderate parser validation and full workspace-realization coverage.

## Known Limitations
- No detailed internal cell LEF macros; components are referenced by abstract names.
- No blockage or obstruction layers beyond pin keepouts.
- No hierarchical blocks or nested modules.
- SPECIALNETS routing is simplified (single-segment stripes).

## Import Recommendation
Import the folder root: `data/import_packages/medium_design`
