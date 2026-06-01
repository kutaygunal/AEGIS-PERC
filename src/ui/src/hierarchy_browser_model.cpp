#include "aegis/ui/hierarchy_browser_model.hpp"

#include <QtGlobal>

#include <algorithm>
#include <string>

namespace aegis::ui {
namespace {

QString kind_label(aegis::storage::ImportedDesignObjectKind kind)
{
    using aegis::storage::ImportedDesignObjectKind;
    switch (kind) {
    case ImportedDesignObjectKind::Instance: return "Instances";
    case ImportedDesignObjectKind::Net: return "Nets";
    case ImportedDesignObjectKind::Port: return "Ports";
    case ImportedDesignObjectKind::Layer: return "Layers";
    case ImportedDesignObjectKind::TechnologyMacro: return "Technology Macros";
    case ImportedDesignObjectKind::Device: return "Devices";
    }
    return "Unknown";
}

int kind_sort_order(aegis::storage::ImportedDesignObjectKind kind)
{
    using aegis::storage::ImportedDesignObjectKind;
    switch (kind) {
    case ImportedDesignObjectKind::Layer: return 0;
    case ImportedDesignObjectKind::TechnologyMacro: return 1;
    case ImportedDesignObjectKind::Instance: return 2;
    case ImportedDesignObjectKind::Device: return 3;
    case ImportedDesignObjectKind::Net: return 4;
    case ImportedDesignObjectKind::Port: return 5;
    }
    return 99;
}

} // namespace

HierarchyBrowserModel::HierarchyBrowserModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

HierarchyBrowserModel::~HierarchyBrowserModel()
{
}

void HierarchyBrowserModel::set_session(const aegis::storage::ImportedDesignSession* session)
{
    beginResetModel();
    m_session = session;
    rebuild_cache();
    endResetModel();
}

const aegis::storage::ImportedDesignSession* HierarchyBrowserModel::session() const noexcept
{
    return m_session;
}

void HierarchyBrowserModel::rebuild_cache()
{
    m_groups.clear();
    m_stable_id_index.clear();
    if (m_session == nullptr) {
        return;
    }

    // Build groups by kind
    using Kind = aegis::storage::ImportedDesignObjectKind;
    std::map<int, Kind> order_to_kind;
    const auto& objects = m_session->objects();

    for (std::size_t i = 0; i < objects.size(); ++i) {
        const int order = kind_sort_order(objects[i].kind);
        if (order_to_kind.find(order) == order_to_kind.end()) {
            order_to_kind[order] = objects[i].kind;
        }
    }

    // Initialize groups
    for (const auto& [order, kind] : order_to_kind) {
        GroupEntry entry;
        entry.kind_order = order;
        entry.kind = kind;
        entry.label = kind_label(kind);
        m_groups.push_back(std::move(entry));
    }

    // Sort groups by order (they already are from std::map, but be explicit)
    std::sort(m_groups.begin(), m_groups.end(),
              [](const GroupEntry& a, const GroupEntry& b) { return a.kind_order < b.kind_order; });

    // Build object index mapping per group and sort within each group by display name
    std::map<Kind, std::vector<std::size_t>> kind_indices;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        kind_indices[objects[i].kind].push_back(i);
    }

    for (auto& group : m_groups) {
        auto it = kind_indices.find(group.kind);
        if (it != kind_indices.end()) {
            group.object_indices = std::move(it->second);
            std::sort(group.object_indices.begin(), group.object_indices.end(),
                      [&objects](std::size_t a, std::size_t b) {
                          const QString name_a = QString::fromStdString(
                              objects[a].display_name.empty() ? objects[a].name : objects[a].display_name);
                          const QString name_b = QString::fromStdString(
                              objects[b].display_name.empty() ? objects[b].name : objects[b].display_name);
                          return name_a.compare(name_b, Qt::CaseInsensitive) < 0;
                      });
        }
    }

    // Build stable-id lookup for fast selection restoration.
    for (int group_row = 0; group_row < static_cast<int>(m_groups.size()); ++group_row) {
        const auto& group = m_groups[group_row];
        for (int row = 0; row < static_cast<int>(group.object_indices.size()); ++row) {
            const auto& obj = objects[group.object_indices[row]];
            m_stable_id_index[obj.stable_id] = StableLocation{group_row, row};
        }
    }
}

