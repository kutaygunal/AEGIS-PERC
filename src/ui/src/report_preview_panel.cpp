#include "aegis/ui/report_preview_panel.hpp"

#include <QLabel>
#include <QPixmap>
#include <QTextEdit>
#include <QVBoxLayout>

namespace aegis::ui {
namespace {

int count_severity(const aegis::rules::ViolationCollection& violations,
                   aegis::rules::Severity severity)
{
    return static_cast<int>(violations.filter_by_severity(severity).size());
}

} // namespace

ReportPreviewPanel::ReportPreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_snapshot = new QLabel("Canvas snapshot placeholder", this);
    m_snapshot->setAlignment(Qt::AlignCenter);
    m_snapshot->setMinimumHeight(140);
    m_snapshot->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(m_snapshot);

    m_summary = new QTextEdit(this);
    m_summary->setReadOnly(true);
    layout->addWidget(m_summary, 1);

    rebuild_summary();
}

void ReportPreviewPanel::set_scene(const UiScene& scene)
{
    m_scene = scene;
    rebuild_summary();
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

void ReportPreviewPanel::set_snapshot(const QPixmap& snapshot)
{
    if (snapshot.isNull()) {
        m_snapshot->setPixmap(QPixmap{});
        m_snapshot->setText("Canvas snapshot placeholder");
        return;
    }

    m_snapshot->setText(QString{});
    m_snapshot->setPixmap(snapshot.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString ReportPreviewPanel::summary_text() const
{
    return m_summary->toPlainText();
}

QString ReportPreviewPanel::snapshot_status_text() const
{
    return m_snapshot->text().isEmpty() ? QString("Canvas snapshot ready") : m_snapshot->text();
}

void ReportPreviewPanel::rebuild_summary()
{
    using aegis::rules::Severity;

    QStringList lines;
    lines.append(QString("Design: %1").arg(QString::fromStdString(m_scene.design_name.empty() ? std::string{"(unnamed)"} : m_scene.design_name)));
    lines.append(QString("Layers: %1").arg(m_scene.layers.size()));
    lines.append(QString("Violations: %1").arg(m_violations.size()));
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
}

} // namespace aegis::ui
