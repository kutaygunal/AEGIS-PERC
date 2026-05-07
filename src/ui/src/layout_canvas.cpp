#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/selection_model.hpp"
#include "aegis/ui/violation_heatmap.hpp"
#include "aegis/ui/violation_overlay.hpp"

#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace aegis::ui {
namespace {

constexpr int kViewportMargin = 24;
constexpr double kMinZoom = 0.0001;
constexpr double kMaxZoom = 100000.0;
constexpr double kZoomStepBase = 1.0015;
constexpr double kBaseGridStep = 10.0;
constexpr double kMinVisiblePixels = 0.75;

[[nodiscard]] QColor color_from_scene(const std::string& color)
{
    const QColor parsed(QString::fromStdString(color));
    return parsed.isValid() ? parsed : QColor("#808080");
}

[[nodiscard]] QRectF bounds_to_rect(const SceneBounds& bounds)
{
    if (!bounds.valid) {
        return QRectF{};
    }
    return QRectF(QPointF(bounds.min_x, bounds.min_y), QPointF(bounds.max_x, bounds.max_y)).normalized();
}

[[nodiscard]] double nice_grid_step(double target_scene_units)
{
    if (target_scene_units <= 0.0) {
        return kBaseGridStep;
    }

    const double exponent = std::floor(std::log10(target_scene_units));
    const double base = std::pow(10.0, exponent);
    const double normalized = target_scene_units / base;

    if (normalized <= 1.0) return base;
    if (normalized <= 2.0) return 2.0 * base;
    if (normalized <= 5.0) return 5.0 * base;
    return 10.0 * base;
}

[[nodiscard]] QRectF point_pick_rect(const QPointF& center, double radius)
{
    return QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
}

[[nodiscard]] bool item_matches_violation_location(const SceneItem& item,
                                                   const aegis::rules::ViolationLocation& location)
{
    if (location.layer.has_value() && item.layer_name != *location.layer) {
        return false;
    }

    if (location.pin_name.has_value()) {
        const auto it = item.source_metadata.find("name");
        if (it == item.source_metadata.end() || it->second != *location.pin_name) {
            return false;
        }
    }

    if (location.net_name.has_value()) {
        const auto it = item.source_metadata.find("net_name");
        if (it != item.source_metadata.end() && it->second == *location.net_name) {
            return true;
        }
        if (!location.pin_name.has_value() && !location.layer.has_value()) {
            return false;
        }
    }

    return true;
}

} // namespace

