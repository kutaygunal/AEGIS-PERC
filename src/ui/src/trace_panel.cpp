#include "aegis/ui/trace_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

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
    m_trace = new QPushButton("Trace", this);
    m_clear = new QPushButton("Clear", this);
    m_focus = new QPushButton("Focus", this);
    row->addWidget(m_request, 1);
    row->addWidget(m_trace);
    row->addWidget(m_clear);
    row->addWidget(m_focus);
    root->addLayout(row);

    m_status = new QLabel(state_text::trace_no_graph(), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_trace, &QPushButton::clicked, this, [this]() { emit trace_requested(m_request->text()); });
    connect(m_request, &QLineEdit::returnPressed, this, [this]() { emit trace_requested(m_request->text()); });
    connect(m_clear, &QPushButton::clicked, this, &TracePanel::clear_trace_requested);
    connect(m_focus, &QPushButton::clicked, this, &TracePanel::focus_trace_requested);

    set_request_enabled(false);
    set_clear_enabled(false);
    set_focus_enabled(false);
}

QString TracePanel::request_text() const
{
    return m_request->text();
}

void TracePanel::set_request_text(const QString& text)
{
    m_request->setText(text);
}

void TracePanel::set_request_enabled(bool enabled)
{
    if (m_request != nullptr) {
        m_request->setEnabled(enabled);
    }
    if (m_trace != nullptr) {
        m_trace->setEnabled(enabled);
    }
}

bool TracePanel::request_enabled() const
{
    return m_request != nullptr && m_request->isEnabled() && m_trace != nullptr && m_trace->isEnabled();
}

void TracePanel::set_clear_enabled(bool enabled)
{
    if (m_clear != nullptr) {
        m_clear->setEnabled(enabled);
    }
}

bool TracePanel::clear_enabled() const
{
    return m_clear != nullptr && m_clear->isEnabled();
}

void TracePanel::set_focus_enabled(bool enabled)
{
    if (m_focus != nullptr) {
        m_focus->setEnabled(enabled);
    }
}

bool TracePanel::focus_enabled() const
{
    return m_focus != nullptr && m_focus->isEnabled();
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
