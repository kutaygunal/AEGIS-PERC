#include "aegis/ui/graph_explorer_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace aegis::ui {
namespace {

QString stable_name_for_node(const aegis::graph::ConnectivityGraph& graph,
                             aegis::graph::NodeId node_id)
{
    const auto type = graph.node_type(node_id);
    const auto& data = graph.node_data(node_id);
    if (type == aegis::graph::NodeType::Net) {
        return QString::fromStdString(std::get<aegis::graph::NetNode>(data).name);
    }
    if (type == aegis::graph::NodeType::Device) {
        return QString::fromStdString(std::get<aegis::graph::DeviceNode>(data).name);
    }

    const auto& pin = std::get<aegis::graph::PinNode>(data);
    for (const auto edge_id : graph.incoming_edges(node_id)) {
        if (graph.edge_data(edge_id).type == aegis::graph::EdgeType::DeviceToPin) {
            const auto& device = std::get<aegis::graph::DeviceNode>(graph.node_data(graph.edge_from(edge_id)));
            return QString::fromStdString(device.name + "." + graph.edge_data(edge_id).terminal_name);
        }
    }
    return QString::fromStdString(pin.name);
}

QString node_kind(const aegis::graph::ConnectivityGraph& graph,
                  aegis::graph::NodeId node_id)
{
    switch (graph.node_type(node_id)) {
        case aegis::graph::NodeType::Device: return "Device";
        case aegis::graph::NodeType::Net: return "Net";
        case aegis::graph::NodeType::Pin: return "Pin";
    }
    return "Node";
}

} // namespace

GraphExplorerPanel::GraphExplorerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* top = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search nodes by name");
    auto* find_button = new QPushButton("Find", this);
    top->addWidget(m_search, 1);
    top->addWidget(find_button);
    root->addLayout(top);

    auto* lod_row = new QHBoxLayout();
    lod_row->addWidget(new QLabel("LOD:", this));
    m_lod = new QSlider(Qt::Horizontal, this);
    m_lod->setRange(1, 500);
    m_lod->setValue(100);
    lod_row->addWidget(m_lod, 1);
    root->addLayout(lod_row);

    m_status = new QLabel(state_text::graph_no_graph(), this);
    root->addWidget(m_status);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Node", "Kind"});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    root->addWidget(m_tree, 1);

    connect(find_button, &QPushButton::clicked, this, [this]() {
        const bool found = search_and_select(m_search->text());
        Q_UNUSED(found);
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        const bool found = search_and_select(m_search->text());
        Q_UNUSED(found);
    });
    connect(m_lod, &QSlider::valueChanged, this, [this](int) { rebuild_tree(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                m_selected_stable_name = current != nullptr ? current->data(0, Qt::UserRole).toString() : QString{};
                if (current == nullptr) {
                    return;
                }
                const QString stable_name = current->data(0, Qt::UserRole).toString();
                if (!stable_name.isEmpty()) {
                    emit graph_node_selected(stable_name);
                }
            });

    update_status();
}

void GraphExplorerPanel::set_graph(const aegis::graph::ConnectivityGraph* graph)
{
    m_graph = graph;
    rebuild_tree();
}

const aegis::graph::ConnectivityGraph* GraphExplorerPanel::graph() const noexcept
{
    return m_graph;
}

int GraphExplorerPanel::visible_item_count() const noexcept
{
    return m_tree->topLevelItemCount();
}

QString GraphExplorerPanel::current_node_name() const
{
    const auto* current = m_tree->currentItem();
    return current != nullptr ? current->data(0, Qt::UserRole).toString() : QString{};
}

QString GraphExplorerPanel::status_text() const
{
    return m_status->text();
}

bool GraphExplorerPanel::search_and_select(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_status->setText(state_text::graph_enter_search());
        return false;
    }
    if (m_graph == nullptr) {
        m_status->setText(state_text::graph_no_graph());
        return false;
    }
    if (m_graph->node_count() == 0) {
        m_status->setText(state_text::graph_empty());
        return false;
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->text(0).compare(trimmed, Qt::CaseInsensitive) == 0) {
            m_tree->setCurrentItem(item);
            update_status();
            return true;
        }
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->text(0).contains(trimmed, Qt::CaseInsensitive)) {
            m_tree->setCurrentItem(item);
            update_status();
            return true;
        }
    }

    const int total = static_cast<int>(m_graph->node_count());
    const int visible = m_tree->topLevelItemCount();
    if (visible < total) {
        m_status->setText(QString("No visible graph node found for '%1' within the current LOD range (%2 / %3 shown)")
                              .arg(trimmed)
                              .arg(visible)
                              .arg(total));
    } else {
        m_status->setText(QString("No graph node found for '%1'").arg(trimmed));
    }
    return false;
}

void GraphExplorerPanel::set_lod_limit(int value)
{
    m_lod->setValue(value);
}

int GraphExplorerPanel::lod_limit() const noexcept
{
    return m_lod->value();
}

void GraphExplorerPanel::rebuild_tree()
{
    const QString desired_selection = m_selected_stable_name;
    m_tree->clear();
    m_selected_stable_name.clear();

    if (m_graph == nullptr) {
        update_status();
        return;
    }

    const int limit = m_lod->value();
    const int total = static_cast<int>(m_graph->node_count());
    const int count = std::min(limit, total);

    for (int i = 0; i < count; ++i) {
        const auto node_id = static_cast<aegis::graph::NodeId>(i);
        const QString stable_name = stable_name_for_node(*m_graph, node_id);
        const QString kind = node_kind(*m_graph, node_id);
        add_node_item(nullptr, stable_name, stable_name, kind);
    }

    if (!desired_selection.isEmpty()) {
        if (auto* item = find_item_by_stable_name(desired_selection)) {
            m_tree->setCurrentItem(item);
            m_selected_stable_name = desired_selection;
        }
    }

    update_status();
}

void GraphExplorerPanel::update_status()
{
    if (m_graph == nullptr) {
        m_status->setText(state_text::graph_no_graph());
        return;
    }

    const int total = static_cast<int>(m_graph->node_count());
    const int visible = m_tree->topLevelItemCount();
    if (total == 0) {
        m_status->setText(state_text::graph_empty());
        return;
    }

    QString status = QString("%1 / %2 graph nodes visible").arg(visible).arg(total);
    if (visible < total) {
        status.append(QString(" (LOD limit %1)").arg(m_lod->value()));
    }
    if (!m_selected_stable_name.isEmpty()) {
        status.append(QString(" — selected: %1").arg(m_selected_stable_name));
    }
    m_status->setText(status);
}

QTreeWidgetItem* GraphExplorerPanel::find_item_by_stable_name(const QString& stable_name) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item != nullptr && item->data(0, Qt::UserRole).toString() == stable_name) {
            return item;
        }
    }
    return nullptr;
}

void GraphExplorerPanel::add_node_item(QTreeWidgetItem* parent,
                                       const QString& label,
                                       const QString& stable_name,
                                       const QString& kind)
{
    auto* item = parent != nullptr
        ? new QTreeWidgetItem(parent, {label, kind})
        : new QTreeWidgetItem(m_tree, {label, kind});
    item->setData(0, Qt::UserRole, stable_name);
}

} // namespace aegis::ui
