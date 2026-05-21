#pragma once

#include "main_window_state.hpp"

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/storage/imported_design_session.hpp"
#include "aegis/storage/project_package.hpp"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantMap>
#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace aegis::ui {

inline QString normalize_saved_name(const QString& name)
{
    return name.trimmed();
}

inline bool saved_name_matches(const QString& lhs, const QString& rhs)
{
    return normalize_saved_name(lhs).compare(normalize_saved_name(rhs), Qt::CaseInsensitive) == 0;
}

inline QVariantMap to_variant_map(const SavedFilterPreset& preset)
{
    QVariantMap map;
    map.insert("name", preset.name);
    map.insert("state", preset.state.to_variant_map());
    return map;
}

inline std::optional<SavedFilterPreset> saved_filter_preset_from_variant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    const QString name = normalize_saved_name(map.value("name").toString());
    if (name.isEmpty()) {
        return std::nullopt;
    }

    SavedFilterPreset preset;
    preset.name = name;
    preset.state = ViolationFilterState::from_variant_map(map.value("state").toMap());
    return preset;
}

inline QVariantMap to_variant_map(const SavedWorkspaceView& view)
{
    QVariantMap map;
    map.insert("name", view.name);
    map.insert("dockState", view.dock_state);
    map.insert("gridVisible", view.grid_visible);
    map.insert("overlaysVisible", view.overlays_visible);
    map.insert("heatmapVisible", view.heatmap_visible);
    map.insert("heatmapOpacity", view.heatmap_opacity);
    map.insert("performanceMetricsVisible", view.performance_metrics_visible);
    return map;
}

inline std::optional<SavedWorkspaceView> saved_workspace_view_from_variant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    const QString name = normalize_saved_name(map.value("name").toString());
    if (name.isEmpty()) {
        return std::nullopt;
    }

    SavedWorkspaceView view;
    view.name = name;
    view.dock_state = map.value("dockState").toByteArray();
    view.grid_visible = map.value("gridVisible", true).toBool();
    view.overlays_visible = map.value("overlaysVisible", true).toBool();
    view.heatmap_visible = map.value("heatmapVisible", false).toBool();
    view.heatmap_opacity = map.value("heatmapOpacity", 0.6).toDouble();
        view.performance_metrics_visible = map.value("performanceMetricsVisible", false).toBool();
    return view;
}

inline QVariantMap to_variant_map(const SavedViewportPreset& preset)
{
    QVariantMap map;
    map.insert("name", preset.name);
    map.insert("zoomLevel", preset.zoom_level);
    map.insert("viewCenterX", preset.view_center_x);
    map.insert("viewCenterY", preset.view_center_y);
    return map;
}

inline std::optional<SavedViewportPreset> saved_viewport_preset_from_variant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    const QString name = normalize_saved_name(map.value("name").toString());
    if (name.isEmpty()) {
        return std::nullopt;
    }

    SavedViewportPreset preset;
    preset.name = name;
    preset.zoom_level = map.value("zoomLevel", 1.0).toDouble();
    preset.view_center_x = map.value("viewCenterX", 0.0).toDouble();
    preset.view_center_y = map.value("viewCenterY", 0.0).toDouble();
    return preset;
}

template <typename Entry>
inline QStringList saved_entry_names(const std::vector<Entry>& entries)
{
    QStringList names;
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    return names;
}

struct BundledSampleInfo {
    QString id;
    QString file_name;
    QString display_name;
    QString description;
};

inline const std::array<BundledSampleInfo, 3>& bundled_samples()
{
    static const std::array<BundledSampleInfo, 3> samples{{
        {"inverter", "inverter.json", "Inverter", "Minimal inverter sample"},
        {"nand2", "nand2.json", "NAND2", "Two-input NAND gate sample"},
        {"ring_oscillator", "ring_oscillator.json", "Ring Oscillator", "Five-stage ring oscillator sample"},
    }};
    return samples;
}

