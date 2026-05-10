# Handoff

## Last Updated
2026-05-10

## Current Status
`P13-003` is done: the Hierarchy Browser panel lists imported design objects grouped by category with live search, selection sync, and explicit empty-state guidance.

## Last Completed Task
`P13-003` — Add imported-design hierarchy and object browser for instances, nets, ports, and layers

## Current Task
No implementation task in progress

## Ready Tasks
- `P13-004` — Expose commercial-grade imported object properties and provenance inspection
- `P13-FOLLOW-001` — Migrate HierarchyBrowser to QAbstractItemModel + QTreeView for million-object scalability

## Blocked Tasks
- None recorded in the current trimmed `tasks.yaml`

## Important Notes
- Imported package commit now stores `loaded_import_session`, installs an imported `UiScene`, and binds the session graph into the desktop shell immediately after `Load Project`.
- `build_imported_design_scene()` is the compatibility seam for imported 2D realization; future P13 UI surfaces should consume scene/session metadata from there rather than reparsing DEF/LEF directly.
- Instance placement rectangles now render from DEF placement coordinates. When LEF macro dimensions are missing, the scene builder falls back to deterministic marker sizes and logs explicit diagnostics instead of leaving the scene blank.
- Stable imported object IDs now flow onto scene layers, ports, and synthesized instance items, creating the anchor for upcoming hierarchy/properties/cross-probing tasks.
- Validation run:
  - `cmake --build build --config Release` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R Scene` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R Import` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R UiWorkflow` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R RunChecks` ✅

## Next Recommended Action
Start `P13-004` by enriching the Properties panel to show structured imported object provenance, artifact path, parser origin, and physical/logical metadata when an imported-design object is selected.
