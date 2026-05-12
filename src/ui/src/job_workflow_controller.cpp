#include "job_workflow_controller.hpp"
#include "main_window_state.hpp"

#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/activity_log_panel.hpp"

#include "aegis/rules/rule_engine.hpp"
#include "aegis/rules/electrical_rules.hpp"
#include "aegis/orchestration/job_pipeline.hpp"

#include <QTimer>
#include <QMetaObject>
#include <QDateTime>
#include <QString>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <filesystem>
#include <chrono>

namespace aegis::ui {

JobWorkflowController::JobWorkflowController(MainWindow& window,
                                               MainWindowState& state,
                                               QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_state(state)
{
}

void JobWorkflowController::execute_run_checks()
{
    if (m_state.has_loaded_import_package) {
        Q_UNUSED(start_imported_run_checks(false));
        return;
    }

    if (m_state.current_graph == nullptr) {
        const QString message = "Run Checks unavailable: no connectivity graph available";
        m_window.publish_ui_notification(message, ActivityLogSeverity::Error, 4000);
        return;
    }

    m_window.publish_ui_notification("Run Checks started using built-in desktop defaults", ActivityLogSeverity::Info, 2000);

    try {
        aegis::rules::RuleEngine engine;
        engine.register_rule(std::make_unique<aegis::rules::FloatingNetRule>());
        engine.register_rule(std::make_unique<aegis::rules::OpenCircuitRule>());
        engine.register_rule(std::make_unique<aegis::rules::ShortCircuitRule>());
        engine.register_rule(std::make_unique<aegis::rules::DomainTaggingRule>());

        aegis::rules::RuleContext context{
            *m_state.current_graph,
            aegis::graph::PropertyMap{},
            m_state.canvas != nullptr ? m_state.canvas->scene().design_name : std::string{}
        };

        auto violations = engine.run_all(context);
        aegis::rules::ViolationCollection collection{std::move(violations)};
        m_window.set_violations(collection);

        const QString message = QString("Run Checks completed: %1 violation(s) using built-in desktop defaults")
                                    .arg(collection.size());
        m_window.publish_ui_notification(message,
                                collection.empty() ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                5000);
    } catch (const std::exception& error) {
        const QString message = QString("Run Checks failed: %1").arg(error.what());
        m_window.publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
    }
}

bool JobWorkflowController::start_imported_run_checks(bool is_retry)
{
    if (m_state.active_job_id.has_value()) {
        m_window.publish_ui_notification("Run Checks already in progress through the local job pipeline", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    if (!m_state.has_loaded_import_package) {
        m_window.publish_ui_notification("Run Checks unavailable: no imported package is loaded", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    if (!imported_package_inputs_exist(m_state.loaded_import_package, m_state.loaded_import_base_path)) {
        m_state.last_job_retry_available = false;
        m_state.last_job_retry_reason = "Retry unavailable: imported package inputs are missing on disk";
        m_window.publish_ui_notification(m_state.last_job_retry_reason, ActivityLogSeverity::Error, 5000);
        m_window.update_action_states();
        return false;
    }

    aegis::orchestration::JobRequest request;
    request.package = m_state.loaded_import_package;
    request.base_path = m_state.loaded_import_base_path;
    request.options = m_state.job_pipeline_options;
    m_state.active_job_output_dir = std::filesystem::temp_directory_path() /
                                    ("aegis_ui_run_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    request.output_dir = m_state.active_job_output_dir;
    request.progress_callback = [this](const aegis::orchestration::JobProgressSnapshot& snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot]() {
            update_job_progress_ui(snapshot);
            const QString message = QString("Run Checks pipeline: %1 (%2/%3) — %4")
                                        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
                                        .arg(snapshot.completed_stages)
                                        .arg(snapshot.total_stages)
                                        .arg(QString::fromStdString(snapshot.message));
            m_window.publish_ui_notification(message, ActivityLogSeverity::Info, 1500);
        }, Qt::QueuedConnection);
    };

    const auto job_id = m_state.job_pipeline.submit(std::move(request));
    m_state.active_job_id = job_id;
    m_state.last_job_retry_available = false;
    m_state.last_job_retry_reason = "Retry unavailable while a local job is active";
    m_state.job_history.push_back({job_id,
                                   "Run Checks",
                                   QString::fromStdString(m_state.loaded_import_package.project().name),
                                   "queued",
                                   QString("Queued imported Run Checks job"),
                                   QString{},
                                   QString{},
                                   {},
                                   QDateTime::currentDateTime(),
                                   {},
                                   std::nullopt,
                                   std::nullopt});
    if (static_cast<int>(m_state.job_history.size()) > m_state.job_history_max_entries) {
        const auto excess = static_cast<int>(m_state.job_history.size()) - m_state.job_history_max_entries;
        m_state.job_history.erase(m_state.job_history.begin(),
                                  m_state.job_history.begin() + excess);
    }
    update_job_progress_ui({job_id,
                            aegis::orchestration::JobState::Queued,
                            aegis::orchestration::JobStage::None,
                            0,
                            5,
                            false,
                            m_state.loaded_import_package.project().name,
                            "Queued imported Run Checks job",
                            std::nullopt,
                            std::nullopt});
    if (m_state.job_poll_timer == nullptr) {
        m_state.job_poll_timer = new QTimer(&m_window);
        m_state.job_poll_timer->setInterval(25);
        connect(m_state.job_poll_timer, &QTimer::timeout, this, &JobWorkflowController::finalize_active_job);
    }
    m_state.job_poll_timer->start();
    m_window.publish_ui_notification(is_retry
                                ? "Retrying Run Checks using imported package content via local job pipeline"
                                : "Run Checks started using imported package content via local job pipeline",
                            ActivityLogSeverity::Info,
                            2000);
    m_window.update_action_states();
    return true;
}

bool JobWorkflowController::can_retry_last_job() const
{
    return !m_state.active_job_id.has_value()
        && m_state.has_loaded_import_package
        && m_state.last_job_retry_available
        && imported_package_inputs_exist(m_state.loaded_import_package, m_state.loaded_import_base_path);
}

void JobWorkflowController::update_job_progress_ui(const aegis::orchestration::JobProgressSnapshot& snapshot)
{
    m_state.last_job_snapshot = snapshot;
    m_state.last_job_progress_text = QString("Job: %1 (%2/%3) — %4")
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
        .arg(snapshot.completed_stages)
        .arg(snapshot.total_stages)
        .arg(QString::fromStdString(snapshot.message));
    if (m_state.job_progress_label != nullptr) {
        m_state.job_progress_label->setText(m_state.last_job_progress_text);
        m_state.job_progress_label->setVisible(snapshot.state == aegis::orchestration::JobState::Running
                                               || snapshot.state == aegis::orchestration::JobState::Queued
                                               || snapshot.state == aegis::orchestration::JobState::Cancelling);
    }

    const QString progress_line = QString("%1 | %2 (%3/%4) | %5")
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.state)))
        .arg(QString::fromStdString(aegis::orchestration::to_string(snapshot.stage)))
        .arg(snapshot.completed_stages)
        .arg(snapshot.total_stages)
        .arg(QString::fromStdString(snapshot.message));
    for (auto& entry : m_state.job_history) {
        if (entry.job_id != snapshot.job_id) {
            continue;
        }
        entry.state = QString::fromStdString(aegis::orchestration::to_string(snapshot.state));
        entry.summary = QString("%1 — %2").arg(entry.action_name, progress_line);
        if (entry.progress_history.isEmpty() || entry.progress_history.back() != progress_line) {
            entry.progress_history.push_back(progress_line);
        }
        entry.json_report_path = snapshot.json_report_path;
        entry.html_report_path = snapshot.html_report_path;
        break;
    }
    refresh_job_history_panel();
    m_window.update_action_states();
}

void JobWorkflowController::refresh_job_history_panel()
{
    if (m_state.job_history_list == nullptr || m_state.job_history_details == nullptr
        || m_state.job_history_open_json_button == nullptr || m_state.job_history_open_html_button == nullptr) {
        return;
    }

    const int current_row = m_state.job_history_list->currentRow();
    m_state.job_history_list->blockSignals(true);
    m_state.job_history_list->clear();
    for (const auto& entry : m_state.job_history) {
        const QString started = entry.started_at.isValid()
            ? entry.started_at.toString(Qt::ISODate)
            : QString("unknown-start");
        m_state.job_history_list->addItem(QString("[%1] %2 — %3 — %4")
                                              .arg(entry.state, entry.action_name, entry.project_name, started));
    }
    m_state.job_history_list->blockSignals(false);

    if (m_state.job_history.empty()) {
        m_state.job_history_details->setPlainText("No local workflow jobs yet.");
        m_state.job_history_open_json_button->setEnabled(false);
        m_state.job_history_open_html_button->setEnabled(false);
        return;
    }

    const int bounded_row = std::clamp(current_row < 0 ? static_cast<int>(m_state.job_history.size()) - 1 : current_row,
                                       0,
                                       static_cast<int>(m_state.job_history.size()) - 1);
    const bool restore_signals = m_state.job_history_list->blockSignals(true);
    m_state.job_history_list->setCurrentRow(bounded_row);
    m_state.job_history_list->blockSignals(restore_signals);

    const auto& entry = m_state.job_history.at(static_cast<std::size_t>(bounded_row));
    QStringList lines;
    lines.append(QString("Action: %1").arg(entry.action_name));
    lines.append(QString("Project: %1").arg(entry.project_name));
    lines.append(QString("State: %1").arg(entry.state));
    lines.append(QString("Started: %1").arg(entry.started_at.isValid() ? entry.started_at.toString(Qt::ISODate) : QString("n/a")));
    lines.append(QString("Finished: %1").arg(entry.finished_at.isValid() ? entry.finished_at.toString(Qt::ISODate) : QString("in progress")));
    if (!entry.result_summary.trimmed().isEmpty()) {
        lines.append(QString("Result: %1").arg(entry.result_summary));
    }
    if (!entry.error_message.trimmed().isEmpty()) {
        lines.append(QString("Error: %1").arg(entry.error_message));
    }
    if (entry.json_report_path.has_value()) {
        lines.append(QString("JSON report: %1").arg(QString::fromStdString(entry.json_report_path->string())));
    }
    if (entry.html_report_path.has_value()) {
        lines.append(QString("HTML report: %1").arg(QString::fromStdString(entry.html_report_path->string())));
    }
    lines.append(QString{});
    lines.append("Progress history:");
    if (entry.progress_history.isEmpty()) {
        lines.append("- No progress updates recorded");
    } else {
        for (const auto& progress : entry.progress_history) {
            lines.append(QString("- %1").arg(progress));
        }
    }
    m_state.job_history_details->setPlainText(lines.join('\n'));
    m_state.job_history_open_json_button->setEnabled(entry.json_report_path.has_value() && std::filesystem::exists(*entry.json_report_path));
    m_state.job_history_open_html_button->setEnabled(entry.html_report_path.has_value() && std::filesystem::exists(*entry.html_report_path));
}

bool JobWorkflowController::open_selected_job_history_report(bool html_report)
{
    if (m_state.job_history_list == nullptr) {
        return false;
    }
    const int row = m_state.job_history_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_state.job_history.size())) {
        m_window.publish_ui_notification("Job report open unavailable: no recent job selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const auto& entry = m_state.job_history.at(static_cast<std::size_t>(row));
    const auto& path = html_report ? entry.html_report_path : entry.json_report_path;
    if (!path.has_value() || !std::filesystem::exists(*path)) {
        m_window.publish_ui_notification(html_report ? "HTML report unavailable for selected job" : "JSON report unavailable for selected job",
                                ActivityLogSeverity::Warning,
                                4000);
        refresh_job_history_panel();
        return false;
    }

    const QString report_path = QString::fromStdString(path->string());
    if (m_state.report_opener && !m_state.report_opener(report_path)) {
        m_window.publish_ui_notification(QString("Failed to open job report: %1").arg(report_path), ActivityLogSeverity::Error, 5000);
        return false;
    }

    m_window.publish_ui_notification(QString("Opened job report: %1").arg(report_path), ActivityLogSeverity::Info, 4000);
    return true;
}

void JobWorkflowController::finalize_active_job()
{
    if (!m_state.active_job_id.has_value()) {
        if (m_state.job_poll_timer != nullptr) {
            m_state.job_poll_timer->stop();
        }
        return;
    }

    if (const auto snapshot = m_state.job_pipeline.snapshot(*m_state.active_job_id); snapshot.has_value()) {
        update_job_progress_ui(*snapshot);
    }

    const auto result = m_state.job_pipeline.result(*m_state.active_job_id);
    if (!result.has_value() || (result->state != aegis::orchestration::JobState::Completed
                                && result->state != aegis::orchestration::JobState::Failed
                                && result->state != aegis::orchestration::JobState::Cancelled)) {
        return;
    }

    if (m_state.job_poll_timer != nullptr) {
        m_state.job_poll_timer->stop();
    }

    if (m_state.job_progress_label != nullptr) {
        m_state.job_progress_label->setVisible(false);
    }

    QString history_result_summary;
    QString history_error_message;
    if (result->state == aegis::orchestration::JobState::Completed) {
        aegis::rules::ViolationCollection collection{result->violations};
        m_window.set_violations(collection);
        m_state.last_job_retry_available = false;
        m_state.last_job_retry_reason = "Retry unavailable: last local job completed successfully";
        history_result_summary = QString("Completed with %1 violation(s)").arg(collection.size());
        const QString message = QString("Run Checks completed: %1 violation(s) using imported package content via local job pipeline")
                                    .arg(collection.size());
        m_window.publish_ui_notification(message,
                                collection.empty() ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                                5000);
    } else if (result->state == aegis::orchestration::JobState::Cancelled) {
        m_state.last_job_retry_available = imported_package_inputs_exist(m_state.loaded_import_package, m_state.loaded_import_base_path);
        m_state.last_job_retry_reason = m_state.last_job_retry_available
            ? QString("Retry available for canceled local job")
            : QString("Retry unavailable: imported package inputs are missing on disk");
        history_result_summary = "Cancelled";
        m_window.publish_ui_notification("Run Checks canceled in local job pipeline", ActivityLogSeverity::Warning, 5000);
    } else {
        m_state.last_job_retry_available = imported_package_inputs_exist(m_state.loaded_import_package, m_state.loaded_import_base_path);
        m_state.last_job_retry_reason = m_state.last_job_retry_available
            ? QString("Retry available for failed local job")
            : QString("Retry unavailable: imported package inputs are missing on disk");
        history_result_summary = "Failed";
        history_error_message = QString::fromStdString(result->error_message);
        const QString message = QString("Run Checks failed: %1").arg(QString::fromStdString(result->error_message));
        m_window.publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
        if (m_state.last_job_retry_available) {
            m_window.append_activity_log("Retry available for failed local job", ActivityLogSeverity::Info);
        }
    }

    for (auto& entry : m_state.job_history) {
        if (entry.job_id != result->job_id) {
            continue;
        }
        entry.state = QString::fromStdString(aegis::orchestration::to_string(result->state));
        entry.result_summary = history_result_summary;
        entry.error_message = history_error_message;
        entry.finished_at = QDateTime::currentDateTime();
        entry.json_report_path = result->json_report_path;
        entry.html_report_path = result->html_report_path;
        entry.summary = QString("%1 — %2").arg(entry.action_name, history_result_summary);
        break;
    }

    m_state.active_job_id.reset();
    refresh_job_history_panel();
    m_window.update_action_states();
}

} // namespace aegis::ui
