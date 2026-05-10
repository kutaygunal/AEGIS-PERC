#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/scene_adapter.hpp"
#include "aegis/storage/imported_design_session.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

using namespace aegis::parsing;
using namespace aegis::ui;

namespace {

std::string sample_path(const std::string& name)
{
    return std::string{AEGIS_SOURCE_DIR} + "/data/sample_designs/" + name + ".json";
}

LayoutIR sample_to_ir(const std::string& name)
{
    std::ifstream in(sample_path(name));
    REQUIRE(in.good());

    nlohmann::json j;
    in >> j;

    LayoutIR ir;
    ir.version = LayoutIR::CURRENT_VERSION;
    ir.design_name = j.at("design").at("name").get<std::string>();
    ir.description = j.at("design").at("description").get<std::string>();

    const std::vector<std::string> colors{"#6AA84F", "#CC0000", "#3C78D8", "#F1C232"};
    const auto& layers = j.at("layout").at("layers");
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto layer_name = layers.at(i).get<std::string>();
        ir.layers.push_back(Layer{layer_name, "sample", static_cast<int>(i), colors.at(i % colors.size())});
    }

    for (const auto& shape : j.at("layout").at("shapes")) {
        const auto& bbox = shape.at("bbox");
        const double x1 = bbox.at(0).get<double>();
        const double y1 = bbox.at(1).get<double>();
        const double x2 = bbox.at(2).get<double>();
        const double y2 = bbox.at(3).get<double>();
        ir.geometries.push_back(Geometry{
            shape.at("layer").get<std::string>(),
            Rectangle{x1, y1, x2 - x1, y2 - y1},
        });
    }

    for (const auto& port : j.at("netlist").at("ports")) {
        ir.ports.push_back(Port{
            port.at("name").get<std::string>(),
            port.at("direction").get<std::string>(),
            port.at("net").get<std::string>(),
            std::nullopt,
            std::nullopt,
        });
    }

    return ir;
}

LayoutIR make_direct_ir()
{
    LayoutIR ir;
    ir.design_name = "direct";
    ir.description = "direct adapter test";
    ir.layers.push_back(Layer{"M1", "metal", 2, "#111111"});
    ir.layers.push_back(Layer{"POLY", "gate", 1, "#222222"});
    ir.geometries.push_back(Geometry{"M1", Rectangle{10.0, 20.0, 30.0, 40.0}});
    ir.geometries.push_back(Geometry{"POLY", Polygon{{{0.0, 0.0}, {5.0, 10.0}, {-5.0, 10.0}}}});
    ir.ports.push_back(Port{"A", "INPUT", "net_a", std::string{"M1"}, Point{12.0, 22.0}});
    ir.annotations.push_back(Annotation{"note", "hello", std::string{"POLY"}, Point{-2.0, 3.0}});
    return ir;
}


std::filesystem::path repo_import_package_dir()
{
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "data" / "import_packages" / "openframe_simple_design";
    return rooted;
}

} // namespace

TEST_CASE("UiScene adapter converts LayoutIR geometry, ports, and annotations", "[ui][P3-001][UiSceneAdapter]")
{
    const UiScene scene = build_ui_scene(make_direct_ir());

    REQUIRE(scene.design_name == "direct");
    REQUIRE(scene.layers.size() == 2);
    REQUIRE(scene.items.size() == 4);
    REQUIRE(scene.bounds.valid);
    REQUIRE(scene.bounds.min_x == -5.0);
    REQUIRE(scene.bounds.min_y == 0.0);
    REQUIRE(scene.bounds.max_x == 40.0);
    REQUIRE(scene.bounds.max_y == 60.0);

    const auto geometry_items = scene.items_by_kind(SceneItemKind::Geometry);
    const auto port_items = scene.items_by_kind(SceneItemKind::Port);
    const auto annotation_items = scene.items_by_kind(SceneItemKind::Annotation);
    REQUIRE(geometry_items.size() == 2);
    REQUIRE(port_items.size() == 1);
    REQUIRE(annotation_items.size() == 1);

    const SceneItem* rect = scene.find_item_by_id("geometry:0");
    REQUIRE(rect != nullptr);
    REQUIRE(rect->shape_kind == SceneShapeKind::Rectangle);
    REQUIRE(rect->layer_name == "M1");
    REQUIRE(rect->color == "#111111");
    REQUIRE(rect->z_order == 2);
    REQUIRE(rect->bounds.width() == 30.0);
    REQUIRE(rect->bounds.height() == 40.0);
    REQUIRE(rect->source_metadata.at("source") == "LayoutIR.geometries");
    REQUIRE(rect->source_metadata.at("source_index") == "0");

    const SceneItem* port = scene.find_item_by_id("port:0");
    REQUIRE(port != nullptr);
    REQUIRE(port->source_metadata.at("name") == "A");
    REQUIRE(port->source_metadata.at("net_name") == "net_a");
    REQUIRE(port->points.size() == 1);
}

