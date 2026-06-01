# Geometry-Rich Design Import Package

Geometry-rich handcrafted corpus fixture for AEGIS-PERC smoke testing.

## Complexity Profile
- **Geometry-rich**: many instances (12), nets (14), and 5 metal layers with dense power-grid stripes.
- Die area: 300 x 300 um.
- Layers: M1, M2, M3, M4, M5 with 4 via definitions.
- Contains sequential elements (DFFs), combinational gates, and scan-mux topology.
- Intended for stress-testing parser scalability, scene-adapter coverage, and layer visibility controls.

## Known Limitations
- No detailed internal cell LEF macros; components are referenced by abstract names.
- OBS layers on the block macro are simplified rectangles.
- No hierarchical blocks or nested modules.
- SPECIALNETS routing uses straight stripes only (no complex dog-legs).

## Import Recommendation
Import the folder root: `data/import_packages/geometry_rich_design`
