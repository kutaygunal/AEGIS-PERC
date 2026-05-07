#include "aegis/ui/layer_panel.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace aegis::ui {
namespace {
constexpr auto kLayerNameRole = Qt::UserRole + 1;
constexpr auto kLayerColorRole = Qt::UserRole + 2;
}

LayerPanel::LayerPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list);

    auto* buttons = new QHBoxLayout();
    m_show_all_button = new QPushButton("Show All", this);
    m_hide_all_button = new QPushButton("Hide All", this);
    m_isolate_button = new QPushButton("Isolate", this);
    buttons->addWidget(m_show_all_button);
    buttons->addWidget(m_hide_all_button);
    buttons->addWidget(m_isolate_button);
    layout->addLayout(buttons);

    connect(m_show_all_button, &QPushButton::clicked, this, &LayerPanel::show_all_layers);
    connect(m_hide_all_button, &QPushButton::clicked, this, &LayerPanel::hide_all_layers);
    connect(m_isolate_button, &QPushButton::clicked, this, [this]() {
        if (auto* item = m_list->currentItem()) {
            isolate_layer(item->data(kLayerNameRole).toString());
        }
    });

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating || item == nullptr) {
            return;
        }
        update_item_presentation(item);
        emit layer_visibility_changed(item->data(kLayerNameRole).toString(),
                                      item->checkState() == Qt::Checked);
    });
}

void LayerPanel::set_layers(const std::vector<SceneLayer>& layers)
{
    m_updating = true;
    m_list->clear();

    for (const auto& layer : layers) {
        auto* item = new QListWidgetItem(QString::fromStdString(layer.name), m_list);
        item->setData(kLayerNameRole, QString::fromStdString(layer.name));
        item->setData(kLayerColorRole, QString::fromStdString(layer.color));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Checked);
        update_item_presentation(item);
    }

    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    m_updating = false;
}

int LayerPanel::layer_count() const noexcept
{
    return m_list->count();
}

QStringList LayerPanel::layer_names() const
{
    QStringList names;
    for (int i = 0; i < m_list->count(); ++i) {
        names.append(m_list->item(i)->data(kLayerNameRole).toString());
    }
    return names;
}

bool LayerPanel::layer_visible(const QString& layer_name) const
{
    if (auto* item = find_item(layer_name)) {
        return item->checkState() == Qt::Checked;
    }
    return false;
}

void LayerPanel::set_layer_visible(const QString& layer_name, bool visible)
{
    if (auto* item = find_item(layer_name)) {
        const Qt::CheckState state = visible ? Qt::Checked : Qt::Unchecked;
        if (item->checkState() != state) {
            m_updating = true;
            item->setCheckState(state);
            update_item_presentation(item);
            m_updating = false;
            emit layer_visibility_changed(layer_name, visible);
        }
    }
}

void LayerPanel::show_all_layers()
{
    for (int i = 0; i < m_list->count(); ++i) {
        set_layer_visible(m_list->item(i)->data(kLayerNameRole).toString(), true);
    }
}

void LayerPanel::hide_all_layers()
{
    for (int i = 0; i < m_list->count(); ++i) {
        set_layer_visible(m_list->item(i)->data(kLayerNameRole).toString(), false);
    }
}

void LayerPanel::isolate_layer(const QString& layer_name)
{
    for (int i = 0; i < m_list->count(); ++i) {
        const QString name = m_list->item(i)->data(kLayerNameRole).toString();
        set_layer_visible(name, name == layer_name);
    }
}

void LayerPanel::update_item_presentation(QListWidgetItem* item)
{
    const QColor swatch(item->data(kLayerColorRole).toString());
    const QColor safe = swatch.isValid() ? swatch : QColor("#808080");
    item->setForeground(safe);
}

QListWidgetItem* LayerPanel::find_item(const QString& layer_name) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        if (item->data(kLayerNameRole).toString() == layer_name) {
            return item;
        }
    }
    return nullptr;
}

} // namespace aegis::ui
