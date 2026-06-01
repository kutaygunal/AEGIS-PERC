#include "aegis/ui/hierarchy_browser_panel.hpp"
#include "aegis/ui/hierarchy_browser_model.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QAbstractItemModel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace aegis::ui {
namespace {

class HierarchyBrowserFilterProxyModel final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override
    {
        if (!source_parent.isValid()) {
            // Parent row = kind group. Keep it if filter is empty OR any child matches.
            if (filterRegularExpression().pattern().isEmpty()) {
                return true;
            }
            const QModelIndex group_idx = sourceModel()->index(source_row, 0, source_parent);
            if (!group_idx.isValid()) {
                return false;
            }
            const int child_count = sourceModel()->rowCount(group_idx);
            for (int r = 0; r < child_count; ++r) {
                if (QSortFilterProxyModel::filterAcceptsRow(r, group_idx)) {
                    return true;
                }
            }
            return false;
        }

        return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
    }
};

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

    m_model = new HierarchyBrowserModel(this);
    m_proxy = new HierarchyBrowserFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterKeyColumn(0);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(false);
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
        apply_filter();
        update_status();
    });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                m_selected_stable_id.clear();
                if (current.isValid()) {
                    const QVariant stable_id_data = m_proxy->data(current, Qt::UserRole);
                    if (!stable_id_data.isNull()) {
                        m_selected_stable_id = stable_id_data.toString();
                        emit object_selected(m_selected_stable_id);
                    }
                }
            });

    update_status();
}

HierarchyBrowserPanel::~HierarchyBrowserPanel()
{
    // Be defensive about ownership/destruction ordering: the session is not a QObject and may already be freed.
    if (m_model != nullptr) {
        m_model->set_session(nullptr);
    }
    m_session = nullptr;
}


void HierarchyBrowserPanel::set_session(const aegis::storage::ImportedDesignSession* session)
{
    m_session = session;
    m_model->set_session(session);
    m_selected_stable_id.clear();

    apply_filter();
    update_status();

    if (m_tree == nullptr) {
        return;
    }

    // Preserve the "expanded categories" feel for small sessions, but avoid expanding huge trees.
    const int total = (m_session != nullptr) ? static_cast<int>(m_session->objects().size()) : 0;
    if (total > 0 && total <= 50000) {
        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            m_tree->expand(m_proxy->index(row, 0));
        }
    } else {
        m_tree->collapseAll();
    }
}

const aegis::storage::ImportedDesignSession* HierarchyBrowserPanel::session() const noexcept
{
    return m_session;
}

int HierarchyBrowserPanel::visible_item_count() const noexcept
{
    if (m_proxy == nullptr || m_tree == nullptr) {
        return 0;
    }
    int count = 0;
    for (int group_row = 0; group_row < m_proxy->rowCount(); ++group_row) {
        const QModelIndex group_idx = m_proxy->index(group_row, 0);
        if (!group_idx.isValid()) continue;
        if (!m_tree->isExpanded(group_idx)) continue;
        count += m_proxy->rowCount(group_idx);
    }
    return count;
}

QString HierarchyBrowserPanel::current_stable_id() const
{
    const auto* sel = m_tree->selectionModel();
    if (sel == nullptr) {
        return {};
    }
    const QModelIndex current = sel->currentIndex();
    if (!current.isValid()) {
        return {};
    }
    const QVariant stable_id_data = m_proxy->data(current, Qt::UserRole);
    return stable_id_data.isNull() ? QString{} : stable_id_data.toString();
}

QString HierarchyBrowserPanel::status_text() const
{
    return m_status->text();
}

bool HierarchyBrowserPanel::search_and_select(const QString& text)
{
    const QString trimmed = text.trimmed();
    m_search->setText(trimmed);
    apply_filter();

    if (trimmed.isEmpty()) {
        update_status();
        return true;
    }

    // After filtering, the first visible leaf is the first matching object.
    for (int group_row = 0; group_row < m_proxy->rowCount(); ++group_row) {
        const QModelIndex group_idx = m_proxy->index(group_row, 0);
        if (!group_idx.isValid()) continue;
        const int children = m_proxy->rowCount(group_idx);
        if (children <= 0) continue;
        const QModelIndex first = m_proxy->index(0, 0, group_idx);
        if (!first.isValid()) continue;
        m_tree->expand(group_idx);
        m_tree->setCurrentIndex(first);
        m_tree->scrollTo(first);
        update_status();
        return true;
    }

    const int total = m_session != nullptr ? static_cast<int>(m_session->objects().size()) : 0;
    m_status->setText(QString("No object found for '%1' (%2 total)").arg(trimmed).arg(total));
    return false;
}

void HierarchyBrowserPanel::select_by_stable_id(const QString& stable_id)
{
    if (m_model == nullptr || m_proxy == nullptr) {
        return;
    }
    const QModelIndex src = m_model->find_by_stable_id(stable_id);
    if (!src.isValid()) {
        return;
    }
    const QModelIndex dst = m_proxy->mapFromSource(src);
    if (!dst.isValid()) {
        return;
    }
    if (dst.parent().isValid()) {
        m_tree->expand(dst.parent());
    }
    m_tree->setCurrentIndex(dst);
    m_tree->scrollTo(dst);
    m_selected_stable_id = stable_id;
}

void HierarchyBrowserPanel::clear_selection()
{
    m_tree->clearSelection();
    if (m_tree->selectionModel() != nullptr) {
        m_tree->selectionModel()->clearCurrentIndex();
    }
    m_selected_stable_id.clear();
}

void HierarchyBrowserPanel::apply_filter()
{
    if (m_proxy == nullptr) {
        return;
    }
    const QString filter = m_search->text().trimmed();
    if (filter.isEmpty()) {
        m_proxy->setFilterRegularExpression(QRegularExpression{});
        return;
    }
    const QRegularExpression re(QRegularExpression::escape(filter), QRegularExpression::CaseInsensitiveOption);
    m_proxy->setFilterRegularExpression(re);
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
        status.append(QString(" -- selected: %1").arg(m_selected_stable_id));
    }
    m_status->setText(status);
}

} // namespace aegis::ui
