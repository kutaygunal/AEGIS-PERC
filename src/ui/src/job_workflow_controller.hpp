#pragma once

#include "aegis/orchestration/job_pipeline.hpp"
#include "main_window_state.hpp"

#include <QObject>
#include <QString>

namespace aegis::ui {

class MainWindow;

class JobWorkflowController : public QObject {
    Q_OBJECT

public:
    JobWorkflowController(MainWindow& window,
                          MainWindowState& state,
                          QObject* parent = nullptr);

    void execute_run_checks();
    bool start_imported_run_checks(bool is_retry);
    bool can_retry_last_job() const;
    void update_job_progress_ui(const aegis::orchestration::JobProgressSnapshot& snapshot);
    void refresh_job_history_panel();
    bool open_selected_job_history_report(bool html_report);
    void finalize_active_job();

private:
    MainWindow& m_window;
    MainWindowState& m_state;
};

} // namespace aegis::ui
