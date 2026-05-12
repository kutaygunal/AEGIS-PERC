#include "diagnostics_report_controller.hpp"
#include "main_window_state.hpp"
#include "aegis/ui/main_window.hpp"

#include "aegis/ui/ui_state_text.hpp"
#include "aegis/ui/violation_explorer_panel.hpp"
#include "aegis/ui/report_preview_panel.hpp"
#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/trace_panel.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/rules/violation.hpp"
#include "aegis/storage/project_package.hpp"

#include <QFileDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSignalBlocker>
#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QClipboard>
#include <QApplication>

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace aegis::ui {

DiagnosticsReportController::DiagnosticsReportController(MainWindow& window,
                                                           MainWindowState& state,
                                                           QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_state(state)
{
}

void DiagnosticsReportController::set_violations(aegis::rules::ViolationCollection violations)
{
    if (m_state.violation_explorer != nullptr) {
        m_state.violation_explorer->set_violations(violations);
    }
    if (m_state.report_preview != nullptr) {
        m_state.report_preview->set_violations(violations);
    }
    m_state.latest_violations = violations.violations();
    if (m_state.canvas != nullptr) {
        m_state.canvas->set_violations(std::move(violations));
        if (m_state.report_preview != nullptr) {
            m_state.report_preview->set_snapshot(m_state.canvas->grab());
        }
    }
    m_window.refresh_workspace_summary();
    refresh_diagnostics_panel();
    m_window.update_action_states();
}

bool DiagnosticsReportController::export_report_preview(bool html_export)
{
    if (m_state.report_preview == nullptr) {
        return false;
    }
    const QString format = html_export ? QString("html") : QString("json");
    QString target_path;
    if (m_state.report_export_path_picker) {
        target_path = m_state.report_export_path_picker(format);
    } else {
        target_path = QFileDialog::getSaveFileName(&m_window,
                                                   html_export ? "Export HTML Report" : "Export JSON Report",
                                                   html_export ? "aegis_report.html" : "aegis_report.json",
                                                   html_export ? "HTML Files (*.html)" : "JSON Files (*.json)");
    }
    if (target_path.trimmed().isEmpty()) {
        m_state.report_preview->set_action_status_for_host(html_export
            ? "HTML export canceled"
            : "JSON export canceled");
        m_window.publish_ui_notification(html_export ? "HTML export canceled" : "JSON export canceled", ActivityLogSeverity::Info, 3000);
        return false;
    }

    const auto filtered = m_state.violation_explorer != nullptr
        ? m_state.violation_explorer->filtered_violations()
        : aegis::rules::ViolationCollection{};
    std::vector<aegis::rules::Violation> violations;
    violations.reserve(filtered.size());
    for (const auto& violation : filtered.violations()) {
        violations.push_back(violation);
    }

    nlohmann::json import_diagnostics = nlohmann::json::array();
    if (m_state.has_loaded_import_package) {
        for (const auto& diagnostic : m_state.loaded_import_package.diagnostics()) {
            import_diagnostics.push_back({
                {"severity", aegis::storage::to_string(diagnostic.severity)},
                {"code", diagnostic.code},
                {"message", diagnostic.message},
                {"artifact_id", diagnostic.artifact_id.has_value() ? nlohmann::json(*diagnostic.artifact_id) : nlohmann::json(nullptr)}
            });
        }
    }

    nlohmann::json runtime_diagnostics = nlohmann::json::array();
    for (auto it = m_state.job_history.rbegin(); it != m_state.job_history.rend(); ++it) {
        if (it->json_report_path.has_value() && std::filesystem::exists(*it->json_report_path)) {
            try {
                std::ifstream input(*it->json_report_path);
                nlohmann::json existing;
                input >> existing;
                if (existing.contains("runtime_diagnostics") && existing["runtime_diagnostics"].is_array()) {
                    runtime_diagnostics = existing["runtime_diagnostics"];
                }
            } catch (...) {
            }
            break;
        }
    }
    if (runtime_diagnostics.empty() && !m_state.job_history.empty()) {
        const auto& latest = m_state.job_history.back();
        if (!latest.error_message.trimmed().isEmpty()) {
            runtime_diagnostics.push_back({{"severity", "error"}, {"message", latest.error_message.toStdString()}});
        }
        for (const auto& line : latest.progress_history) {
            runtime_diagnostics.push_back({{"severity", "info"}, {"message", line.toStdString()}});
        }
    }

    nlohmann::json export_json{
        {"project", {
            {"design_name", m_state.canvas != nullptr ? m_state.canvas->scene().design_name : std::string{}},
            {"imported_project_name", m_state.has_loaded_import_package ? m_state.loaded_import_package.project().name : std::string{}},
            {"validation_status", m_state.has_loaded_import_package ? aegis::storage::to_string(m_state.loaded_import_package.validation_status()) : std::string{"not_imported"}}
        }},
        {"summary", {
            {"text", m_state.report_preview->summary_text().toStdString()},
            {"snapshot_status", m_state.report_preview->snapshot_status_text().toStdString()},
            {"activity_log", m_window.activity_log_entries().join('\n').toStdString()},
            {"violation_count", violations.size()}
        }},
        {"import_diagnostics", import_diagnostics},
        {"runtime_diagnostics", runtime_diagnostics},
        {"violations", violations}
    };

    try {
        std::filesystem::create_directories(std::filesystem::path(target_path.toStdString()).parent_path());
        if (html_export) {
            QStringList lines;
            lines.append("<!doctype html><html><head><meta charset=\"utf-8\"\u003e<title>AEGIS-PERC Report Preview Export</title></head><body>");
            lines.append("<h1>AEGIS-PERC Report Preview Export</h1>");
            lines.append(QString("<p><strong>Design:</strong> %1</p>").arg(html_escape(m_state.canvas != nullptr
                ? QString::fromStdString(m_state.canvas->scene().design_name)
                : QString{})));
            if (m_state.has_loaded_import_package) {
                lines.append(QString("<p><strong>Imported project:</strong> %1</p>").arg(html_escape(QString::fromStdString(m_state.loaded_import_package.project().name))));
            }
            lines.append(QString("<p><strong>Snapshot:</strong> %1</p>").arg(html_escape(m_state.report_preview->snapshot_status_text())));
            lines.append(QString("<h2>Summary</h2><pre>%1</pre>").arg(html_escape(m_state.report_preview->summary_text())));
            lines.append("<h2>Violations</h2><ul>");
            for (const auto& violation : violations) {
                lines.append(QString("<li>[%1] %2: %3</li>")
                                 .arg(html_escape(QString::fromStdString(aegis::rules::severity_to_string(violation.severity))))
                                 .arg(html_escape(QString::fromStdString(violation.rule_id)))
                                 .arg(html_escape(QString::fromStdString(violation.message))));
            }
            if (violations.empty()) {
                lines.append("<li>No violations in current preview</li>");
            }
            lines.append("</ul><h2>Import Diagnostics</h2><ul>");
            if (import_diagnostics.empty()) {
                lines.append("<li>No import diagnostics</li>");
            } else {
                for (const auto& diagnostic : import_diagnostics) {
                    lines.append(QString("<li>[%1] %2</li>")
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("severity", "info"))))
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("message", "")))));
                }
            }
            lines.append("</ul><h2>Runtime Diagnostics</h2><ul>");
            if (runtime_diagnostics.empty()) {
                lines.append("<li>No runtime diagnostics</li>");
            } else {
                for (const auto& diagnostic : runtime_diagnostics) {
                    lines.append(QString("<li>[%1] %2</li>")
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("severity", "info"))))
                                     .arg(html_escape(QString::fromStdString(diagnostic.value("message", "")))));
                }
            }
            lines.append("</ul></body></html>");
            std::ofstream out(target_path.toStdString(), std::ios::binary);
            out << lines.join('\n').toStdString();
        } else {
            std::ofstream out(target_path.toStdString(), std::ios::binary);
            out << export_json.dump(2);
        }
    } catch (const std::exception& error) {
        const QString message = QString("Report export failed: %1").arg(error.what());
        m_state.report_preview->set_action_status_for_host(message);
        m_window.publish_ui_notification(message, ActivityLogSeverity::Error, 5000);
        return false;
    }

    const QString message = QString("Exported report preview to %1").arg(target_path);
    m_state.report_preview->set_action_status_for_host(message);
    m_window.publish_ui_notification(message, ActivityLogSeverity::Info, 5000);
    return true;
}