LayoutCanvas::LayoutCanvas(QWidget* parent)
    : QWidget(parent)
    , m_background_color("#101318")
{
    setObjectName("LayoutCanvas");
    setMinimumSize(320, 240);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void LayoutCanvas::set_scene(UiScene scene)
{
    m_scene = std::move(scene);
    m_layer_visibility.clear();
    for (const auto& layer : m_scene.layers) {
        m_layer_visibility[layer.name] = true;
    }
    rebuild_violation_overlays();
    rebuild_heatmap();
    invalidate_lod_cache();
    m_performance_metrics.total_item_count = m_scene.items.size();
    m_performance_metrics.visible_item_count = 0;
    m_performance_metrics.simplified_item_count = 0;
    m_view_initialized = false;
    fit_to_view();
}

const UiScene& LayoutCanvas::scene() const noexcept
{
    return m_scene;
}

std::size_t LayoutCanvas::scene_item_count() const noexcept
{
    return m_scene.items.size();
}

bool LayoutCanvas::has_scene() const noexcept
{
    return !m_scene.items.empty();
}

void LayoutCanvas::set_background_color(const QColor& color)
{
    m_background_color = color.isValid() ? color : QColor("#101318");
    update();
}

QColor LayoutCanvas::background_color() const noexcept
{
    return m_background_color;
}

void LayoutCanvas::set_selection_model(SelectionModel* selection_model)
{
    m_selection_model = selection_model;
    update();
}

SelectionModel* LayoutCanvas::selection_model() const noexcept
{
    return m_selection_model;
}

const SceneItem* LayoutCanvas::hit_test_widget_position(const QPointF& widget_point) const
{
    if (!has_scene()) {
        return nullptr;
    }

    const QPointF scene_point = widget_to_scene(widget_point);
    const SceneItem* best = nullptr;
    double best_distance = std::numeric_limits<double>::max();

    for (const auto& item : m_scene.items) {
        if (!item.layer_name.empty() && !layer_visibility(item.layer_name)) {
            continue;
        }
        const double distance = hit_test_distance_scene(item, scene_point);
        if (distance < best_distance) {
            best_distance = distance;
            best = &item;
        }
    }

    return best;
}

void LayoutCanvas::set_violations(aegis::rules::ViolationCollection violations)
{
    m_violations = std::move(violations);
    rebuild_violation_overlays();
    rebuild_heatmap();
    update();
}

std::size_t LayoutCanvas::violation_count() const noexcept
{
    return m_violations.size();
}

void LayoutCanvas::set_heatmap_visible(bool visible)
{
    if (m_heatmap_settings.visible == visible) {
        return;
    }
    m_heatmap_settings.visible = visible;
    update();
}

bool LayoutCanvas::heatmap_visible() const noexcept
{
    return m_heatmap_settings.visible;
}

void LayoutCanvas::set_heatmap_opacity(double opacity)
{
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    if (std::abs(m_heatmap_settings.opacity - clamped) < 1e-9) {
        return;
    }
    m_heatmap_settings.opacity = clamped;
    update();
}

double LayoutCanvas::heatmap_opacity() const noexcept
{
    return m_heatmap_settings.opacity;
}

std::size_t LayoutCanvas::heatmap_bucket_count() const noexcept
{
    return m_heatmap_buckets.size();
}

QString LayoutCanvas::heatmap_empty_state_text() const
{
    return m_heatmap_buckets.empty() ? QString("No violation heatmap data") : QString{};
}

void LayoutCanvas::set_trace_result(ConnectivityTraceResult trace_result)
{
    m_trace_result = std::move(trace_result);
    update();
}

void LayoutCanvas::clear_trace()
{
    m_trace_result = {};
    update();
}

bool LayoutCanvas::has_active_trace() const noexcept
{
    return m_trace_result.resolved;
}

std::size_t LayoutCanvas::traced_item_count() const noexcept
{
    return m_trace_result.related_scene_item_ids.size();
}

void LayoutCanvas::focus_trace()
{
    if (m_trace_result.focus_point.has_value()) {
        center_on_scene_point(*m_trace_result.focus_point);
    }
}

void LayoutCanvas::set_violation_overlays_visible(bool visible)
{
    if (m_violation_overlays_visible == visible) {
        return;
    }
    m_violation_overlays_visible = visible;
    update();
}

bool LayoutCanvas::violation_overlays_visible() const noexcept
{
    return m_violation_overlays_visible;
}

int LayoutCanvas::unresolved_violation_count() const noexcept
{
    return m_unresolved_violation_count;
}

const std::vector<ViolationOverlayItem>& LayoutCanvas::violation_overlays() const noexcept
{
    return m_violation_overlays;
}

void LayoutCanvas::set_layer_visibility(const std::string& layer_name, bool visible)
{
    if (layer_name.empty()) {
        return;
    }
    m_layer_visibility[layer_name] = visible;
    invalidate_lod_cache();
    update();
}

bool LayoutCanvas::layer_visibility(const std::string& layer_name) const
{
    const auto it = m_layer_visibility.find(layer_name);
    return it == m_layer_visibility.end() ? true : it->second;
}

void LayoutCanvas::show_all_layers()
{
    for (auto& [_, visible] : m_layer_visibility) {
        visible = true;
    }
    invalidate_lod_cache();
    update();
}

void LayoutCanvas::hide_all_layers()
{
    for (auto& [_, visible] : m_layer_visibility) {
        visible = false;
    }
    invalidate_lod_cache();
    update();
}

std::vector<std::string> LayoutCanvas::visible_layers() const
{
    std::vector<std::string> layers;
    for (const auto& [name, visible] : m_layer_visibility) {
        if (visible) {
            layers.push_back(name);
        }
    }
    return layers;
}

void LayoutCanvas::set_grid_visible(bool visible)
{
    if (m_grid_visible == visible) {
        return;
    }
    m_grid_visible = visible;
    update();
}

bool LayoutCanvas::grid_visible() const noexcept
{
    return m_grid_visible;
}

void LayoutCanvas::toggle_grid()
{
    set_grid_visible(!m_grid_visible);
}

void LayoutCanvas::fit_to_view()
{
    const QRectF source = scene_rect();
    const QRectF target = rect().adjusted(kViewportMargin, kViewportMargin,
                                          -kViewportMargin, -kViewportMargin);
    const double target_width = std::max(1.0, target.width());
    const double target_height = std::max(1.0, target.height());

    const double sx = target_width / std::max(1.0, source.width());
    const double sy = target_height / std::max(1.0, source.height());
    m_zoom_level = std::clamp(std::min(sx, sy), kMinZoom, kMaxZoom);
    m_view_center = source.center();
    m_view_initialized = true;
    invalidate_lod_cache();
    notify_viewport_changed();
    update();
}

void LayoutCanvas::reset_view()
{
    m_zoom_level = 1.0;
    m_view_center = scene_rect().center();
    m_view_initialized = true;
    invalidate_lod_cache();
    notify_viewport_changed();
    update();
}

void LayoutCanvas::center_on_scene_point(const QPointF& scene_point)
{
    ensure_view_initialized();
    m_view_center = scene_point;
    invalidate_lod_cache();
    notify_viewport_changed();
    update();
}

bool LayoutCanvas::center_on_violation(const std::string& violation_id)
{
    const auto it = std::find_if(m_violation_overlays.begin(), m_violation_overlays.end(),
                                 [&violation_id](const ViolationOverlayItem& overlay) {
                                     return overlay.violation_id == violation_id;
                                 });
    if (it == m_violation_overlays.end() || !it->resolved) {
        return false;
    }
    if (it->point.has_value()) {
        center_on_scene_point(*it->point);
        return true;
    }
    if (it->bounds.has_value()) {
        center_on_scene_point(it->bounds->center());
        return true;
    }
    return false;
}

void LayoutCanvas::zoom_by(double factor, const QPointF& widget_anchor)
{
    if (factor <= 0.0 || !std::isfinite(factor)) {
        return;
    }

    ensure_view_initialized();
    const QPointF scene_anchor_before = widget_to_scene(widget_anchor);
    m_zoom_level = std::clamp(m_zoom_level * factor, kMinZoom, kMaxZoom);
    const QPointF scene_anchor_after = widget_to_scene(widget_anchor);
    m_view_center += scene_anchor_before - scene_anchor_after;
    invalidate_lod_cache();
    notify_viewport_changed();
    update();
}

void LayoutCanvas::pan_by(const QPointF& widget_delta)
{
    ensure_view_initialized();
    if (m_zoom_level <= 0.0) {
        return;
    }

    m_view_center -= QPointF(widget_delta.x() / m_zoom_level,
                             -widget_delta.y() / m_zoom_level);
    invalidate_lod_cache();
    notify_viewport_changed();
    update();
}

double LayoutCanvas::zoom_level() const noexcept
{
    return m_zoom_level;
}

QPointF LayoutCanvas::view_center() const noexcept
{
    return m_view_center;
}

QPointF LayoutCanvas::widget_to_scene(const QPointF& widget_point) const
{
    bool invertible = false;
    const QTransform inverse = scene_to_widget_transform().inverted(&invertible);
    return invertible ? inverse.map(widget_point) : QPointF{};
}

QPointF LayoutCanvas::scene_to_widget(const QPointF& scene_point) const
{
    return scene_to_widget_transform().map(scene_point);
}

void LayoutCanvas::set_performance_metrics_visible(bool visible)
{
    if (m_performance_metrics_visible == visible) {
        return;
    }
    m_performance_metrics_visible = visible;
    emit performance_metrics_changed();
    update();
}

bool LayoutCanvas::performance_metrics_visible() const noexcept
{
    return m_performance_metrics_visible;
}

const CanvasPerformanceMetrics& LayoutCanvas::performance_metrics() const noexcept
{
    return m_performance_metrics;
}

QString LayoutCanvas::performance_metrics_text() const
{
    return QString("Frame %1 ms | Visible %2/%3 | Overlays %4 | Simplified %5")
        .arg(m_performance_metrics.frame_time_ms, 0, 'f', 2)
        .arg(m_performance_metrics.visible_item_count)
        .arg(m_performance_metrics.total_item_count)
        .arg(m_performance_metrics.overlay_count)
        .arg(m_performance_metrics.simplified_item_count);
}

std::size_t LayoutCanvas::lod_cache_item_count() const noexcept
{
    return m_lod_cache.size();
}

void LayoutCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    ensure_view_initialized();

    QElapsedTimer timer;
    timer.start();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), m_background_color);

    if (m_grid_visible) {
        paint_grid(painter);
    }

    m_performance_metrics.total_item_count = m_scene.items.size();
    m_performance_metrics.visible_item_count = 0;
    m_performance_metrics.simplified_item_count = 0;
    m_performance_metrics.overlay_count = 0;

    if (!has_scene() || !m_scene.bounds.valid) {
        painter.setPen(QColor("#AAB2BF"));
        painter.drawText(rect(), Qt::AlignCenter, "No layout scene loaded");
        m_performance_metrics.frame_time_ms = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
        emit performance_metrics_changed();
        return;
    }

    m_performance_metrics.visible_item_count = paint_scene(painter);

    if (m_heatmap_settings.visible) {
        Q_UNUSED(paint_heatmap(painter));
    }
    if (m_violation_overlays_visible) {
        m_performance_metrics.overlay_count = paint_violation_overlays(painter);
    }

    if (m_heatmap_settings.visible && m_heatmap_buckets.empty()) {
        painter.setPen(QColor("#AAB2BF"));
        painter.drawText(rect().adjusted(0, 24, 0, 0), Qt::AlignCenter, heatmap_empty_state_text());
    }

    m_performance_metrics.frame_time_ms = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    emit performance_metrics_changed();
}

