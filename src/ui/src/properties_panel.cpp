#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace aegis::ui {

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_summary = new QLabel(state_text::properties_no_scene(), this);
    m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_summary);
}

void PropertiesPanel::set_scene(const UiScene& scene)
{
    m_scene = scene;
    update_summary({});
}

void PropertiesPanel::set_selected_ids(const QStringList& ids)
{
    update_summary(ids);
}

QString PropertiesPanel::summary_text() const
{
    return m_summary->text();
}

void PropertiesPanel::update_summary(const QStringList& ids)
{
    if (ids.isEmpty()) {
        m_summary->setText(m_scene.items.empty() ? state_text::properties_no_scene()
                                                 : state_text::properties_no_selection());
        return;
    }

    QStringList lines;
    lines.append(QString("Selected: %1").arg(ids.size()));

    for (const auto& id : ids) {
        const auto* item = m_scene.find_item_by_id(id.toStdString());
        if (item == nullptr) {
            lines.append(QString("- %1").arg(id));
            continue;
        }

        QString desc = QString("- %1 | layer=%2")
                           .arg(id)
                           .arg(QString::fromStdString(item->layer_name));
        const auto name_it = item->source_metadata.find("name");
        if (name_it != item->source_metadata.end()) {
            desc += QString(" | name=%1").arg(QString::fromStdString(name_it->second));
        }
        const auto key_it = item->source_metadata.find("key");
        if (key_it != item->source_metadata.end()) {
            desc += QString(" | key=%1").arg(QString::fromStdString(key_it->second));
        }
        lines.append(desc);
    }

    m_summary->setText(lines.join('\n'));
}

} // namespace aegis::ui