void DiagnosticsReportController::refresh_diagnostics_panel()
{
    m_state.diagnostics_entries.clear();

    const auto* package = !m_state.pending_import_package.artifacts().empty()
        ? &m_state.pending_import_package
        : (m_state.has_loaded_import_package ? &m_state.loaded_import_package : nullptr);
    if (package != nullptr) {
        for (const auto& diagnostic : package->diagnostics()) {
            MainWindowState::DiagnosticEntry entry;
            entry.severity = QString::fromStdString(aegis::storage::to_string(diagnostic.severity));
            entry.source = "import";
            entry.summary = QString::fromStdString(diagnostic.message);
            entry.details = QString("Import diagnostic\nSeverity: %1\nCode: %2\nMessage: %3")
                .arg(entry.severity,
                     QString::fromStdString(diagnostic.code),
                     QString::fromStdString(diagnostic.message));
            entry.blocking = diagnostic.severity == aegis::storage::DiagnosticSeverity::Error;
            if (diagnostic.artifact_id.has_value()) {
                entry.artifact_id = QString::fromStdString(*diagnostic.artifact_id);
                if (const auto* artifact = package->find_artifact_by_id(*diagnostic.artifact_id)) {
                    entry.artifact_path = QString::fromStdString(artifact->path.generic_string());
                    entry.details += QString("\nArtifact: %1").arg(entry.artifact_path);
                }
            }
            m_state.diagnostics_entries.push_back(std::move(entry));
        }
    }

    for (const auto& violation : m_state.latest_violations) {
        MainWindowState::DiagnosticEntry entry;
        entry.severity = QString::fromStdString(aegis::rules::severity_to_string(violation.severity));
        entry.source = "run";
        entry.summary = QString::fromStdString(violation.message);
        entry.violation_id = QString::fromStdString(violation.id);
        entry.details = QString("Run diagnostic\nViolation: %1\nRule: %2\nSeverity: %3\nMessage: %4")
            .arg(entry.violation_id,
                 QString::fromStdString(violation.rule_id),
                 entry.severity,
                 QString::fromStdString(violation.message));
        entry.blocking = violation.severity == aegis::rules::Severity::Error || violation.severity == aegis::rules::Severity::Fatal;
        m_state.diagnostics_entries.push_back(std::move(entry));
    }

    if (m_state.diagnostics_table == nullptr || m_state.diagnostics_details == nullptr || m_state.diagnostics_severity_filter == nullptr) {
        return;
    }

    const QString severity_filter = m_state.diagnostics_severity_filter->currentText().trimmed().toLower();
    std::vector<int> visible_indexes;
    for (int i = 0; i < static_cast<int>(m_state.diagnostics_entries.size()); ++i) {
        const auto& entry = m_state.diagnostics_entries[static_cast<std::size_t>(i)];
        if (severity_filter != "all" && !severity_filter.isEmpty() && entry.severity.compare(severity_filter, Qt::CaseInsensitive) != 0) {
            continue;
        }
        visible_indexes.push_back(i);
    }

    const QSignalBlocker blocker(m_state.diagnostics_table);
    m_state.diagnostics_table->clearContents();
    m_state.diagnostics_table->setRowCount(static_cast<int>(visible_indexes.size()));
    for (int row = 0; row < static_cast<int>(visible_indexes.size()); ++row) {
        const auto& entry = m_state.diagnostics_entries[static_cast<std::size_t>(visible_indexes[static_cast<std::size_t>(row)])];
        auto* severity_item = new QTableWidgetItem(entry.severity);
        severity_item->setData(Qt::UserRole, visible_indexes[static_cast<std::size_t>(row)]);
        if (entry.blocking) {
            severity_item->setBackground(QBrush(QColor(255, 225, 225)));
        }
        m_state.diagnostics_table->setItem(row, 0, severity_item);
        m_state.diagnostics_table->setItem(row, 1, new QTableWidgetItem(entry.source));
        m_state.diagnostics_table->setItem(row, 2, new QTableWidgetItem(entry.summary));
    }

    if (visible_indexes.empty()) {
        m_state.diagnostics_details->setPlainText("No diagnostics match the current filters. Clear filters or adjust the severity selection.");
        if (m_state.diagnostics_related_button != nullptr) {
            m_state.diagnostics_related_button->setEnabled(false);
        }
        return;
    }

    int row = m_state.diagnostics_table->currentRow();
    if (row < 0 || row >= m_state.diagnostics_table->rowCount()) {
        row = 0;
        m_state.diagnostics_table->selectRow(row);
    }
    const auto* severity_item = m_state.diagnostics_table->item(row, 0);
    if (severity_item == nullptr) {
        m_state.diagnostics_details->setPlainText("Select a diagnostic to inspect its details.");
        if (m_state.diagnostics_related_button != nullptr) {
            m_state.diagnostics_related_button->setEnabled(false);
        }
        return;
    }
    const int entry_index = severity_item->data(Qt::UserRole).toInt();
    const auto& entry = m_state.diagnostics_entries[static_cast<std::size_t>(entry_index)];
    m_state.diagnostics_details->setPlainText(entry.details);

    if (entry.source == "import" && !entry.artifact_path.trimmed().isEmpty() && m_state.import_artifact_table != nullptr) {
        for (int artifact_row = 0; artifact_row < m_state.import_artifact_table->rowCount(); ++artifact_row) {
            if (auto* item = m_state.import_artifact_table->item(artifact_row, 0); item != nullptr
                && item->text() == entry.artifact_path) {
                m_state.import_artifact_table->selectRow(artifact_row);
                break;
            }
        }
    } else if (entry.source == "run" && !entry.violation_id.trimmed().isEmpty() && m_state.violation_explorer != nullptr) {
        for (int violation_row = 0; violation_row < m_state.violation_explorer->violation_count(); ++violation_row) {
            m_state.violation_explorer->select_row(violation_row);
            if (m_state.violation_explorer->current_violation() != nullptr
                && QString::fromStdString(m_state.violation_explorer->current_violation()->id) == entry.violation_id) {
                break;
            }
        }
    }

    if (m_state.diagnostics_related_button != nullptr) {
        m_state.diagnostics_related_button->setEnabled(true);
    }
}