void LayoutCanvas::wheelEvent(QWheelEvent* event)
{
    const double factor = std::pow(kZoomStepBase, event->angleDelta().y());
    zoom_by(factor, event->position());
    event->accept();
}

void LayoutCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && m_space_pressed)) {
        m_panning = true;
        m_last_pan_pos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_selection_model != nullptr) {
        const SceneItem* hit = hit_test_widget_position(event->position());
        const std::string hit_id = hit != nullptr ? hit->id : std::string{};
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            if (!hit_id.empty()) {
                m_selection_model->toggle_selected(hit_id);
            }
        } else {
            m_selection_model->select_only(hit_id);
        }
        update();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void LayoutCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_last_pan_pos;
        m_last_pan_pos = event->pos();
        pan_by(QPointF(delta));
        event->accept();
    } else {
        emit cursor_position_changed(widget_to_scene(event->position()), m_zoom_level);
        QWidget::mouseMoveEvent(event);
    }
}

void LayoutCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_space_pressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void LayoutCanvas::keyPressEvent(QKeyEvent* event)
{
    if (!event->isAutoRepeat() && event->key() == Qt::Key_Space) {
        m_space_pressed = true;
        if (!m_panning) {
            setCursor(Qt::OpenHandCursor);
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void LayoutCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (!event->isAutoRepeat() && event->key() == Qt::Key_Space) {
        m_space_pressed = false;
        if (!m_panning) {
            setCursor(Qt::ArrowCursor);
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

QRectF LayoutCanvas::scene_rect() const noexcept
{
    QRectF scene = bounds_to_rect(m_scene.bounds);
    if (!scene.isValid()) {
        scene = QRectF(-50.0, -50.0, 100.0, 100.0);
    }
    if (scene.width() <= 0.0) {
        scene.setWidth(1.0);
    }
    if (scene.height() <= 0.0) {
        scene.setHeight(1.0);
    }
    return scene;
}

void LayoutCanvas::ensure_view_initialized()
{
    if (!m_view_initialized) {
        fit_to_view();
    }
}

QTransform LayoutCanvas::scene_to_widget_transform() const
{
    QTransform transform;
    transform.translate(width() / 2.0, height() / 2.0);
    transform.scale(m_zoom_level, -m_zoom_level);
    transform.translate(-m_view_center.x(), -m_view_center.y());
    return transform;
}

void LayoutCanvas::paint_grid(QPainter& painter)
{
    if (m_zoom_level <= 0.0) {
        return;
    }

    const QPointF top_left = widget_to_scene(QPointF(0.0, 0.0));
    const QPointF bottom_right = widget_to_scene(QPointF(width(), height()));
    const double min_x = std::min(top_left.x(), bottom_right.x());
    const double max_x = std::max(top_left.x(), bottom_right.x());
    const double min_y = std::min(top_left.y(), bottom_right.y());
    const double max_y = std::max(top_left.y(), bottom_right.y());

    const double step = nice_grid_step(40.0 / m_zoom_level);
    const double start_x = std::floor(min_x / step) * step;
    const double start_y = std::floor(min_y / step) * step;

    painter.save();
    painter.setTransform(scene_to_widget_transform());
    painter.setPen(QPen(QColor(255, 255, 255, 32), 0.0));

    for (double x = start_x; x <= max_x; x += step) {
        painter.drawLine(QPointF(x, min_y), QPointF(x, max_y));
    }
    for (double y = start_y; y <= max_y; y += step) {
        painter.drawLine(QPointF(min_x, y), QPointF(max_x, y));
    }

    painter.setPen(QPen(QColor(255, 255, 255, 70), 0.0));
    painter.drawLine(QPointF(0.0, min_y), QPointF(0.0, max_y));
    painter.drawLine(QPointF(min_x, 0.0), QPointF(max_x, 0.0));
    painter.restore();
}

std::size_t LayoutCanvas::paint_scene(QPainter& painter)
{
    rebuild_lod_cache();

    painter.save();
    painter.setTransform(scene_to_widget_transform());

    for (const auto& entry : m_lod_cache) {
        const auto& item = m_scene.items.at(entry.item_index);

        QColor color = color_from_scene(item.color);
        QColor fill = color;
        fill.setAlpha(96);
        QColor stroke = color.lighter(135);
        stroke.setAlpha(220);
        const bool traced = std::find(m_trace_result.related_scene_item_ids.begin(),
                                      m_trace_result.related_scene_item_ids.end(),
                                      item.id) != m_trace_result.related_scene_item_ids.end()
                         || m_trace_result.traced_layers.contains(item.layer_name);
        const bool selected = m_selection_model != nullptr && m_selection_model->contains(item.id);
        if (traced) {
            stroke = QColor("#4FC3F7");
            fill = stroke;
            fill.setAlpha(100);
        }
        if (selected) {
            stroke = QColor("#FFD54F");
            fill = stroke;
            fill.setAlpha(120);
        }

        painter.setPen(QPen(stroke, 0.0));
        painter.setBrush(fill);

        if (entry.simplified) {
            const QPointF center = bounds_to_rect(item.bounds).center();
            const double radius = 1.5 / std::max(m_zoom_level, kMinZoom);
            painter.drawEllipse(center, radius, radius);
            continue;
        }

        if (item.shape_kind == SceneShapeKind::Rectangle) {
            painter.drawRect(bounds_to_rect(item.bounds));
        } else if (item.shape_kind == SceneShapeKind::Polygon) {
            QPolygonF polygon;
            for (const auto& point : item.points) {
                polygon << QPointF(point.x, point.y);
            }
            painter.drawPolygon(polygon);
        } else if (item.shape_kind == SceneShapeKind::Point && !item.points.empty()) {
            const QPointF point(item.points.front().x, item.points.front().y);
            painter.drawEllipse(point, 3.0, 3.0);
        }
    }

    painter.restore();

    painter.setPen(QColor("#AAB2BF"));
    const QString metrics = m_performance_metrics_visible
        ? performance_metrics_text()
        : QString("%1 items | %2% | %3 vio | %4 trace")
              .arg(m_scene.items.size())
              .arg(QString::number(m_zoom_level * 100.0, 'f', 1))
              .arg(m_violations.size())
              .arg(m_trace_result.related_scene_item_ids.size());
    painter.drawText(12, 20, metrics);
    return m_lod_cache.size();
}

std::size_t LayoutCanvas::paint_heatmap(QPainter& painter)
{
    painter.save();
    painter.setTransform(scene_to_widget_transform());

    int max_count = 0;
    for (const auto& bucket : m_heatmap_buckets) {
        if (bucket.layer_name.has_value() && !layer_visibility(*bucket.layer_name)) {
            continue;
        }
        max_count = std::max(max_count, bucket.count);
    }
    if (max_count <= 0) {
        painter.restore();
        return 0;
    }

    const double radius = std::max(scene_rect().width(), scene_rect().height()) / 12.0;
    painter.setPen(Qt::NoPen);
    for (const auto& bucket : m_heatmap_buckets) {
        if (bucket.layer_name.has_value() && !layer_visibility(*bucket.layer_name)) {
            continue;
        }
        const QColor color = violation_heatmap_color(static_cast<double>(bucket.count) / max_count,
                                                     m_heatmap_settings.opacity);
        painter.setBrush(color);
        painter.drawEllipse(bucket.center, radius, radius);
    }

    painter.restore();
    return m_heatmap_buckets.size();
}

std::size_t LayoutCanvas::paint_violation_overlays(QPainter& painter)
{
    painter.save();
    painter.setTransform(scene_to_widget_transform());

    const double marker_radius = 8.0 / std::max(m_zoom_level, kMinZoom);
    std::size_t painted = 0;

    for (const auto& overlay : m_violation_overlays) {
        if (!overlay.resolved) {
            continue;
        }

        ++painted;
        QColor color = violation_severity_color(overlay.severity);
        painter.setPen(QPen(color, 0.0));
        QColor fill = color;
        fill.setAlpha(48);
        painter.setBrush(fill);

        if (overlay.bounds.has_value()) {
            painter.drawRect(*overlay.bounds);
        }
        if (overlay.point.has_value()) {
            const QPointF point = *overlay.point;
            painter.drawEllipse(point, marker_radius, marker_radius);
            painter.drawLine(QPointF(point.x() - marker_radius, point.y()),
                             QPointF(point.x() + marker_radius, point.y()));
            painter.drawLine(QPointF(point.x(), point.y() - marker_radius),
                             QPointF(point.x(), point.y() + marker_radius));
        }
    }

    painter.restore();
    return painted;
}

void LayoutCanvas::rebuild_heatmap()
{
    m_heatmap_buckets.clear();
    std::map<std::pair<int, int>, std::size_t> bucket_index;
    const QRectF bounds = scene_rect();
    const double bucket_size = std::max(1.0, std::max(bounds.width(), bounds.height()) / 12.0);

    for (const auto& violation : m_violations.violations()) {
        std::optional<QPointF> point;
        std::optional<std::string> layer_name;

        if (violation.location.point.has_value()) {
            point = QPointF(violation.location.point->x, violation.location.point->y);
            layer_name = violation.location.layer;
        } else {
            for (const auto& item : m_scene.items) {
                if (!item_matches_violation_location(item, violation.location) || !item.bounds.valid) {
                    continue;
                }
                point = QPointF((item.bounds.min_x + item.bounds.max_x) * 0.5,
                                (item.bounds.min_y + item.bounds.max_y) * 0.5);
                if (!item.layer_name.empty()) {
                    layer_name = item.layer_name;
                }
                break;
            }
        }

        if (!point.has_value()) {
            continue;
        }

        const int bx = static_cast<int>(std::floor((point->x() - bounds.left()) / bucket_size));
        const int by = static_cast<int>(std::floor((point->y() - bounds.top()) / bucket_size));
        const auto key = std::make_pair(bx, by);
        const auto it = bucket_index.find(key);
        if (it == bucket_index.end()) {
            m_heatmap_buckets.push_back(ViolationHeatmapBucket{
                QPointF(bounds.left() + (bx + 0.5) * bucket_size,
                        bounds.top() + (by + 0.5) * bucket_size),
                1,
                layer_name,
            });
            bucket_index.emplace(key, m_heatmap_buckets.size() - 1);
        } else {
            ++m_heatmap_buckets[it->second].count;
            if (!m_heatmap_buckets[it->second].layer_name.has_value()) {
                m_heatmap_buckets[it->second].layer_name = layer_name;
            }
        }
    }
}

void LayoutCanvas::rebuild_violation_overlays()
{
    m_violation_overlays.clear();
    m_unresolved_violation_count = 0;

    for (const auto& violation : m_violations.violations()) {
        ViolationOverlayItem overlay;
        overlay.violation_id = violation.id;
        overlay.severity = violation.severity;

        QRectF aggregate_bounds;
        bool has_bounds = false;

        if (violation.location.point.has_value()) {
            overlay.point = QPointF(violation.location.point->x, violation.location.point->y);
            overlay.resolved = true;
        }

        for (const auto& item : m_scene.items) {
            if (!item_matches_violation_location(item, violation.location)) {
                continue;
            }
            if (!item.bounds.valid) {
                continue;
            }

            const QRectF rect = bounds_to_rect(item.bounds);
            aggregate_bounds = has_bounds ? aggregate_bounds.united(rect) : rect;
            has_bounds = true;
            overlay.resolved = true;
        }

        if (has_bounds) {
            overlay.bounds = aggregate_bounds;
        }

        if (!overlay.resolved) {
            ++m_unresolved_violation_count;
        }
        m_violation_overlays.push_back(std::move(overlay));
    }
}

void LayoutCanvas::rebuild_lod_cache()
{
    if (m_lod_cache_valid && m_lod_cache_zoom == m_zoom_level &&
        m_lod_cache_center == m_view_center && m_lod_cache_size == size()) {
        return;
    }

    m_lod_cache.clear();
    m_performance_metrics.simplified_item_count = 0;

    const QTransform transform = scene_to_widget_transform();
    const QRectF viewport_rect = rect().adjusted(-1.0, -1.0, 1.0, 1.0);

    for (std::size_t index = 0; index < m_scene.items.size(); ++index) {
        const auto& item = m_scene.items[index];
        if (item.kind != SceneItemKind::Geometry) {
            continue;
        }
        if (!layer_visibility(item.layer_name) || !item.bounds.valid) {
            continue;
        }

        const QRectF widget_bounds = transform.mapRect(bounds_to_rect(item.bounds));
        if (!widget_bounds.intersects(viewport_rect)) {
            continue;
        }

        const double pixel_extent = std::max(std::abs(widget_bounds.width()), std::abs(widget_bounds.height()));
        const bool simplified = pixel_extent < kMinVisiblePixels;
        if (simplified) {
            ++m_performance_metrics.simplified_item_count;
        }
        m_lod_cache.push_back(LodCacheEntry{index, simplified});
    }

    m_lod_cache_zoom = m_zoom_level;
    m_lod_cache_center = m_view_center;
    m_lod_cache_size = size();
    m_lod_cache_valid = true;
}

void LayoutCanvas::invalidate_lod_cache() noexcept
{
    m_lod_cache_valid = false;
}

double LayoutCanvas::hit_test_distance_scene(const SceneItem& item, const QPointF& scene_point) const
{
    constexpr double kPointTolerancePx = 8.0;
    const double point_tolerance_scene = kPointTolerancePx / std::max(m_zoom_level, kMinZoom);

    if (item.shape_kind == SceneShapeKind::Rectangle) {
        const QRectF rect = bounds_to_rect(item.bounds);
        if (rect.adjusted(-point_tolerance_scene, -point_tolerance_scene,
                          point_tolerance_scene, point_tolerance_scene).contains(scene_point)) {
            return point_tolerance_scene * 0.5;
        }
        return std::numeric_limits<double>::max();
    }

    if (item.shape_kind == SceneShapeKind::Polygon) {
        QPolygonF polygon;
        for (const auto& point : item.points) {
            polygon << QPointF(point.x, point.y);
        }
        if (polygon.containsPoint(scene_point, Qt::OddEvenFill)) {
            return point_tolerance_scene * 0.5;
        }
        if (polygon.boundingRect().adjusted(-point_tolerance_scene, -point_tolerance_scene,
                                            point_tolerance_scene, point_tolerance_scene).contains(scene_point)) {
            return point_tolerance_scene;
        }
        return std::numeric_limits<double>::max();
    }

    if (!item.points.empty()) {
        const QPointF point(item.points.front().x, item.points.front().y);
        const double dx = scene_point.x() - point.x();
        const double dy = scene_point.y() - point.y();
        const double distance = std::sqrt(dx * dx + dy * dy);
        return distance <= point_tolerance_scene ? distance : std::numeric_limits<double>::max();
    }

    return std::numeric_limits<double>::max();
}

void LayoutCanvas::notify_viewport_changed()
{
    emit viewport_changed(m_zoom_level, m_view_center);
}

} // namespace aegis::ui
