#include "aegis/ui/activity_log_panel.hpp"

#include <QDateTime>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

namespace aegis::ui {
namespace {

QColor severity_color(ActivityLogSeverity severity)
{
    switch (severity) {
        case ActivityLogSeverity::Info: return QColor("#243226");
        case ActivityLogSeverity::Warning: return QColor("#3A3522");
        case ActivityLogSeverity::Error: return QColor("#3A2424");
    }
    return QColor("#2F2F2F");
}

QColor severity_text_color(ActivityLogSeverity severity)
{
    switch (severity) {
        case ActivityLogSeverity::Info:
        case ActivityLogSeverity::Warning:
        case ActivityLogSeverity::Error:
            return QColor("#F2F2F2");
    }
    return QColor("#F2F2F2");
}

QString severity_label(ActivityLogSeverity severity)
{
    switch (severity) {
        case ActivityLogSeverity::Info: return "INFO";
        case ActivityLogSeverity::Warning: return "WARN";
        case ActivityLogSeverity::Error: return "ERROR";
    }
    return "INFO";
}

} // namespace

ActivityLogPanel::ActivityLogPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_entries = new QListWidget(this);
    m_entries->setObjectName("ActivityLogEntries");
    m_entries->setAlternatingRowColors(false);
    m_entries->setSelectionMode(QAbstractItemView::NoSelection);
    m_entries->setFocusPolicy(Qt::NoFocus);
    m_entries->setStyleSheet(
        "QListWidget#ActivityLogEntries {"
        "  background-color: #1F1F1F;"
        "  color: #F2F2F2;"
        "  border: 1px solid #3A3A3A;"
        "}"
        "QListWidget#ActivityLogEntries::item {"
        "  color: #F2F2F2;"
        "  padding: 2px 4px;"
        "  margin: 1px 0px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget#ActivityLogEntries::item:selected {"
        "  color: #F2F2F2;"
        "  background: #4A628A;"
        "}"
    );
    layout->addWidget(m_entries);
}

void ActivityLogPanel::append_entry(const QString& message, ActivityLogSeverity severity)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    const QString text = QString("[%1] [%2] %3").arg(timestamp, severity_label(severity), message);
    m_entries->addItem(make_item(text, severity));
    trim_to_limit();
    m_entries->scrollToBottom();
}

void ActivityLogPanel::clear_entries()
{
    m_entries->clear();
}

int ActivityLogPanel::entry_count() const noexcept
{
    return m_entries != nullptr ? m_entries->count() : 0;
}

QString ActivityLogPanel::entry_text(int index) const
{
    if (m_entries == nullptr || index < 0 || index >= m_entries->count()) {
        return {};
    }
    const auto* item = m_entries->item(index);
    return item != nullptr ? item->text() : QString{};
}

QStringList ActivityLogPanel::all_entry_texts() const
{
    QStringList texts;
    if (m_entries == nullptr) {
        return texts;
    }
    for (int i = 0; i < m_entries->count(); ++i) {
        if (const auto* item = m_entries->item(i); item != nullptr) {
            texts.push_back(item->text());
        }
    }
    return texts;
}

int ActivityLogPanel::max_entries() const noexcept
{
    return m_max_entries;
}

void ActivityLogPanel::trim_to_limit()
{
    while (m_entries != nullptr && m_entries->count() > m_max_entries) {
        delete m_entries->takeItem(0);
    }
}

QListWidgetItem* ActivityLogPanel::make_item(const QString& text, ActivityLogSeverity severity)
{
    auto* item = new QListWidgetItem(text);
    item->setBackground(severity_color(severity));
    item->setForeground(severity_text_color(severity));
    return item;
}

} // namespace aegis::ui
