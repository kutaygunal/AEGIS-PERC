#pragma once

#include "aegis/storage/imported_design_session.hpp"

#include <QAbstractItemModel>

#include <map>
#include <unordered_map>
#include <vector>

namespace aegis::ui {

class HierarchyBrowserModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit HierarchyBrowserModel(QObject* parent = nullptr);
    ~HierarchyBrowserModel();

    void set_session(const aegis::storage::ImportedDesignSession* session);
    [[nodiscard]] const aegis::storage::ImportedDesignSession* session() const noexcept;

    // QAbstractItemModel interface
    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Stable-id lookup (used by panel for selection restoration)
    [[nodiscard]] QModelIndex find_by_stable_id(const QString& stable_id) const;

private:
    struct StableLocation {
        int group_row = -1;
        int object_row = -1;
    };

    struct GroupEntry {
        int kind_order = 0;
        aegis::storage::ImportedDesignObjectKind kind;
        QString label;
        std::vector<std::size_t> object_indices; // sorted indices into session->objects()
    };

    const aegis::storage::ImportedDesignSession* m_session = nullptr;
    std::vector<GroupEntry> m_groups; // sorted by kind_order
    std::unordered_map<std::string, StableLocation> m_stable_id_index;

    void rebuild_cache();

    [[nodiscard]] bool is_group(const QModelIndex& index) const;
    [[nodiscard]] const GroupEntry* group_at(int row) const;
    [[nodiscard]] const aegis::storage::ImportedDesignObject* object_at(const QModelIndex& index) const;
};

} // namespace aegis::ui
