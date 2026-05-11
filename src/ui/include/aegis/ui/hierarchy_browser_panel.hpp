#pragma once

#include "aegis/storage/imported_design_session.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace aegis::ui {

class HierarchyBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit HierarchyBrowserPanel(QWidget* parent = nullptr);

    void set_session(const aegis::storage::ImportedDesignSession* session);
    [[nodiscard]] const aegis::storage::ImportedDesignSession* session() const noexcept;
    [[nodiscard]] int visible_item_count() const noexcept;
    [[nodiscard]] QString current_stable_id() const;
    [[nodiscard]] QString status_text() const;
    [[nodiscard]] bool search_and_select(const QString& text);
    void select_by_stable_id(const QString& stable_id);
    void clear_selection();

signals:
    void object_selected(QString stable_id);
    void object_double_clicked(QString stable_id);
    void cross_probe_requested(QString stable_id, QString origin_panel);

private:
    const aegis::storage::ImportedDesignSession* m_session = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;
    QString m_selected_stable_id;

    void rebuild_tree();
    void apply_search_filter();
    void update_status();
    [[nodiscard]] QTreeWidgetItem* find_item_by_stable_id(const QString& stable_id) const;
    void add_object_item(QTreeWidgetItem* parent,
                         const QString& label,
                         const QString& stable_id,
                         const QString& kind);
};

} // namespace aegis::ui
