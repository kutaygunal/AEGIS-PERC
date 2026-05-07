#pragma once

#include "aegis/ui/scene_adapter.hpp"

#include <QWidget>

class QLabel;

namespace aegis::ui {

class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    void set_scene(const UiScene& scene);
    void set_selected_ids(const QStringList& ids);

    [[nodiscard]] QString summary_text() const;

private:
    UiScene m_scene;
    QLabel* m_summary = nullptr;

    void update_summary(const QStringList& ids);
};

} // namespace aegis::ui