TEST_CASE("UiScene adapter covers inverter sample design", "[ui][P3-001][UiSceneAdapter]")
{
    const LayoutIR ir = sample_to_ir("inverter");
    const UiScene scene = build_ui_scene(ir);

    REQUIRE(scene.design_name == "inverter");
    REQUIRE(scene.layers.size() == 4);
    REQUIRE(scene.items_by_kind(SceneItemKind::Geometry).size() == 8);
    REQUIRE(scene.items_by_kind(SceneItemKind::Port).size() == ir.ports.size());
    REQUIRE(scene.bounds.valid);
    REQUIRE(scene.bounds.min_x == 0.0);
    REQUIRE(scene.bounds.min_y == 0.0);
    REQUIRE(scene.bounds.max_x == 170.0);
    REQUIRE(scene.bounds.max_y == 210.0);
}

TEST_CASE("UiScene adapter covers nand2 sample design", "[ui][P3-001][UiSceneAdapter]")
{
    const LayoutIR ir = sample_to_ir("nand2");
    const UiScene scene = build_ui_scene(ir);

    REQUIRE(scene.design_name == "nand2");
    REQUIRE(scene.layers.size() == 4);
    REQUIRE(scene.items_by_kind(SceneItemKind::Geometry).size() == 9);
    REQUIRE(scene.items_by_kind(SceneItemKind::Port).size() == ir.ports.size());
    REQUIRE(scene.bounds.valid);
    REQUIRE(scene.bounds.min_x == 0.0);
    REQUIRE(scene.bounds.min_y == 0.0);
    REQUIRE(scene.bounds.max_x == 290.0);
    REQUIRE(scene.bounds.max_y == 300.0);
}

TEST_CASE("UiScene adapter materializes imported design session into a commercial 2D scene", "[ui][P13][P13-002][UiSceneAdapter]")
{
    using namespace aegis::storage;

    const auto root = repo_import_package_dir();
    REQUIRE(std::filesystem::exists(root / "layout" / "technology.lef"));

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.project().description = "Curated import-package sample derived from openframe_simple_example/simple_design";
    package.project().customer = "OpenFrame Example";
    package.project().design_stage = "place_route";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.artifacts().push_back({"rules-1", "rules/aegis_rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    package.artifacts().push_back({"power-1", "power/power_domains.csv", ArtifactRole::PowerDomainsCsv, ArtifactCategory::Power, true, "manifest"});
    package.artifacts().push_back({"current-1", "reports/current_report.csv", ArtifactRole::CurrentCsv, ArtifactCategory::Current, true, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);
    const auto scene_result = build_imported_design_scene(session);

    REQUIRE(scene_result.scene.design_name == "OpenFrameSimpleDesign");
    REQUIRE(scene_result.scene.bounds.valid);
    REQUIRE(scene_result.scene.items.size() > session.physical_ir().geometries.size());
    REQUIRE(scene_result.scene.items_by_kind(SceneItemKind::Geometry).size() > 0);
    REQUIRE(scene_result.scene.items_by_kind(SceneItemKind::Port).size() == session.physical_ir().ports.size());
    REQUIRE(scene_result.scene.layers.size() >= session.physical_ir().layers.size());
    REQUIRE_FALSE(scene_result.diagnostics.empty());

    bool saw_instance = false;
    bool saw_stable_port = false;
    for (const auto& item : scene_result.scene.items) {
        const auto origin_it = item.source_metadata.find("scene_origin");
        if (origin_it != item.source_metadata.end() && origin_it->second == "imported_instance") {
            saw_instance = true;
            REQUIRE(item.id.find("layout-1:instance:") != std::string::npos);
            REQUIRE(item.bounds.valid);
        }
        if (item.kind == SceneItemKind::Port && item.id.find("layout-1:port:") != std::string::npos) {
            saw_stable_port = true;
        }
    }
    REQUIRE(saw_instance);
    REQUIRE(saw_stable_port);
}
