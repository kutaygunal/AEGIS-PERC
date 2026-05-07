#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

namespace aegis::ui {

class TracePanel : public QWidget {
    Q_OBJECT
public:
    explicit TracePanel(QWidget* parent = nullptr);

    [[nodiscard]] QString request_text() const;
    void set_request_text(const QString& text);
    void set_status_text(const QString& text);
    [[nodiscard]] QString status_text() const;

signals:
    void trace_requested(QString stable_name);
    void clear_trace_requested();
    void focus_trace_requested();

private:
    QLineEdit* m_request = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace aegis::ui
