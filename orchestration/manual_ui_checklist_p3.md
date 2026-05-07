# Sprint 3 Visualization Manual UI Checklist

## Setup
- Launch `build/bin/Release/aegis-perc.exe`
- Use **Open Sample** to load the bundled inverter sample
- Confirm docks are visible: Layers, Properties, Violations, Report Preview, Graph Explorer, Trace, Log

## Canvas navigation
- Pan with middle mouse drag
- Pan with space + left drag
- Zoom with mouse wheel
- Run **Fit View** and **Reset View** actions
- Confirm zoom/status updates remain stable after repeated navigation

## Selection and properties
- Click geometry, ports, and annotations where available
- Ctrl-click to multi-select
- Use **Clear Selection** and confirm Properties updates immediately

## Layers and overlays
- Toggle individual layers in the Layers dock
- Hide/show a layer and confirm canvas updates without reload
- Toggle violation overlays from the toolbar/menu
- Toggle the grid from the toolbar/menu

## Violations and filtering
- Select a violation in the Violations dock and confirm canvas centering
- Filter by layer, severity, and search text
- Confirm overlay count and report preview update with the filtered set
- Clear filters and confirm the full set returns

## Connectivity and graph tools
- Search for a node in Graph Explorer
- Trigger trace from graph selection and confirm canvas highlighting
- Use Trace clear/focus controls
- Hide/show Graph Explorer from the View menu

## Heatmap and report preview
- Enable heatmap and adjust opacity
- Confirm empty-state text appears for locationless/empty violation sets
- Confirm Report Preview shows design name, layer count, severity summary, selected violation details, and snapshot status

## Performance diagnostics
- Enable developer performance metrics
- Confirm status bar metrics update while navigating
- Confirm large synthetic scenes still render and interaction remains responsive
