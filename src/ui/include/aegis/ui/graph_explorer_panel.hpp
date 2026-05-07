#pragma once

#include "aegis/graph/connectivity_graph.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QSlider;
class QTreeWidget;
class QTreeWidgetItem;

namespace aegis::ui {

class GraphExplorerPanel : public QWidget {
    Q_OBJECT
public:
    explicit GraphExplorerPanel(QWidget* parent = nullptr);

    void set_graph(const aegis::graph::ConnectivityGraph* graph);
    [[nodiscard]] const aegis::graph::ConnectivityGraph* graph() const noexcept;
    [[nodiscard]] int visible_item_count() const noexcept;
    [[nodiscard]] QString current_node_name() const;
    [[nodiscard]] QString status_text() const;
    [[nodiscard]] bool search_and_select(const QString& text);
    void set_lod_limit(int value);
    [[nodiscard]] int lod_limit() const noexcept;

signals:
    void graph_node_selected(QString stable_name);

private:
    const aegis::graph::ConnectivityGraph* m_graph = nullptr;
    QLineEdit* m_search = nullptr;
    QSlider* m_lod = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;

    void rebuild_tree();
    void update_status();
    void add_node_item(QTreeWidgetItem* parent,
                       const QString& label,
                       const QString& stable_name,
                       const QString& kind);
};

} // namespace aegis::ui
