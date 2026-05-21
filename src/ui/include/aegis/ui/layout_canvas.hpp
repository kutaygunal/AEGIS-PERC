#pragma once

#include "aegis/ui/connectivity_trace.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/ui/violation_heatmap.hpp"
#include "aegis/ui/violation_overlay.hpp"

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

struct CanvasPerformanceMetrics {
    double frame_time_ms = 0.0;
    std::size_t visible_item_count = 0;
    std::size_t total_item_count = 0;
    std::size_t overlay_count = 0;
    std::size_t simplified_item_count = 0;

    bool operator==(const CanvasPerformanceMetrics& other) const noexcept = default;
};

struct ViewportState {
    double zoom_level = 1.0;
    QPointF view_center{0.0, 0.0};

    bool operator==(const ViewportState& other) const noexcept = default;
};

class LayoutCanvas : public QWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);

    void set_scene(UiScene scene);
    [[nodiscard]] const UiScene& scene() const noexcept;
    [[nodiscard]] std::size_t scene_item_count() const noexcept;
    [[nodiscard]] bool has_scene() const noexcept;
    [[nodiscard]] QString empty_state_text() const;

    void set_background_color(const QColor& color);
    [[nodiscard]] QColor background_color() const noexcept;

    void set_selection_model(SelectionModel* selection_model);
    [[nodiscard]] SelectionModel* selection_model() const noexcept;
    [[nodiscard]] const SceneItem* hit_test_widget_position(const QPointF& widget_point) const;

    void set_violations(aegis::rules::ViolationCollection violations);
    [[nodiscard]] std::size_t violation_count() const noexcept;
    void set_heatmap_visible(bool visible);
    [[nodiscard]] bool heatmap_visible() const noexcept;
    void set_heatmap_opacity(double opacity);
    [[nodiscard]] double heatmap_opacity() const noexcept;
    [[nodiscard]] std::size_t heatmap_bucket_count() const noexcept;
    [[nodiscard]] QString heatmap_empty_state_text() const;
    void set_trace_result(ConnectivityTraceResult trace_result);
    void clear_trace();
    [[nodiscard]] bool has_active_trace() const noexcept;
    [[nodiscard]] std::size_t traced_item_count() const noexcept;
    void focus_trace();
    void set_violation_overlays_visible(bool visible);
    [[nodiscard]] bool violation_overlays_visible() const noexcept;
    [[nodiscard]] int unresolved_violation_count() const noexcept;
    [[nodiscard]] const std::vector<ViolationOverlayItem>& violation_overlays() const noexcept;

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
    void center_on_scene_point(const QPointF& scene_point);
    [[nodiscard]] bool center_on_violation(const std::string& violation_id);
    void zoom_by(double factor, const QPointF& widget_anchor);
    void pan_by(const QPointF& widget_delta);

    // Viewport presets / large-scene navigation
    [[nodiscard]] ViewportState current_viewport_state() const noexcept;
    void restore_viewport_state(const ViewportState& state);
    void push_viewport_state();
    [[nodiscard]] bool can_viewport_back() const noexcept;
    [[nodiscard]] bool can_viewport_forward() const noexcept;
    void viewport_back();
    void viewport_forward();
    void zoom_to_selection();
    void zoom_to_violations();
    void zoom_to_trace();
    void jump_to_coordinate(const QPointF& scene_point, double zoom);

    [[nodiscard]] double zoom_level() const noexcept;
    [[nodiscard]] QPointF view_center() const noexcept;
    [[nodiscard]] QPointF widget_to_scene(const QPointF& widget_point) const;
    [[nodiscard]] QPointF scene_to_widget(const QPointF& scene_point) const;
    [[nodiscard]] QTransform scene_to_widget_transform() const;

    void set_performance_metrics_visible(bool visible);
    [[nodiscard]] bool performance_metrics_visible() const noexcept;
    [[nodiscard]] const CanvasPerformanceMetrics& performance_metrics() const noexcept;
    [[nodiscard]] QString performance_metrics_text() const;
    [[nodiscard]] std::size_t lod_cache_item_count() const noexcept;

signals:
    void cursor_position_changed(QPointF scene_position, double zoom_level);
    void viewport_changed(double zoom_level, QPointF view_center);
    void performance_metrics_changed();

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
    aegis::rules::ViolationCollection m_violations;
    ConnectivityTraceResult m_trace_result;
    ViolationHeatmapSettings m_heatmap_settings;
    std::vector<ViolationHeatmapBucket> m_heatmap_buckets;
    std::vector<ViolationOverlayItem> m_violation_overlays;
    bool m_violation_overlays_visible = true;
    int m_unresolved_violation_count = 0;

    struct LodCacheEntry {
        std::size_t item_index = 0;
        bool simplified = false;
    };

    void rebuild_heatmap();
    void rebuild_violation_overlays();
    void rebuild_lod_cache();
    void invalidate_lod_cache() noexcept;
    [[nodiscard]] double hit_test_distance_scene(const SceneItem& item, const QPointF& scene_point) const;
    void paint_grid(QPainter& painter);
    std::size_t paint_scene(QPainter& painter);
    std::size_t paint_heatmap(QPainter& painter);
    std::size_t paint_violation_overlays(QPainter& painter);
    void notify_viewport_changed();

    bool m_performance_metrics_visible = false;
    CanvasPerformanceMetrics m_performance_metrics;
    std::vector<LodCacheEntry> m_lod_cache;
    bool m_lod_cache_valid = false;
    double m_lod_cache_zoom = 0.0;
    QPointF m_lod_cache_center{0.0, 0.0};
    QSize m_lod_cache_size;

    // Viewport history for large-scene navigation
    std::vector<ViewportState> m_viewport_history;
    std::size_t m_viewport_history_index = 0;
    bool m_viewport_history_pushing = false;
    static constexpr std::size_t kMaxViewportHistory = 32;
    void prune_viewport_history();
    void fit_to_rect(const QRectF& rect);
};

} // namespace aegis::ui
