#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/properties_panel.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/storage/imported_design_session.hpp"
#include "aegis/parsing/layout_ir.hpp"

#include <QApplication>
#include <memory>
#include <filesystem>

using namespace aegis::storage;
using namespace aegis::parsing;
using namespace aegis::ui;
namespace fs = std::filesystem;

namespace {

struct QtAppGuard {
    int argc = 1;
    char arg0[8] = "aegis";
    char* argv[2] = { arg0, nullptr };
    std::unique_ptr<QApplication> app;

    QtAppGuard() {
        if (!QApplication::instance()) {
            app = std::make_unique<QApplication>(argc, argv);
        }
    }
};

LayoutIR make_sample_ir()
{
    LayoutIR ir;
    ir.design_name = "test_design";
    ir.layers.push_back(Layer{"M1", "metal", 0, "#FF0000"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{0.0, 0.0, 10.0, 10.0}});
    ir.ports.push_back(Port{"A", "INPUT", "net_a", std::string{"M1"}, Point{5.0, 5.0}});
    return ir;
}

UiScene build_test_scene()
{
    const auto ir = make_sample_ir();
    UiScene scene = aegis::ui::build_ui_scene(ir);
    // Augment the port item with object_id so it resolves to an imported object
    for (auto& item : scene.items) {
        if (item.kind == aegis::ui::SceneItemKind::Port) {
            item.source_metadata["object_id"] = "layout-1:port:A";
        }
    }
    return scene;
}

ImportedDesignSession make_test_session()
{
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    ImportedDesignSession session = builder.build(package, root);
    return session;
}

} // namespace

TEST_CASE("PropertiesPanel shows empty state when no scene is loaded",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    aegis::ui::PropertiesPanel panel;
    REQUIRE(panel.summary_text().contains("Load a design", Qt::CaseInsensitive));
}

TEST_CASE("PropertiesPanel shows no-selection state when scene exists but nothing is selected",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    aegis::ui::PropertiesPanel panel;
    panel.set_scene(build_test_scene());
    REQUIRE(panel.summary_text().contains("Select layout items", Qt::CaseInsensitive));
}

TEST_CASE("PropertiesPanel shows structured imported object properties for selected item",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    auto session = make_test_session();

    // Use the real imported scene rather than a synthetic one
    const auto imported_scene = aegis::ui::build_imported_design_scene(session);
    const auto& scene = imported_scene.scene;

    aegis::ui::PropertiesPanel panel;
    panel.set_scene(scene);
    panel.set_session(&session);

    // Find a scene item that resolved to an imported object
    const aegis::ui::SceneItem* imported_item = nullptr;
    for (const auto& item : scene.items) {
        if (item.source_metadata.count("object_id") > 0 || item.id.find("layout-1:") != std::string::npos) {
            imported_item = &item;
            break;
        }
    }
    REQUIRE(imported_item != nullptr);
    panel.set_selected_ids({QString::fromStdString(imported_item->id)});

    const QString text = panel.summary_text();
    REQUIRE(text.contains("Identity", Qt::CaseInsensitive));
    REQUIRE(text.contains("Provenance", Qt::CaseInsensitive));
    REQUIRE(text.contains("Scene Properties", Qt::CaseInsensitive));

    // Provenance fields
    REQUIRE(text.contains("Artifact ID", Qt::CaseInsensitive));
    REQUIRE(text.contains("Path", Qt::CaseInsensitive));
    REQUIRE(text.contains("Parser", Qt::CaseInsensitive));

    // Metadata fields (direction, layer, coordinates)
    const bool has_metadata = text.contains("Metadata", Qt::CaseInsensitive) || text.contains("direction", Qt::CaseInsensitive);
    REQUIRE(has_metadata);

    // Scene properties
    REQUIRE(text.contains("Z order", Qt::CaseInsensitive));
}

TEST_CASE("PropertiesPanel shows scene-only properties when no session is bound",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    aegis::ui::PropertiesPanel panel;
    panel.set_scene(build_test_scene());
    // No session bound

    const auto scene = build_test_scene();
    const aegis::ui::SceneItem* geometry_item = nullptr;
    for (const auto& item : scene.items) {
        if (item.kind == aegis::ui::SceneItemKind::Geometry) {
            geometry_item = &item;
            break;
        }
    }
    REQUIRE(geometry_item != nullptr);
    panel.set_selected_ids({QString::fromStdString(geometry_item->id)});

    const QString text = panel.summary_text();
    REQUIRE_FALSE(text.contains("Identity", Qt::CaseInsensitive));
    REQUIRE_FALSE(text.contains("Provenance", Qt::CaseInsensitive));
    REQUIRE(text.contains("Scene Properties", Qt::CaseInsensitive));
    REQUIRE(text.contains("Rectangle", Qt::CaseInsensitive));
}

TEST_CASE("PropertiesPanel handles multi-selection without crashing",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    auto session = make_test_session();
    auto scene = build_test_scene();

    aegis::ui::PropertiesPanel panel;
    panel.set_scene(scene);
    panel.set_session(&session);

    QStringList ids;
    for (const auto& item : scene.items) {
        ids.append(QString::fromStdString(item.id));
    }
    REQUIRE(ids.size() > 1);

    panel.set_selected_ids(ids);
    const QString text = panel.summary_text();
    REQUIRE(text.contains("Scene Properties", Qt::CaseInsensitive));
}

TEST_CASE("PropertiesPanel shows no-properties message for unknown selection",
          "[ui][P13][P13-004][PropertiesPanel]")
{
    QtAppGuard guard;
    aegis::ui::PropertiesPanel panel;
    panel.set_scene(build_test_scene());
    panel.set_session(nullptr);

    panel.set_selected_ids({"unknown_id_xyz"});
    REQUIRE(panel.summary_text().contains("No properties available", Qt::CaseInsensitive));
}