inline const BundledSampleInfo* bundled_sample_by_id(const QString& sample_id)
{
    const auto& samples = bundled_samples();
    const auto it = std::find_if(samples.begin(), samples.end(), [&sample_id](const BundledSampleInfo& sample) {
        return sample.id.compare(sample_id.trimmed(), Qt::CaseInsensitive) == 0;
    });
    return it != samples.end() ? &(*it) : nullptr;
}

inline std::filesystem::path sample_design_path(const char* name)
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "data" / "sample_designs" / name;
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("data") / "sample_designs" / name;
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "data" / "sample_designs" / name;
}

inline std::filesystem::path project_readme_path()
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "README.md";
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("README.md");
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "README.md";
}

inline std::filesystem::path docs_directory_path()
{
#ifdef AEGIS_SOURCE_DIR
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "docs";
    if (std::filesystem::exists(rooted)) {
        return rooted;
    }
#endif

    const auto candidate = std::filesystem::path("docs");
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }

    return std::filesystem::path("..") / ".." / "docs";
}

inline std::optional<aegis::parsing::LayoutIR> load_sample_design_ir(const char* name)
{
    std::ifstream input(sample_design_path(name), std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    nlohmann::json j;
    input >> j;

    aegis::parsing::LayoutIR ir;
    ir.version = aegis::parsing::LayoutIR::CURRENT_VERSION;
    ir.design_name = j.at("design").at("name").get<std::string>();
    ir.description = j.at("design").at("description").get<std::string>();

    static const std::vector<std::string> colors{"#6AA84F", "#CC0000", "#3C78D8", "#F1C232"};
    const auto& layers = j.at("layout").at("layers");
    for (std::size_t i = 0; i < layers.size(); ++i) {
        ir.layers.push_back(aegis::parsing::Layer{
            layers.at(i).get<std::string>(),
            "sample",
            static_cast<int>(i),
            colors.at(i % colors.size())
        });
    }

    for (const auto& shape : j.at("layout").at("shapes")) {
        if (!shape.contains("bbox") || shape.at("bbox").size() != 4) {
            continue;
        }
        const auto& bbox = shape.at("bbox");
        const double x1 = bbox.at(0).get<double>();
        const double y1 = bbox.at(1).get<double>();
        const double x2 = bbox.at(2).get<double>();
        const double y2 = bbox.at(3).get<double>();
        ir.geometries.push_back(aegis::parsing::Geometry{
            shape.at("layer").get<std::string>(),
            aegis::parsing::Rectangle{x1, y1, x2 - x1, y2 - y1}
        });
    }

    for (const auto& net : j.at("netlist").at("nets")) {
        aegis::parsing::Net ir_net;
        ir_net.name = net.at("name").get<std::string>();
        if (net.contains("type")) {
            ir_net.properties["type"] = net.at("type").get<std::string>();
        }
        if (net.contains("connections")) {
            for (const auto& connection : net.at("connections")) {
                const std::string device = connection.at("device").get<std::string>();
                const std::string pin = connection.at("pin").get<std::string>();
                ir_net.pin_names.push_back(device + "." + pin);
            }
        }
        ir.nets.push_back(std::move(ir_net));
    }

    for (const auto& device : j.at("netlist").at("devices")) {
        aegis::parsing::Device ir_device;
        ir_device.name = device.at("name").get<std::string>();
        ir_device.type = device.at("type").get<std::string>();
        if (device.contains("pins")) {
            for (auto it = device.at("pins").begin(); it != device.at("pins").end(); ++it) {
                ir_device.pins[it.key()] = it.value().get<std::string>();
            }
        }
        if (device.contains("properties")) {
            for (auto it = device.at("properties").begin(); it != device.at("properties").end(); ++it) {
                ir_device.properties[it.key()] = it.value().dump();
            }
        }
        ir.devices.push_back(std::move(ir_device));
    }

    for (const auto& port : j.at("netlist").at("ports")) {
        std::string direction = port.at("direction").get<std::string>();
        std::transform(direction.begin(), direction.end(), direction.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        ir.ports.push_back(aegis::parsing::Port{
            port.at("name").get<std::string>(),
            direction,
            port.at("net").get<std::string>(),
            std::nullopt,
            std::nullopt
        });
    }

    return ir;
}

inline void configure_action(QAction* action, const QString& tooltip, const QString& object_name = {})
{
    if (action == nullptr) {
        return;
    }
    action->setToolTip(tooltip);
    action->setStatusTip(tooltip);
    action->setShortcutVisibleInContextMenu(true);
    if (!object_name.trimmed().isEmpty()) {
        action->setObjectName(object_name);
    }
}

template <typename Widget>
inline Widget* configure_accessible_widget(Widget* widget,
                                    const QString& accessible_name,
                                    const QString& tooltip = {},
                                    const QString& accessible_description = {})
{
    if (widget == nullptr) {
        return nullptr;
    }
    widget->setAccessibleName(accessible_name);
    if (!accessible_description.trimmed().isEmpty()) {
        widget->setAccessibleDescription(accessible_description);
    }
    if (!tooltip.trimmed().isEmpty()) {
        widget->setToolTip(tooltip);
        widget->setStatusTip(tooltip);
    }
    return widget;
}

inline QString import_summary_text(const aegis::storage::ProjectPackage& package)
{
    QStringList lines;
    lines.append(QString("Project: %1").arg(QString::fromStdString(package.project().name)));
    lines.append(QString("Validation: %1").arg(QString::fromStdString(aegis::storage::to_string(package.validation_status()))));
    lines.append(QString("Artifacts: %1").arg(package.artifacts().size()));
    lines.append(QString("Diagnostics: %1").arg(package.diagnostics().size()));
    lines.append(QString{});
    lines.append("Detected artifacts:");
    for (const auto& artifact : package.artifacts()) {
        lines.append(QString("- %1 [%2/%3]%4")
                         .arg(QString::fromStdString(artifact.path.generic_string()))
                         .arg(QString::fromStdString(aegis::storage::to_string(artifact.category)))
                         .arg(QString::fromStdString(aegis::storage::to_string(artifact.role)))
                         .arg(artifact.optional ? " optional" : " required-candidate"));
    }
    if (package.artifacts().empty()) {
        lines.append("- No recognized artifacts");
    }
    lines.append(QString{});
    lines.append("Diagnostics:");
    for (const auto& diagnostic : package.diagnostics()) {
        lines.append(QString("- [%1] %2")
                         .arg(QString::fromStdString(aegis::storage::to_string(diagnostic.severity)))
                         .arg(QString::fromStdString(diagnostic.message)));
    }
    if (package.diagnostics().empty()) {
        lines.append("- No diagnostics");
    }
    return lines.join('\n');
}

inline QString imported_design_session_summary_text(const aegis::storage::ImportedDesignSession* session)
{
    if (session == nullptr) {
        return QString("Imported design session: unavailable");
    }

    return QString("Imported design session\n"
                   "Status: %1\n"
                   "Technology libraries: %2\n"
                   "Design layers: %3\n"
                   "Instances: %4\n"
                   "Ports: %5\n"
                   "Nets: %6\n"
                   "Devices: %7\n"
                   "Rule packs: %8\n"
                   "Graph nodes: %9\n"
                   "Session diagnostics: %10")
        .arg(QString::fromStdString(aegis::storage::to_string(session->status())))
        .arg(session->technology_libraries().size())
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Layer))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Instance))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Port))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Net))
        .arg(session->object_count(aegis::storage::ImportedDesignObjectKind::Device))
        .arg(session->rule_artifact_ids().size())
        .arg(session->graph().node_count())
        .arg(session->diagnostics().size());
}

