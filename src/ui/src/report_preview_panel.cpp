#include "aegis/ui/report_preview_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace aegis::ui {
namespace {

int count_severity(const aegis::rules::ViolationCollection& violations,
                   aegis::rules::Severity severity)
{
    return static_cast<int>(violations.filter_by_severity(severity).size());
}

QString design_summary_name(const UiScene& scene)
{
    return QString::fromStdString(scene.design_name.empty() ? std::string{"(no design loaded)"} : scene.design_name);
}

} // namespace

ReportPreviewPanel::ReportPreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* actions = new QHBoxLayout();
    m_refresh = new QPushButton("Refresh Snapshot", this);
    m_copy_summary = new QPushButton("Copy Summary", this);
    m_copy_snapshot = new QPushButton("Copy Snapshot", this);
    actions->addWidget(m_refresh);
    actions->addWidget(m_copy_summary);
    actions->addWidget(m_copy_snapshot);
    actions->addStretch(1);
    layout->addLayout(actions);

    m_snapshot = new QLabel(this);
    m_snapshot->setAlignment(Qt::AlignCenter);
    m_snapshot->setMinimumHeight(140);
    m_snapshot->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_snapshot);

    m_snapshot_status = new QLabel(this);
    m_snapshot_status->setWordWrap(true);
    layout->addWidget(m_snapshot_status);

    m_summary = new QTextEdit(this);
    m_summary->setReadOnly(true);
    layout->addWidget(m_summary, 1);

    connect(m_refresh, &QPushButton::clicked, this, &ReportPreviewPanel::trigger_refresh);
    connect(m_copy_summary, &QPushButton::clicked, this, &ReportPreviewPanel::trigger_copy_summary);
    connect(m_copy_snapshot, &QPushButton::clicked, this, &ReportPreviewPanel::trigger_copy_snapshot);

    set_snapshot(QPixmap{});
    rebuild_summary();
    update_action_state();
}

void ReportPreviewPanel::set_scene(const UiScene& scene)
{
    m_scene = scene;
    rebuild_summary();
    update_action_state();
}

void ReportPreviewPanel::set_violations(aegis::rules::ViolationCollection violations)
{
    m_violations = std::move(violations);
    rebuild_summary();
}

void ReportPreviewPanel::set_current_violation(const aegis::rules::Violation* violation)
{
    m_current_violation = violation != nullptr ? std::optional<aegis::rules::Violation>(*violation) : std::nullopt;
    rebuild_summary();
}

void ReportPreviewPanel::set_selected_item_count(int count)
{
    m_selected_item_count = std::max(0, count);
    rebuild_summary();
}

void ReportPreviewPanel::set_snapshot(const QPixmap& snapshot)
{
    m_snapshot_pixmap = snapshot;
    if (snapshot.isNull()) {
        m_snapshot->setPixmap(QPixmap{});
        m_snapshot->setText(m_scene.design_name.empty()
            ? state_text::report_snapshot_empty()
            : state_text::report_snapshot_refresh_needed());
        m_snapshot_status->setText(m_snapshot->text());
        update_action_state();
        return;
    }

    m_snapshot->setText(QString{});
    m_snapshot->setPixmap(snapshot.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_snapshot_status->setText(QString("Canvas snapshot ready for %1").arg(design_summary_name(m_scene)));
    update_action_state();
}

QString ReportPreviewPanel::summary_text() const
{
    return m_summary->toPlainText();
}

QString ReportPreviewPanel::snapshot_status_text() const
{
    if (!m_snapshot_pixmap.isNull()) {
        return "Canvas snapshot ready";
    }
    return m_snapshot_status != nullptr ? m_snapshot_status->text() : QString{};
}

QString ReportPreviewPanel::last_action_status_text() const
{
    return m_last_action_status;
}

bool ReportPreviewPanel::refresh_enabled() const
{
    return m_refresh != nullptr && m_refresh->isEnabled();
}

bool ReportPreviewPanel::copy_summary_enabled() const
{
    return m_copy_summary != nullptr && m_copy_summary->isEnabled();
}

bool ReportPreviewPanel::copy_snapshot_enabled() const
{
    return m_copy_snapshot != nullptr && m_copy_snapshot->isEnabled();
}

void ReportPreviewPanel::trigger_refresh()
{
    if (!refresh_enabled()) {
        set_action_status_text("Load a design before refreshing the report preview");
        return;
    }
    set_action_status_text(QString("Requested snapshot refresh for %1").arg(design_summary_name(m_scene)));
    emit refresh_requested();
}

void ReportPreviewPanel::trigger_copy_summary()
{
    if (!copy_summary_enabled()) {
        set_action_status_text("Report summary is unavailable to copy");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setText(summary_text());
    }
    set_action_status_text("Copied report summary");
}

void ReportPreviewPanel::trigger_copy_snapshot()
{
    if (!copy_snapshot_enabled()) {
        set_action_status_text("Snapshot is unavailable to copy");
        return;
    }
    if (auto* clipboard = QApplication::clipboard()) {
        clipboard->setPixmap(m_snapshot_pixmap);
    }
    set_action_status_text("Copied report snapshot");
}

void ReportPreviewPanel::rebuild_summary()
{
    using aegis::rules::Severity;

    QStringList lines;
    if (m_scene.design_name.empty()) {
        lines.append(state_text::report_summary_empty());
    }
    lines.append(QString("Design: %1").arg(design_summary_name(m_scene)));
    lines.append(QString("Layers: %1").arg(m_scene.layers.size()));
    lines.append(QString("Violations: %1").arg(m_violations.size()));
    lines.append(QString("Selection: %1 item(s)").arg(m_selected_item_count));
    lines.append(QString("  info=%1 warning=%2 error=%3 fatal=%4")
                     .arg(count_severity(m_violations, Severity::Info))
                     .arg(count_severity(m_violations, Severity::Warning))
                     .arg(count_severity(m_violations, Severity::Error))
                     .arg(count_severity(m_violations, Severity::Fatal)));

    if (m_current_violation.has_value()) {
        lines.append(QString{});
        lines.append(QString("Selected violation: %1").arg(QString::fromStdString(m_current_violation->id)));
        lines.append(QString("  rule=%1 severity=%2")
                         .arg(QString::fromStdString(m_current_violation->rule_id))
                         .arg(QString::fromStdString(aegis::rules::severity_to_string(m_current_violation->severity))));
        lines.append(QString("  message=%1").arg(QString::fromStdString(m_current_violation->message)));
    } else {
        lines.append(QString{});
        lines.append("Selected violation: none");
    }

    lines.append(QString{});
    lines.append("Export is reserved for P5.");
    m_summary->setPlainText(lines.join('\n'));
    update_action_state();
}

void ReportPreviewPanel::update_action_state()
{
    const bool has_scene = !m_scene.design_name.empty();
    const bool has_summary = m_summary != nullptr && !m_summary->toPlainText().trimmed().isEmpty();
    const bool has_snapshot = !m_snapshot_pixmap.isNull();

    if (m_refresh != nullptr) {
        m_refresh->setEnabled(has_scene);
    }
    if (m_copy_summary != nullptr) {
        m_copy_summary->setEnabled(has_summary);
    }
    if (m_copy_snapshot != nullptr) {
        m_copy_snapshot->setEnabled(has_snapshot);
    }
}

void ReportPreviewPanel::set_action_status_text(const QString& text)
{
    m_last_action_status = text;
}

} // namespace aegis::ui
