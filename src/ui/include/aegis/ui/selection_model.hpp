#pragma once

#include <QObject>
#include <QStringList>

#include <string>
#include <vector>

namespace aegis::ui {

class SelectionModel : public QObject {
    Q_OBJECT
public:
    explicit SelectionModel(QObject* parent = nullptr);

    void clear();
    void set_selected_ids(const std::vector<std::string>& ids);
    void select_only(const std::string& id);
    void toggle_selected(const std::string& id);

    [[nodiscard]] bool contains(const std::string& id) const;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const std::vector<std::string>& selected_ids() const noexcept;

    // Cross-probing support
    void select_and_emit(const std::string& id);
    [[nodiscard]] std::string primary_stable_id() const;
    void set_primary_stable_id(const std::string& stable_id);

signals:
    void selection_changed(QStringList selected_ids);

private:
    std::vector<std::string> m_selected_ids;

    void emit_selection_changed();
};

} // namespace aegis::ui