bool DiagnosticsReportController::navigate_current_violation_relationships()
{
    if (m_state.violation_explorer == nullptr || m_state.violation_explorer->current_violation() == nullptr) {
        m_window.publish_ui_notification("Navigation unavailable: no current violation selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const auto& violation = *m_state.violation_explorer->current_violation();
    bool opened_any = false;

    const QStringList graph_targets{
        violation.location.pin_name.has_value() ? QString::fromStdString(*violation.location.pin_name) : QString{},
        violation.location.net_name.has_value() ? QString::fromStdString(*violation.location.net_name) : QString{},
        violation.location.device_name.has_value() ? QString::fromStdString(*violation.location.device_name) : QString{},
        metadata_value(violation, {"pin_name", "graph_pin"}),
        metadata_value(violation, {"net_name", "graph_net"}),
        metadata_value(violation, {"device", "device_name", "graph_node"})
    };
    for (const auto& target : graph_targets) {
        if (!target.trimmed().isEmpty() && m_window.search_graph_node(target)) {
            opened_any = true;
            break;
        }
    }

    const QString artifact_id = metadata_value(violation, {"artifact_id", "source_artifact_id"});
    const QString artifact_path = metadata_value(violation, {"artifact_path", "source_artifact_path"});
    if ((!artifact_id.trimmed().isEmpty() || !artifact_path.trimmed().isEmpty()) && m_state.import_artifact_table != nullptr) {
        for (int row = 0; row < m_state.import_artifact_table->rowCount(); ++row) {
            auto* item = m_state.import_artifact_table->item(row, 0);
            if (item == nullptr) {
                continue;
            }
            const QString row_artifact_id = item->data(Qt::UserRole).toString();
            const QString row_artifact_path = item->text();
            if ((!artifact_id.trimmed().isEmpty() && row_artifact_id.compare(artifact_id, Qt::CaseInsensitive) == 0)
                || (!artifact_path.trimmed().isEmpty()
                    && QFileInfo(row_artifact_path).filePath().compare(QFileInfo(artifact_path).filePath(), Qt::CaseInsensitive) == 0)) {
                m_state.import_artifact_table->selectRow(row);
                opened_any = true;
                break;
            }
        }
    }

    if (!opened_any) {
        m_window.publish_ui_notification("No related graph or import metadata link could be resolved for the current violation", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    m_window.publish_ui_notification("Opened related graph/import context for current violation", ActivityLogSeverity::Info, 4000);
    return true;
}

bool DiagnosticsReportController::select_related_violations_for_current_diagnostic()
{
    if (m_state.diagnostics_table == nullptr || m_state.violation_explorer == nullptr) {
        return false;
    }
    const int row = m_state.diagnostics_table->currentRow();
    if (row < 0) {
        m_window.publish_ui_notification("Related violation navigation unavailable: no diagnostic selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto* severity_item = m_state.diagnostics_table->item(row, 0);
    if (severity_item == nullptr) {
        m_window.publish_ui_notification("Related violation navigation unavailable: diagnostic details are incomplete", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto& entry = m_state.diagnostics_entries.at(static_cast<std::size_t>(severity_item->data(Qt::UserRole).toInt()));

    QStringList related_ids;
    if (!entry.violation_id.trimmed().isEmpty()) {
        related_ids.append(entry.violation_id);
    } else {
        for (const auto& violation : m_state.latest_violations) {
            if (violation_matches_artifact(violation, entry.artifact_id, entry.artifact_path)) {
                related_ids.append(QString::fromStdString(violation.id));
            }
        }
    }

    const int selected = m_state.violation_explorer->select_violation_ids(related_ids);
    if (selected <= 0) {
        m_window.publish_ui_notification("No related visible violations were found for the selected diagnostic", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    m_window.publish_ui_notification(QString("Opened %1 related violation(s) from selected diagnostic").arg(selected), ActivityLogSeverity::Info, 4000);
    return true;
}

bool DiagnosticsReportController::select_related_violations_for_current_artifact()
{
    if (m_state.import_artifact_table == nullptr || m_state.violation_explorer == nullptr) {
        return false;
    }
    const int row = m_state.import_artifact_table->currentRow();
    if (row < 0) {
        m_window.publish_ui_notification("Related violation navigation unavailable: no import artifact selected", ActivityLogSeverity::Warning, 4000);
        return false;
    }
    const auto* item = m_state.import_artifact_table->item(row, 0);
    if (item == nullptr) {
        m_window.publish_ui_notification("Related violation navigation unavailable: import artifact details are incomplete", ActivityLogSeverity::Warning, 4000);
        return false;
    }

    const QString artifact_id = item->data(Qt::UserRole).toString();
    const QString artifact_path = item->text();
    QStringList related_ids;
    for (const auto& violation : m_state.latest_violations) {
        if (violation_matches_artifact(violation, artifact_id, artifact_path)) {
            related_ids.append(QString::fromStdString(violation.id));
        }
    }

    const int selected = m_state.violation_explorer->select_violation_ids(related_ids);
    if (selected <= 0) {
        m_window.publish_ui_notification("No related visible violations were found for the selected import artifact", ActivityLogSeverity::Warning, 5000);
        return false;
    }

    m_window.publish_ui_notification(QString("Opened %1 related violation(s) from selected import artifact").arg(selected), ActivityLogSeverity::Info, 4000);
    return true;
}

bool DiagnosticsReportController::request_trace_by_name(const QString& stable_name)
{
    if (m_state.canvas == nullptr) {
        return false;
    }

    const QString trimmed_name = stable_name.trimmed();
    if (m_state.trace_panel != nullptr) {
        m_state.trace_panel->set_request_text(trimmed_name);
    }

    if (trimmed_name.isEmpty()) {
        m_window.publish_trace_feedback("Enter a net, port, or device.pin to trace", ActivityLogSeverity::Warning, 4000);
        m_window.update_action_states();
        return false;
    }

    if (m_state.current_graph == nullptr) {
        m_window.publish_trace_feedback("Trace unavailable: no connectivity graph available", ActivityLogSeverity::Warning, 4000);
        m_window.update_action_states();
        return false;
    }

    const bool replaced_previous_trace = m_state.canvas->has_active_trace();
    const auto result = m_state.trace_adapter.trace_by_name(trimmed_name.toStdString(), m_state.canvas->scene());
    m_state.canvas->set_trace_result(result);

    QString message = QString::fromStdString(result.message);
    if (result.resolved && replaced_previous_trace) {
        message.append(" (replaced previous trace)");
    }
    m_window.publish_trace_feedback(message, result.resolved ? ActivityLogSeverity::Info : ActivityLogSeverity::Warning,
                           result.resolved ? 3000 : 4000);
    m_window.update_action_states();
    return result.resolved;
}

bool DiagnosticsReportController::request_trace_from_selection()
{
    if (m_state.selection_model == nullptr || m_state.canvas == nullptr) {
        return false;
    }
    const auto& ids = m_state.selection_model->selected_ids();
    if (ids.empty()) {
        m_window.publish_trace_feedback("No selection available for trace", ActivityLogSeverity::Warning, 4000);
        m_window.update_action_states();
        return false;
    }
    const auto* item = m_state.canvas->scene().find_item_by_id(ids.front());
    if (item == nullptr) {
        m_window.publish_trace_feedback("Selected item is no longer available for trace", ActivityLogSeverity::Warning, 4000);
        m_window.update_action_states();
        return false;
    }
    const auto net_it = item->source_metadata.find("net_name");
    if (net_it != item->source_metadata.end()) {
        return request_trace_by_name(QString::fromStdString(net_it->second));
    }
    const auto name_it = item->source_metadata.find("name");
    if (name_it != item->source_metadata.end()) {
        return request_trace_by_name(QString::fromStdString(name_it->second));
    }
    m_window.publish_trace_feedback("Selected item has no traceable stable name", ActivityLogSeverity::Warning, 4000);
    m_window.update_action_states();
    return false;
}

bool DiagnosticsReportController::request_trace_from_current_violation()
{
    if (m_state.violation_explorer == nullptr) {
        return false;
    }
    const auto* violation = m_state.violation_explorer->current_violation();
    if (violation == nullptr) {
        m_window.publish_trace_feedback("No current violation selected for trace", ActivityLogSeverity::Warning, 4000);
        m_window.update_action_states();
        return false;
    }
    if (violation->location.net_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.net_name));
    }
    if (violation->location.pin_name.has_value()) {
        return request_trace_by_name(QString::fromStdString(*violation->location.pin_name));
    }
    m_window.publish_trace_feedback("Violation does not contain a traceable graph reference", ActivityLogSeverity::Warning, 4000);
    m_window.update_action_states();
    return false;
}

void DiagnosticsReportController::clear_trace()
{
    if (m_state.canvas == nullptr) {
        return;
    }
    if (!m_state.canvas->has_active_trace()) {
        m_window.publish_trace_feedback("No active trace to clear", ActivityLogSeverity::Info, 3000);
        m_window.update_action_states();
        return;
    }
    m_state.canvas->clear_trace();
    m_window.publish_trace_feedback("Trace cleared", ActivityLogSeverity::Info, 3000);
    m_window.update_action_states();
}

void DiagnosticsReportController::focus_trace()
{
    if (m_state.canvas == nullptr) {
        return;
    }
    if (!m_state.canvas->has_active_trace()) {
        m_window.publish_trace_feedback("No active trace to focus", ActivityLogSeverity::Info, 3000);
        m_window.update_action_states();
        return;
    }
    m_state.canvas->focus_trace();
    m_window.publish_trace_feedback("Focused active trace", ActivityLogSeverity::Info, 3000);
    m_window.update_action_states();
}

void DiagnosticsReportController::update_trace_controls()
{
    if (m_state.trace_panel == nullptr) {
        return;
    }
    const bool has_graph = m_state.current_graph != nullptr;
    const bool has_active_trace = m_state.canvas != nullptr && m_state.canvas->has_active_trace();
    m_state.trace_panel->set_request_enabled(has_graph);
    m_state.trace_panel->set_clear_enabled(has_active_trace);
    m_state.trace_panel->set_focus_enabled(has_active_trace);

    const QString current_status = m_state.trace_panel->status_text();
    if (!has_graph) {
        if (current_status.trimmed().isEmpty() || current_status == state_text::trace_idle()) {
            m_state.trace_panel->set_status_text(state_text::trace_no_graph());
        }
    } else if (!has_active_trace
               && (current_status.trimmed().isEmpty() || current_status == state_text::trace_no_graph())) {
        m_state.trace_panel->set_status_text(state_text::trace_idle());
    }
}

} // namespace aegis::ui
