# Sparse Design Import Package

Minimal handcrafted corpus fixture for AEGIS-PERC smoke testing.

## Complexity Profile
- **Sparse**: very few instances (2), nets (3), and geometry.
- Die area: 100 x 100 um.
- Layers: M1, M2.
- Intended for fast parser validation and basic workspace-realization coverage.

## Known Limitations
- No via geometry beyond the M1_M2 default definition.
- No blockage or obstruction layers.
- No hierarchical instances.
- No clocked elements.

## Import Recommendation
Import the folder root: `data/import_packages/sparse_design`
