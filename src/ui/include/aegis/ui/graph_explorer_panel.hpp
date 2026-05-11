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
    void graph_node_double_clicked(QString stable_name);
    void cross_probe_requested(QString stable_name, QString origin_panel);

private:
    const aegis::graph::ConnectivityGraph* m_graph = nullptr;
    QLineEdit* m_search = nullptr;
    QSlider* m_lod = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;
    QString m_selected_stable_name;

    void rebuild_tree();
    void update_status();
    [[nodiscard]] QTreeWidgetItem* find_item_by_stable_name(const QString& stable_name) const;
    void add_node_item(QTreeWidgetItem* parent,
                       const QString& label,
                       const QString& stable_name,
                       const QString& kind);
};

} // namespace aegis::ui
