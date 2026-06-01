#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/hierarchy_browser_panel.hpp"
#include "aegis/storage/imported_design_session.hpp"

#include <QApplication>
#include <memory>
#include <filesystem>

using namespace aegis::storage;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
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

TEST_CASE("HierarchyBrowserPanel shows empty state when no session is loaded",
          "[ui][P13][P13-003][HierarchyBrowser]")
{
    QtAppGuard guard;
    aegis::ui::HierarchyBrowserPanel panel;
    REQUIRE(panel.session() == nullptr);
    REQUIRE(panel.visible_item_count() == 0);
    REQUIRE(panel.status_text().contains("Load an imported design", Qt::CaseInsensitive));
    REQUIRE(panel.current_stable_id().isEmpty());
}

TEST_CASE("HierarchyBrowserPanel populates categories from imported design session",
          "[ui][P13][P13-003][HierarchyBrowser]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);

    aegis::ui::HierarchyBrowserPanel panel;
    panel.set_session(&session);

    REQUIRE(panel.session() == &session);
    REQUIRE(panel.visible_item_count() > 0);
    REQUIRE(panel.status_text().contains("visible", Qt::CaseInsensitive));
    REQUIRE(panel.status_text().contains(QString::number(session.objects().size())));
}

TEST_CASE("HierarchyBrowserPanel search finds and selects an object by name",
          "[ui][P13][P13-003][HierarchyBrowser]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);

    aegis::ui::HierarchyBrowserPanel panel;
    panel.set_session(&session);

    const auto& first_object = session.objects().front();
    const QString name = QString::fromStdString(first_object.display_name.empty() ? first_object.name : first_object.display_name);
    REQUIRE(panel.search_and_select(name));
    REQUIRE(panel.current_stable_id() == QString::fromStdString(first_object.stable_id));

    REQUIRE_FALSE(panel.search_and_select("nonexistent_xyz_12345"));
    REQUIRE(panel.status_text().contains("No object found", Qt::CaseInsensitive));
}

TEST_CASE("HierarchyBrowserPanel clear_selection removes current highlight",
          "[ui][P13][P13-003][HierarchyBrowser]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);

    aegis::ui::HierarchyBrowserPanel panel;
    panel.set_session(&session);

    REQUIRE(panel.search_and_select(QString::fromStdString(session.objects().front().name)));
    REQUIRE_FALSE(panel.current_stable_id().isEmpty());
    panel.clear_selection();
    REQUIRE(panel.current_stable_id().isEmpty());
}

TEST_CASE("HierarchyBrowserPanel avoids eager expansion for large sessions",
          "[ui][P13][P13-FOLLOW-001][HierarchyBrowser]")
{
    QtAppGuard guard;

    ImportedDesignSession session;
    session.objects().reserve(100000);

    for (std::size_t i = 0; i < 100000; ++i) {
        ImportedDesignObject obj;
        obj.kind = ImportedDesignObjectKind::Instance;
        obj.name = "U" + std::to_string(i);
        obj.display_name = obj.name;
        obj.stable_id = "instance:" + std::to_string(i);
        session.objects().push_back(std::move(obj));
    }

    aegis::ui::HierarchyBrowserPanel panel;
    REQUIRE_NOTHROW(panel.set_session(&session));

    // Large sessions should not auto-expand all items (lazy view population).
    REQUIRE(panel.visible_item_count() == 0);
    REQUIRE(panel.status_text().contains(QString::number(session.objects().size())));

    // Search must still be able to select an object without pre-expanding everything.
    REQUIRE(panel.search_and_select("U42"));
    REQUIRE(panel.current_stable_id() == "instance:42");
}


TEST_CASE("HierarchyBrowserPanel destruction does not crash", "[ui][HierarchyBrowser][debug]")
{
    QtAppGuard guard;
    aegis::ui::HierarchyBrowserPanel panel;
    // Just create and destroy without a session
}


TEST_CASE("HierarchyBrowserPanel destruction after set_session does not crash", "[ui][HierarchyBrowser][debug]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    const ImportedDesignSession session = builder.build(package, root);

    aegis::ui::HierarchyBrowserPanel panel;
    panel.set_session(&session);
    // Destroy with session set
}


TEST_CASE("HierarchyBrowserPanel destruction after dangling session does not crash", "[ui][HierarchyBrowser][debug]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    auto session = std::make_unique<ImportedDesignSession>(builder.build(package, root));

    aegis::ui::HierarchyBrowserPanel panel;
    panel.set_session(session.get());
    
    // Simulate MainWindow destruction order: delete session first, then destroy panel
    session.reset();
    
    // panel.set_session(nullptr);  // MainWindow doesn't do this in destructor
    // Just let panel be destroyed with dangling session pointer
}


TEST_CASE("HierarchyBrowserPanel heap destruction after dangling session does not crash", "[ui][HierarchyBrowser][debug]")
{
    QtAppGuard guard;
    const fs::path root = fs::path(AEGIS_SOURCE_DIR) / "data/import_packages/openframe_simple_design";

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();

    ImportedDesignSessionBuilder builder;
    auto session = std::make_unique<ImportedDesignSession>(builder.build(package, root));

    auto* panel = new aegis::ui::HierarchyBrowserPanel();
    panel->set_session(session.get());
    
    // Simulate MainWindow destruction order: delete session first
    session.reset();
    
    // Then destroy panel (as Qt would do)
    delete panel;
}
