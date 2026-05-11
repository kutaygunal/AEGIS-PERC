#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/ui_state_text.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include <QApplication>

namespace aegis::ui {
namespace {

QString escape_html(const QString& text)
{
    QString out = text;
    out.replace("\u0026", "\u0026amp;");
    out.replace("<", "\u0026lt;");
    out.replace(">", "\u0026gt;");
    return out;
}

QString escape_html(const std::string& text)
{
    return escape_html(QString::fromStdString(text));
}

QString format_row(const QString& key, const QString& value)
{
    return QString("<tr><td style='white-space:nowrap;padding-right:12px;'><b>%1</b></td><td>%2</td></tr>")
        .arg(escape_html(key))
        .arg(escape_html(value));
}

QString format_section(const QString& title, const QString& rows)
{
    if (rows.isEmpty()) {
        return QString{};
    }
    return QString(
        "<p style='margin:8px 0 4px 0;color:#555;'><b>%1</b></p>"
        "<table style='border-collapse:collapse;'>%2</table>")
        .arg(escape_html(title))
        .arg(rows);
}

QString provenance_rows(const aegis::storage::SourceProvenance& p)
{
    QString rows;
    rows += format_row("Artifact ID", QString::fromStdString(p.artifact_id));
    rows += format_row("Path", QString::fromStdString(p.artifact_path.generic_string()));
    rows += format_row("Role", QString::fromStdString(aegis::storage::to_string(p.role)));
    rows += format_row("Origin", QString::fromStdString(p.origin));
    rows += format_row("Parser", QString::fromStdString(p.parser_name));
    if (p.source_line.has_value()) {
        rows += format_row("Source line", QString::number(static_cast<qint64>(*p.source_line)));
    }
    if (p.source_row.has_value()) {
        rows += format_row("Source row", QString::number(static_cast<qint64>(*p.source_row)));
    }
    return rows;
}

QString metadata_rows(const std::map<std::string, std::string>& metadata)
{
    if (metadata.empty()) {
        return QString{};
    }
    QString rows;
    for (const auto& [key, value] : metadata) {
        rows += format_row(QString::fromStdString(key), QString::fromStdString(value));
    }
    return rows;
}

QString object_kind_display_name(aegis::storage::ImportedDesignObjectKind kind)
{
    using aegis::storage::ImportedDesignObjectKind;
    switch (kind) {
    case ImportedDesignObjectKind::Instance: return "Instance";
    case ImportedDesignObjectKind::Net: return "Net";
    case ImportedDesignObjectKind::Port: return "Port";
    case ImportedDesignObjectKind::Layer: return "Layer";
    case ImportedDesignObjectKind::TechnologyMacro: return "Technology Macro";
    case ImportedDesignObjectKind::Device: return "Device";
    }
    return "Unknown";
}

QString format_scene_item(const aegis::ui::SceneItem* item)
{
    if (item == nullptr) {
        return QString{};
    }

    QString rows;
    rows += format_row("Scene ID", QString::fromStdString(item->id));
    rows += format_row("Kind", [&]() {
        switch (item->kind) {
        case SceneItemKind::Geometry: return "Geometry";
        case SceneItemKind::Port: return "Port";
        case SceneItemKind::Annotation: return "Annotation";
        }
        return "Unknown";
    }());
    rows += format_row("Layer", QString::fromStdString(item->layer_name));
    rows += format_row("Shape", [&]() {
        switch (item->shape_kind) {
        case SceneShapeKind::Rectangle: return "Rectangle";
        case SceneShapeKind::Polygon: return "Polygon";
        case SceneShapeKind::Point: return "Point";
        }
        return "Unknown";
    }());
    rows += format_row("Z order", QString::number(item->z_order));
    if (item->bounds.valid) {
        rows += format_row("Bounds",
                           QString("[%1, %2] – [%3, %4]")
                               .arg(item->bounds.min_x, 0, 'g', 6)
                               .arg(item->bounds.min_y, 0, 'g', 6)
                               .arg(item->bounds.max_x, 0, 'g', 6)
                               .arg(item->bounds.max_y, 0, 'g', 6));
    }
    if (item->points.size() >= 4) {
        rows += format_row("Points", QString::number(static_cast<qint64>(item->points.size())));
    }
    return format_section("Scene Properties", rows);
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_summary = new QLabel(state_text::properties_no_scene(), this);
    m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(m_summary);
}

void PropertiesPanel::set_scene(const UiScene& scene)
{
    m_scene = scene;
    update_summary({});
}

void PropertiesPanel::set_session(const aegis::storage::ImportedDesignSession* session)
{
    m_session = session;
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

    QStringList parts;

    for (const auto& qid : ids) {
        const std::string id = qid.toStdString();
        const auto* scene_item = m_scene.find_item_by_id(id);
        const aegis::storage::ImportedDesignObject* imported_object = nullptr;

        if (m_session != nullptr && scene_item != nullptr) {
            const auto obj_it = scene_item->source_metadata.find("object_id");
            if (obj_it != scene_item->source_metadata.end()) {
                imported_object = m_session->find_object_by_stable_id(obj_it->second);
            }
        }
        if (m_session != nullptr && imported_object == nullptr) {
            imported_object = m_session->find_object_by_stable_id(id);
        }

        parts.append(QString("<p style='margin:0 0 6px 0;color:#222;font-size:13px;'><b>%1</b></p>")
                         .arg(escape_html(qid)));

        if (imported_object != nullptr) {
            QString identity_rows;
            identity_rows += format_row("Name", QString::fromStdString(imported_object->display_name.empty()
                                                                                    ? imported_object->name
                                                                                    : imported_object->display_name));
            identity_rows += format_row("Kind", object_kind_display_name(imported_object->kind));
            identity_rows += format_row("Stable ID", QString::fromStdString(imported_object->stable_id));
            parts.append(format_section("Identity", identity_rows));

            parts.append(format_section("Provenance", provenance_rows(imported_object->provenance)));

            QString meta_rows = metadata_rows(imported_object->metadata);
            if (!meta_rows.isEmpty()) {
                parts.append(format_section("Metadata", meta_rows));
            }
        }

        const auto* item = scene_item;
        if (item == nullptr) {
            item = m_scene.find_item_by_id(id);
        }
        if (item != nullptr) {
            parts.append(format_scene_item(item));
        }

        if (imported_object == nullptr && item == nullptr) {
            parts.append(QString("<p style='color:#888;margin:4px 0;'>No properties available.</p>"));
        }
    }

    m_summary->setText(parts.join("<hr style='border:none;border-top:1px solid #ccc;margin:6px 0;'/>"));
}

} // namespace aegis::ui
