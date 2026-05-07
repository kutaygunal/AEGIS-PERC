#include "aegis/ui/layout_canvas.hpp"
#include "aegis/ui/selection_model.hpp"

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

void LayoutCanvas::set_layer_visibility(const std::string& layer_name, bool visible)
{
    if (layer_name.empty()) {
        return;
    }
    m_layer_visibility[layer_name] = visible;
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
    update();
}

void LayoutCanvas::hide_all_layers()
{
    for (auto& [_, visible] : m_layer_visibility) {
        visible = false;
    }
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
    notify_viewport_changed();
    update();
}

void LayoutCanvas::reset_view()
{
    m_zoom_level = 1.0;
    m_view_center = scene_rect().center();
    m_view_initialized = true;
    notify_viewport_changed();
    update();
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

void LayoutCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    ensure_view_initialized();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), m_background_color);

    if (m_grid_visible) {
        paint_grid(painter);
    }

    if (!has_scene() || !m_scene.bounds.valid) {
        painter.setPen(QColor("#AAB2BF"));
        painter.drawText(rect(), Qt::AlignCenter, "No layout scene loaded");
        return;
    }

    paint_scene(painter);
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

void LayoutCanvas::paint_scene(QPainter& painter)
{
    painter.save();
    painter.setTransform(scene_to_widget_transform());

    for (const auto& item : m_scene.items) {
        if (item.kind != SceneItemKind::Geometry) {
            continue;
        }
        if (!layer_visibility(item.layer_name)) {
            continue;
        }

        QColor color = color_from_scene(item.color);
        QColor fill = color;
        fill.setAlpha(96);
        QColor stroke = color.lighter(135);
        stroke.setAlpha(220);
        const bool selected = m_selection_model != nullptr && m_selection_model->contains(item.id);
        if (selected) {
            stroke = QColor("#FFD54F");
            fill = stroke;
            fill.setAlpha(120);
        }

        painter.setPen(QPen(stroke, selected ? 0.0 : 0.0));
        painter.setBrush(fill);

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
    painter.drawText(12, 20, QString("%1 items | %2%")
                                .arg(m_scene.items.size())
                                .arg(QString::number(m_zoom_level * 100.0, 'f', 1)));
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
