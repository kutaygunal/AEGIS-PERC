#include "aegis/ui/selection_model.hpp"

#include <algorithm>

namespace aegis::ui {

SelectionModel::SelectionModel(QObject* parent)
    : QObject(parent)
{
}

void SelectionModel::clear()
{
    if (m_selected_ids.empty()) {
        return;
    }
    m_selected_ids.clear();
    emit_selection_changed();
}

void SelectionModel::set_selected_ids(const std::vector<std::string>& ids)
{
    if (m_selected_ids == ids) {
        return;
    }
    m_selected_ids = ids;
    emit_selection_changed();
}

void SelectionModel::select_only(const std::string& id)
{
    if (id.empty()) {
        clear();
        return;
    }
    set_selected_ids({id});
}

void SelectionModel::toggle_selected(const std::string& id)
{
    if (id.empty()) {
        return;
    }

    auto it = std::find(m_selected_ids.begin(), m_selected_ids.end(), id);
    if (it == m_selected_ids.end()) {
        m_selected_ids.push_back(id);
    } else {
        m_selected_ids.erase(it);
    }
    emit_selection_changed();
}

bool SelectionModel::contains(const std::string& id) const
{
    return std::find(m_selected_ids.begin(), m_selected_ids.end(), id) != m_selected_ids.end();
}

bool SelectionModel::empty() const noexcept
{
    return m_selected_ids.empty();
}

const std::vector<std::string>& SelectionModel::selected_ids() const noexcept
{
    return m_selected_ids;
}

void SelectionModel::emit_selection_changed()
{
    QStringList ids;
    for (const auto& id : m_selected_ids) {
        ids.append(QString::fromStdString(id));
    }
    emit selection_changed(ids);
}

void SelectionModel::select_and_emit(const std::string& id)
{
    if (id.empty()) {
        clear();
        return;
    }
    if (m_selected_ids.size() == 1 && m_selected_ids.front() == id) {
        emit_selection_changed();
        return;
    }
    m_selected_ids = {id};
    emit_selection_changed();
}

std::string SelectionModel::primary_stable_id() const
{
    return m_selected_ids.empty() ? std::string{} : m_selected_ids.front();
}

void SelectionModel::set_primary_stable_id(const std::string& stable_id)
{
    if (stable_id.empty()) {
        clear();
        return;
    }
    if (!m_selected_ids.empty() && m_selected_ids.front() == stable_id) {
        return;
    }
    m_selected_ids.insert(m_selected_ids.begin(), stable_id);
    if (m_selected_ids.size() > 1) {
        m_selected_ids.resize(1);
    }
    emit_selection_changed();
}

} // namespace aegis::ui