QModelIndex HierarchyBrowserModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }

    if (!parent.isValid()) {
        // Top-level group item
        if (row < 0 || row >= static_cast<int>(m_groups.size())) {
            return {};
        }
        return createIndex(row, column, quintptr(0)); // internalId = 0 means group
    }

    // Child of a group = object item
    if (!is_group(parent)) {
        return {};
    }
    const int group_row = parent.row();
    if (group_row < 0 || group_row >= static_cast<int>(m_groups.size())) {
        return {};
    }
    const auto& group = m_groups[group_row];
    if (row < 0 || row >= static_cast<int>(group.object_indices.size())) {
        return {};
    }
    // internalId = kind_order + 1 means object under this kind group
    return createIndex(row, column, quintptr(group.kind_order + 1));
}

QModelIndex HierarchyBrowserModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    if (is_group(child)) {
        return {}; // groups have no parent
    }
    // Object items have the group as parent
    const int kind_order = static_cast<int>(child.internalId()) - 1;
    // Find the group with this kind_order
    for (std::size_t i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].kind_order == kind_order) {
            return createIndex(static_cast<int>(i), 0, quintptr(0));
        }
    }
    return {};
}

int HierarchyBrowserModel::rowCount(const QModelIndex& parent) const
{
    if (m_session == nullptr) {
        return 0;
    }
    if (!parent.isValid()) {
        return static_cast<int>(m_groups.size());
    }
    if (is_group(parent)) {
        const int group_row = parent.row();
        if (group_row >= 0 && group_row < static_cast<int>(m_groups.size())) {
            return static_cast<int>(m_groups[group_row].object_indices.size());
        }
    }
    return 0;
}

int HierarchyBrowserModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 2; // Object, Kind
}

QVariant HierarchyBrowserModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || m_session == nullptr) {
        return {};
    }

    if (is_group(index)) {
        if (role == Qt::DisplayRole && index.column() == 0) {
            const auto* group = group_at(index.row());
            return group ? QVariant(group->label) : QVariant{};
        }
        if (role == Qt::UserRole) {
            return QVariant{}; // groups have no stable_id
        }
        return {};
    }

    const auto* object = object_at(index);
    if (object == nullptr) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) {
            return QString::fromStdString(
                object->display_name.empty() ? object->name : object->display_name);
        }
        if (index.column() == 1) {
            return QString::fromStdString(aegis::storage::to_string(object->kind));
        }
    }
    if (role == Qt::UserRole && index.column() == 0) {
        return QString::fromStdString(object->stable_id);
    }
    return {};
}

Qt::ItemFlags HierarchyBrowserModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    if (is_group(index)) {
        return Qt::ItemIsEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant HierarchyBrowserModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) return "Object";
        if (section == 1) return "Kind";
    }
    return {};
}

QModelIndex HierarchyBrowserModel::find_by_stable_id(const QString& stable_id) const
{
    if (m_session == nullptr || stable_id.isEmpty()) {
        return {};
    }
    const auto it = m_stable_id_index.find(stable_id.toStdString());
    if (it == m_stable_id_index.end()) {
        return {};
    }
    const auto& loc = it->second;
    if (loc.group_row < 0 || loc.group_row >= static_cast<int>(m_groups.size())) {
        return {};
    }
    return createIndex(loc.object_row, 0, quintptr(m_groups[loc.group_row].kind_order + 1));
}

bool HierarchyBrowserModel::is_group(const QModelIndex& index) const
{
    return index.isValid() && index.internalId() == 0;
}

const HierarchyBrowserModel::GroupEntry* HierarchyBrowserModel::group_at(int row) const
{
    if (row >= 0 && row < static_cast<int>(m_groups.size())) {
        return &m_groups[row];
    }
    return nullptr;
}

const aegis::storage::ImportedDesignObject* HierarchyBrowserModel::object_at(const QModelIndex& index) const
{
    if (!index.isValid() || m_session == nullptr) {
        return nullptr;
    }
    const int kind_order = static_cast<int>(index.internalId()) - 1;
    for (const auto& group : m_groups) {
        if (group.kind_order == kind_order) {
            const int row = index.row();
            if (row >= 0 && row < static_cast<int>(group.object_indices.size())) {
                return &m_session->objects()[group.object_indices[row]];
            }
            return nullptr;
        }
    }
    return nullptr;
}

} // namespace aegis::ui