inline QStringList choose_import_paths(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Import Design Package");
    dialog.setModal(true);
    dialog.resize(420, 160);

    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel("Choose a customer project folder or select individual design-package files.", &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* folder_button = buttons->addButton("Choose Project Folder...", QDialogButtonBox::ActionRole);
    auto* files_button = buttons->addButton("Choose Files...", QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    QStringList selected_paths;
    QObject::connect(folder_button, &QPushButton::clicked, &dialog, [&dialog, parent, &selected_paths]() {
        const QString folder = QFileDialog::getExistingDirectory(parent,
                                                                 "Select Project Folder",
                                                                 QString{},
                                                                 QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (folder.trimmed().isEmpty()) {
            return;
        }
        selected_paths = {folder};
        dialog.accept();
    });
    QObject::connect(files_button, &QPushButton::clicked, &dialog, [&dialog, parent, &selected_paths]() {
        const QStringList files = QFileDialog::getOpenFileNames(parent,
                                                                "Select Design Package Files",
                                                                QString{},
                                                                "All Files (*.*)");
        if (files.isEmpty()) {
            return;
        }
        selected_paths = files;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return selected_paths;
}

inline QString artifact_requirement_text(const aegis::storage::SourceArtifact& artifact)
{
    return artifact.optional ? "Optional" : "Required candidate";
}

inline QString artifact_status_text(const aegis::storage::ProjectPackage& package,
                             const aegis::storage::SourceArtifact& artifact)
{
    bool has_warning = false;
    int related_count = 0;
    for (const auto& diagnostic : package.diagnostics()) {
        if (!diagnostic.artifact_id.has_value() || *diagnostic.artifact_id != artifact.id) {
            continue;
        }
        ++related_count;
        if (diagnostic.severity == aegis::storage::DiagnosticSeverity::Error) {
            return QString("error (%1)").arg(related_count);
        }
        if (diagnostic.severity == aegis::storage::DiagnosticSeverity::Warning) {
            has_warning = true;
        }
    }
    if (has_warning) {
        return QString("warning (%1)").arg(related_count);
    }

    const bool package_missing_required_inputs = std::any_of(package.diagnostics().begin(), package.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.code == "MISSING_REQUIRED_TECHNOLOGY" ||
               diagnostic.code == "MISSING_REQUIRED_LAYOUT" ||
               diagnostic.code == "MISSING_REQUIRED_NETLIST" ||
               diagnostic.code == "MISSING_REQUIRED_RULES";
    });
    if (package_missing_required_inputs
        && (artifact.category == aegis::storage::ArtifactCategory::ExternalReports
            || artifact.category == aegis::storage::ArtifactCategory::Unknown)) {
        return "warning (review role)";
    }

    return related_count > 0 ? QString("info (%1)").arg(related_count) : QString("ok");
}

inline QString import_diagnostics_text(const aegis::storage::ProjectPackage& package)
{
    QStringList lines;
    for (const auto& diagnostic : package.diagnostics()) {
        lines.append(QString("- [%1] %2")
                         .arg(QString::fromStdString(aegis::storage::to_string(diagnostic.severity)))
                         .arg(QString::fromStdString(diagnostic.message)));
    }
    if (lines.isEmpty()) {
        lines.append("- No diagnostics");
    }
    return lines.join('\n');
}

inline std::vector<aegis::storage::ArtifactRole> supported_import_roles()
{
    using aegis::storage::ArtifactRole;
    return {
        ArtifactRole::Lef,
        ArtifactRole::Def,
        ArtifactRole::Verilog,
        ArtifactRole::SystemVerilog,
        ArtifactRole::Spice,
        ArtifactRole::Spi,
        ArtifactRole::Cdl,
        ArtifactRole::AegisRulePack,
        ArtifactRole::PowerDomainsCsv,
        ArtifactRole::CurrentCsv,
        ArtifactRole::WaiverCsv,
        ArtifactRole::WaiverYaml,
        ArtifactRole::WaiverJson,
        ArtifactRole::ImportedReport,
        ArtifactRole::Unknown,
    };
}

} // namespace aegis::ui
