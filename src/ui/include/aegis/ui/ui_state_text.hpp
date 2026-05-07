#pragma once

#include <QString>

namespace aegis::ui::state_text {

inline QString canvas_empty()
{
    return "Load a design to inspect layout geometry.";
}

inline QString properties_no_scene()
{
    return "Load a design to inspect item properties.";
}

inline QString properties_no_selection()
{
    return "Select layout items to inspect their properties.";
}

inline QString violations_empty()
{
    return "Run checks to populate the violations panel.";
}

inline QString violations_filtered_empty()
{
    return "No violations match the current filters. Clear filters or adjust the criteria.";
}

inline QString violations_no_selection()
{
    return "Select a violation to inspect its details.";
}

inline QString trace_no_graph()
{
    return "Load a design with connectivity data to trace nets and pins.";
}

inline QString trace_idle()
{
    return "Enter a net, port, or device.pin to start a trace.";
}

inline QString graph_no_graph()
{
    return "Load a design with connectivity data to explore graph nodes.";
}

inline QString graph_empty()
{
    return "Connectivity graph is empty.";
}

inline QString graph_enter_search()
{
    return "Enter a graph node name to search.";
}

inline QString report_summary_empty()
{
    return "Load a design to preview report content.";
}

inline QString report_snapshot_empty()
{
    return "Load a design to preview a workspace snapshot.";
}

inline QString report_snapshot_refresh_needed()
{
    return "Snapshot unavailable until the preview is refreshed.";
}

inline QString heatmap_empty()
{
    return "Run checks or clear filters to generate violation heatmap data.";
}

} // namespace aegis::ui::state_text
