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
        case ActivityLogSeverity::Info: return QColor("#D9EAD3");
        case ActivityLogSeverity::Warning: return QColor("#FFF2CC");
        case ActivityLogSeverity::Error: return QColor("#F4CCCC");
    }
    return QColor("#FFFFFF");
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
    m_entries->setAlternatingRowColors(true);
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
    return item;
}

} // namespace aegis::ui
