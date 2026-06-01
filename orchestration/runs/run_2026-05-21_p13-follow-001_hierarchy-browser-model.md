# Run Log — 2026-05-21 — P13-FOLLOW-001

## Objective
Migrate the HierarchyBrowser from widget-backed `QTreeWidget` semantics to a scalable `QAbstractItemModel` + `QTreeView` approach suitable for very large imported designs, while keeping the MainWindow integration API stable.

## Scope
- Update HierarchyBrowser UI implementation to use model/view + proxy filtering.
- Preserve grouping and sort order.
- Add regression coverage for large-session lazy behavior.

## Changes
- `HierarchyBrowserPanel` now sets a `QTreeView` model and uses a `QSortFilterProxyModel` (subclass) for filtering:
  - Filter/search is proxy-driven rather than iterating/hiding tree rows.
  - Large sessions avoid eager auto-expansion; small sessions keep categories expanded for the legacy feel.
  - Added defensive teardown: panel destructor clears the model session to avoid dangling-session crashes in Qt destruction order.
- `HierarchyBrowserModel`:
  - Removed debug destructor output.
  - Added stable-id index (`stable_id -> (group,row)`) for fast selection restoration.
- Tests:
  - Added `HierarchyBrowserPanel avoids eager expansion for large sessions` to cover lazy expansion semantics and selection on a stress fixture.

## Files Touched
- `src/ui/src/hierarchy_browser_panel.cpp`
- `src/ui/include/aegis/ui/hierarchy_browser_panel.hpp`
- `src/ui/src/hierarchy_browser_model.cpp`
- `src/ui/include/aegis/ui/hierarchy_browser_model.hpp`
- `tests/unit/ui/test_hierarchy_browser.cpp`
- `orchestration/tasks.yaml`
- `orchestration/HANDOFF.md`

## Test Commands (Windows / Release)
- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure -R HierarchyBrowser`
- `ctest --test-dir build -C Release --output-on-failure -R UiWorkflow`

## Results
- Build: ✅
- `HierarchyBrowser` tests: ✅
- `UiWorkflow` tests: ✅

## Notes / Follow-ups
- If/when 1M+ object sessions become common in interactive usage, consider moving expensive index/sort construction off the UI thread and reporting progress via the existing async realization pipeline (P13-010).

