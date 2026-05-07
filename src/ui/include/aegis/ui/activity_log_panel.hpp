#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace aegis::ui {

enum class ActivityLogSeverity {
    Info,
    Warning,
    Error,
};

class ActivityLogPanel : public QWidget {
    Q_OBJECT
public:
    explicit ActivityLogPanel(QWidget* parent = nullptr);

    void append_entry(const QString& message, ActivityLogSeverity severity = ActivityLogSeverity::Info);
    void clear_entries();

    int entry_count() const noexcept;
    QString entry_text(int index) const;
    QStringList all_entry_texts() const;
    int max_entries() const noexcept;

private:
    void trim_to_limit();
    QListWidgetItem* make_item(const QString& text, ActivityLogSeverity severity);

    QListWidget* m_entries = nullptr;
    int m_max_entries = 200;
};

} // namespace aegis::ui
