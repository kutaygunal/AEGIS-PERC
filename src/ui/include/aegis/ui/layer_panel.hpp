#pragma once

#include "aegis/ui/scene_adapter.hpp"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QPushButton;

namespace aegis::ui {

enum class VisibilityPreset {
    All,
    RoutingOnly,
    MacrosOnly,
    PinsAndPorts,
    PowerFocused,
    ViolationReview,
};

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    void set_layers(const std::vector<SceneLayer>& layers);
    [[nodiscard]] int layer_count() const noexcept;
    [[nodiscard]] QStringList layer_names() const;
    [[nodiscard]] bool layer_visible(const QString& layer_name) const;

    void set_layer_visible(const QString& layer_name, bool visible);
    void show_all_layers();
    void hide_all_layers();
    void isolate_layer(const QString& layer_name);

    [[nodiscard]] std::vector<std::pair<QString, SceneLayerCategory>> layer_categories() const;
    void apply_visibility_preset(VisibilityPreset preset);
    void set_category_visible(SceneLayerCategory category, bool visible);
    [[nodiscard]] VisibilityPreset current_visibility_preset() const noexcept;

signals:
    void layer_visibility_changed(QString layer_name, bool visible);

private:
    QListWidget* m_list = nullptr;
    QPushButton* m_show_all_button = nullptr;
    QPushButton* m_hide_all_button = nullptr;
    QPushButton* m_isolate_button = nullptr;
    QComboBox* m_preset_combo = nullptr;
    bool m_updating = false;
    VisibilityPreset m_current_preset = VisibilityPreset::All;

    void update_item_presentation(QListWidgetItem* item);
    QListWidgetItem* find_item(const QString& layer_name) const;
};

} // namespace aegis::ui
