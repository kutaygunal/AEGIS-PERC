#include "aegis/ui/trace_panel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aegis::ui {

TracePanel::TracePanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* row = new QHBoxLayout();
    m_request = new QLineEdit(this);
    m_request->setPlaceholderText("net / port / device.pin");
    auto* trace = new QPushButton("Trace", this);
    auto* clear = new QPushButton("Clear", this);
    auto* focus = new QPushButton("Focus", this);
    row->addWidget(m_request, 1);
    row->addWidget(trace);
    row->addWidget(clear);
    row->addWidget(focus);
    root->addLayout(row);

    m_status = new QLabel("No active trace", this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(trace, &QPushButton::clicked, this, [this]() { emit trace_requested(m_request->text()); });
    connect(m_request, &QLineEdit::returnPressed, this, [this]() { emit trace_requested(m_request->text()); });
    connect(clear, &QPushButton::clicked, this, &TracePanel::clear_trace_requested);
    connect(focus, &QPushButton::clicked, this, &TracePanel::focus_trace_requested);
}

QString TracePanel::request_text() const
{
    return m_request->text();
}

void TracePanel::set_request_text(const QString& text)
{
    m_request->setText(text);
}

void TracePanel::set_status_text(const QString& text)
{
    m_status->setText(text);
}

QString TracePanel::status_text() const
{
    return m_status->text();
}

} // namespace aegis::ui
