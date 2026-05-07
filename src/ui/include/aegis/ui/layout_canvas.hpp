#pragma once

#include "aegis/ui/scene_adapter.hpp"

class QPoint;

namespace aegis::ui { class SelectionModel; }

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QTransform>
#include <QWidget>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace aegis::ui {

class LayoutCanvas : public QWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);

    void set_scene(UiScene scene);
    [[nodiscard]] const UiScene& scene() const noexcept;
    [[nodiscard]] std::size_t scene_item_count() const noexcept;
    [[nodiscard]] bool has_scene() const noexcept;

    void set_background_color(const QColor& color);
    [[nodiscard]] QColor background_color() const noexcept;

    void set_selection_model(SelectionModel* selection_model);
    [[nodiscard]] SelectionModel* selection_model() const noexcept;
    [[nodiscard]] const SceneItem* hit_test_widget_position(const QPointF& widget_point) const;

    void set_layer_visibility(const std::string& layer_name, bool visible);
    [[nodiscard]] bool layer_visibility(const std::string& layer_name) const;
    void show_all_layers();
    void hide_all_layers();
    [[nodiscard]] std::vector<std::string> visible_layers() const;

    void set_grid_visible(bool visible);
    [[nodiscard]] bool grid_visible() const noexcept;
    void toggle_grid();

    void fit_to_view();
    void reset_view();
    void zoom_by(double factor, const QPointF& widget_anchor);
    void pan_by(const QPointF& widget_delta);

    [[nodiscard]] double zoom_level() const noexcept;
    [[nodiscard]] QPointF view_center() const noexcept;
    [[nodiscard]] QPointF widget_to_scene(const QPointF& widget_point) const;
    [[nodiscard]] QPointF scene_to_widget(const QPointF& scene_point) const;
    [[nodiscard]] QTransform scene_to_widget_transform() const;

signals:
    void cursor_position_changed(QPointF scene_position, double zoom_level);
    void viewport_changed(double zoom_level, QPointF view_center);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    UiScene m_scene;
    QColor m_background_color;
    bool m_grid_visible = true;
    std::map<std::string, bool> m_layer_visibility;
    double m_zoom_level = 1.0;
    QPointF m_view_center{0.0, 0.0};
    bool m_view_initialized = false;
    bool m_panning = false;
    bool m_space_pressed = false;
    QPoint m_last_pan_pos;

    [[nodiscard]] QRectF scene_rect() const noexcept;
    void ensure_view_initialized();
    SelectionModel* m_selection_model = nullptr;

    [[nodiscard]] double hit_test_distance_scene(const SceneItem& item, const QPointF& scene_point) const;
    void paint_grid(QPainter& painter);
    void paint_scene(QPainter& painter);
    void notify_viewport_changed();
};

} // namespace aegis::ui
