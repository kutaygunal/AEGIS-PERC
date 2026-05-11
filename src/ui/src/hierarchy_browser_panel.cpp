#include "aegis/ui/hierarchy_browser_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

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

HierarchyBrowserPanel::HierarchyBrowserPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* top = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search objects by name");
    m_search->setClearButtonEnabled(true);
    auto* find_button = new QPushButton("Find", this);
    top->addWidget(m_search, 1);
    top->addWidget(find_button);
    root->addLayout(top);

    m_status = new QLabel(state_text::hierarchy_no_session(), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Object", "Kind"});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(false);
    root->addWidget(m_tree, 1);

    connect(find_button, &QPushButton::clicked, this, [this]() {
        Q_UNUSED(search_and_select(m_search->text()));
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        Q_UNUSED(search_and_select(m_search->text()));
    });
    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        apply_search_filter();
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                m_selected_stable_id = current != nullptr ? current->data(0, Qt::UserRole).toString() : QString{};
                if (current == nullptr) {
                    return;
                }
                const QString stable_id = current->data(0, Qt::UserRole).toString();
                if (!stable_id.isEmpty()) {
                    emit object_selected(stable_id);
                }
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr) {
                    return;
                }
                const QString stable_id = item->data(0, Qt::UserRole).toString();
                if (!stable_id.isEmpty()) {
                    emit object_double_clicked(stable_id);
                    emit cross_probe_requested(stable_id, "hierarchy");
                }
            });

    update_status();
}

void HierarchyBrowserPanel::set_session(const aegis::storage::ImportedDesignSession* session)
{
    m_session = session;
    rebuild_tree();
}

const aegis::storage::ImportedDesignSession* HierarchyBrowserPanel::session() const noexcept
{
    return m_session;
}

int HierarchyBrowserPanel::visible_item_count() const noexcept
{
    int count = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* group = m_tree->topLevelItem(i);
        if (group == nullptr) {
            continue;
        }
        for (int j = 0; j < group->childCount(); ++j) {
            if (!group->child(j)->isHidden()) {
                ++count;
            }
        }
    }
    return count;
}

QString HierarchyBrowserPanel::current_stable_id() const
{
    const auto* current = m_tree->currentItem();
    return current != nullptr ? current->data(0, Qt::UserRole).toString() : QString{};
}

QString HierarchyBrowserPanel::status_text() const
{
    return m_status->text();
}

bool HierarchyBrowserPanel::search_and_select(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_status->setText(state_text::hierarchy_enter_search());
        return false;
    }
    if (m_session == nullptr) {
        m_status->setText(state_text::hierarchy_no_session());
        return false;
    }
    if (m_session->objects().empty()) {
        m_status->setText(state_text::hierarchy_empty());
        return false;
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto* child = group->child(j);
            if (child->isHidden()) {
                continue;
            }
            if (child->text(0).compare(trimmed, Qt::CaseInsensitive) == 0) {
                m_tree->setCurrentItem(child);
                m_tree->scrollToItem(child);
                update_status();
                return true;
            }
        }
    }
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto* child = group->child(j);
            if (child->isHidden()) {
                continue;
            }
            if (child->text(0).contains(trimmed, Qt::CaseInsensitive)) {
                m_tree->setCurrentItem(child);
                m_tree->scrollToItem(child);
                update_status();
                return true;
            }
        }
    }

    const int total = static_cast<int>(m_session->objects().size());
    m_status->setText(QString("No object found for '%1' (%2 total)").arg(trimmed).arg(total));
    return false;
}

void HierarchyBrowserPanel::select_by_stable_id(const QString& stable_id)
{
    if (auto* item = find_item_by_stable_id(stable_id)) {
        m_tree->setCurrentItem(item);
        m_tree->scrollToItem(item);
        m_selected_stable_id = stable_id;
    }
}

void HierarchyBrowserPanel::clear_selection()
{
    m_tree->clearSelection();
    m_tree->setCurrentItem(nullptr);
    m_selected_stable_id.clear();
}

void HierarchyBrowserPanel::rebuild_tree()
{
    const QString desired_selection = m_selected_stable_id;
    m_tree->clear();
    m_selected_stable_id.clear();

    if (m_session == nullptr) {
        update_status();
        return;
    }

    std::map<int, QTreeWidgetItem*> groups;
    const auto& objects = m_session->objects();

    // Build groups first
    for (const auto& object : objects) {
        const int order = kind_sort_order(object.kind);
        if (groups.find(order) == groups.end()) {
            const QString label = kind_label(object.kind);
            auto* group = new QTreeWidgetItem(m_tree, {label, ""});
            group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            group->setExpanded(true);
            groups.emplace(order, group);
        }
    }

    // Populate items
    for (const auto& object : objects) {
        const int order = kind_sort_order(object.kind);
        auto* group = groups.at(order);
        const QString display = QString::fromStdString(object.display_name.empty() ? object.name : object.display_name);
        const QString kind = QString::fromStdString(aegis::storage::to_string(object.kind));
        add_object_item(group, display, QString::fromStdString(object.stable_id), kind);
    }

    // Sort each group by display name
    for (auto& [order, group] : groups) {
        group->sortChildren(0, Qt::AscendingOrder);
    }

    if (!desired_selection.isEmpty()) {
        if (auto* item = find_item_by_stable_id(desired_selection)) {
            m_tree->setCurrentItem(item);
            m_selected_stable_id = desired_selection;
        }
    }

    apply_search_filter();
    update_status();
}

void HierarchyBrowserPanel::apply_search_filter()
{
    const QString filter = m_search->text().trimmed();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* group = m_tree->topLevelItem(i);
        if (group == nullptr) {
            continue;
        }
        bool any_visible = false;
        for (int j = 0; j < group->childCount(); ++j) {
            auto* child = group->child(j);
            if (child == nullptr) {
                continue;
            }
            const bool matches = filter.isEmpty() || child->text(0).contains(filter, Qt::CaseInsensitive);
            child->setHidden(!matches);
            if (matches) {
                any_visible = true;
            }
        }
        group->setHidden(!filter.isEmpty() && !any_visible);
    }
}

void HierarchyBrowserPanel::update_status()
{
    if (m_session == nullptr) {
        m_status->setText(state_text::hierarchy_no_session());
        return;
    }

    const int total = static_cast<int>(m_session->objects().size());
    if (total == 0) {
        m_status->setText(state_text::hierarchy_empty());
        return;
    }

    const int visible = visible_item_count();
    QString status = QString("%1 / %2 objects visible").arg(visible).arg(total);
    if (!m_selected_stable_id.isEmpty()) {
        status.append(QString(" — selected: %1").arg(m_selected_stable_id));
    }
    m_status->setText(status);
}

QTreeWidgetItem* HierarchyBrowserPanel::find_item_by_stable_id(const QString& stable_id) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* group = m_tree->topLevelItem(i);
        if (group == nullptr) {
            continue;
        }
        for (int j = 0; j < group->childCount(); ++j) {
            auto* child = group->child(j);
            if (child != nullptr && child->data(0, Qt::UserRole).toString() == stable_id) {
                return child;
            }
        }
    }
    return nullptr;
}

void HierarchyBrowserPanel::add_object_item(QTreeWidgetItem* parent,
                                             const QString& label,
                                             const QString& stable_id,
                                             const QString& kind)
{
    auto* item = parent != nullptr
        ? new QTreeWidgetItem(parent, {label, kind})
        : new QTreeWidgetItem(m_tree, {label, kind});
    item->setData(0, Qt::UserRole, stable_id);
}

} // namespace aegis::ui
