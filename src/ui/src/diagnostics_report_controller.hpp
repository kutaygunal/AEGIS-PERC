#pragma once

#include "main_window_state.hpp"

#include <QObject>
#include <QString>
#include <QPointF>

namespace aegis::ui {

class MainWindow;

class DiagnosticsReportController : public QObject {
    Q_OBJECT

public:
    DiagnosticsReportController(MainWindow& window,
                                MainWindowState& state,
                                QObject* parent = nullptr);

    void set_violations(aegis::rules::ViolationCollection violations);
    bool export_report_preview(bool html_export);
    void refresh_diagnostics_panel();
    bool navigate_current_violation_relationships();
    bool select_related_violations_for_current_diagnostic();
    bool select_related_violations_for_current_artifact();
    bool request_trace_by_name(const QString& stable_name);
    bool request_trace_from_selection();
    bool request_trace_from_current_violation();
    void clear_trace();
    void focus_trace();
    void update_trace_controls();

private:
    MainWindow& m_window;
    MainWindowState& m_state;
};

} // namespace aegis::ui
