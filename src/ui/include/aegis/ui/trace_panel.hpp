#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace aegis::ui {

class TracePanel : public QWidget {
    Q_OBJECT
public:
    explicit TracePanel(QWidget* parent = nullptr);

    [[nodiscard]] QString request_text() const;
    void set_request_text(const QString& text);
    void set_request_enabled(bool enabled);
    [[nodiscard]] bool request_enabled() const;
    void set_clear_enabled(bool enabled);
    [[nodiscard]] bool clear_enabled() const;
    void set_focus_enabled(bool enabled);
    [[nodiscard]] bool focus_enabled() const;
    void set_status_text(const QString& text);
    [[nodiscard]] QString status_text() const;

signals:
    void trace_requested(QString stable_name);
    void clear_trace_requested();
    void focus_trace_requested();

private:
    QLineEdit* m_request = nullptr;
    QPushButton* m_trace = nullptr;
    QPushButton* m_clear = nullptr;
    QPushButton* m_focus = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace aegis::ui
