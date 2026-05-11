# Handoff

## Last Updated
2026-05-10

## Current Status
`P13-004` is done: the Properties panel renders structured imported-object identity, provenance, metadata, and scene properties when an imported design session is active. All 420 tests pass.

## Last Completed Task
`P13-004` — Expose commercial-grade imported object properties and provenance inspection

## Current Task
No implementation task in progress

## Ready Tasks
- `P13-FOLLOW-001` — Migrate HierarchyBrowser to QAbstractItemModel + QTreeView for million-object scalability

## Blocked Tasks
- None recorded in the current trimmed `tasks.yaml`

## Important Notes
- Imported package commit now stores `loaded_import_session`, installs an imported `UiScene`, and binds the session graph into the desktop shell immediately after `Load Project`.
- `build_imported_design_scene()` is the compatibility seam for imported 2D realization; future P13 UI surfaces should consume scene/session metadata from there rather than reparsing DEF/LEF directly.
- Instance placement rectangles now render from DEF placement coordinates. When LEF macro dimensions are missing, the scene builder falls back to deterministic marker sizes and logs explicit diagnostics instead of leaving the scene blank.
- Stable imported object IDs now flow onto scene layers, ports, and synthesized instance items, creating the anchor for upcoming hierarchy/properties/cross-probing tasks.
- `PropertiesPanel::set_session()` binds an `ImportedDesignSession` so that selecting an imported object on the canvas or hierarchy browser displays:
  - **Identity**: stable ID, human-readable name, type label
  - **Provenance**: artifact ID, source path, parser name, origin line/column
  - **Metadata**: key/value table from `ImportedDesignObject::metadata`
  - **Scene Properties**: kind, layer, shape, z-order, bounding box, point count
  with HTML formatting and proper empty-state guidance for no-scene / no-selection / no-session / unknown-selection.
- Scene-only selection (no session bound) still shows clean scene properties without the imported sections, preserving sample-mode behavior.
- `MainWindow` wires `properties_panel->set_session()` on import load and clears it on sample load, maintaining consistent lifecycle.
- Validation run:
  - `cmake --build build --config Release` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R Scene` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R Import` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R UiWorkflow` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R RunChecks` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R PropertiesPanel` ✅
  - `ctest --test-dir build -C Release --output-on-failure -R HierarchyBrowser` ✅
  - `ctest --test-dir build -C Release --output-on-failure` ✅ (420/420)

## Next Recommended Action
Start `P13-FOLLOW-001` by migrating `HierarchyBrowserPanel` from `QTreeWidget` to `QAbstractItemModel` + `QTreeView` with on-demand lazy loading for million-object scalability, or pick up the next ready task from `tasks.yaml`.
