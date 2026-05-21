#include <catch2/catch_test_macros.hpp>

#include "aegis/ui/imported_design_realization.hpp"
#include "aegis/ui/scene_adapter.hpp"
#include "aegis/storage/project_package.hpp"

#include <QApplication>
#include <QSignalSpy>

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

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

std::filesystem::path repo_import_package_dir()
{
    const auto rooted = std::filesystem::path(AEGIS_SOURCE_DIR) / "data" / "import_packages" / "openframe_simple_design";
    return rooted;
}

aegis::storage::ProjectPackage make_valid_package()
{
    using namespace aegis::storage;
    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "OpenFrameSimpleDesign";
    package.artifacts().push_back({"technology-1", "layout/technology.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.rebuild_normalized_view();
    return package;
}

aegis::storage::ProjectPackage make_empty_package()
{
    using namespace aegis::storage;
    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "Empty";
    package.rebuild_normalized_view();
    return package;
}

} // namespace

TEST_CASE("ImportedDesignRealization progresses through all stages", "[ui][P13-010][RealizationPipeline]")
{
    QtAppGuard guard;

    aegis::ui::ImportedDesignRealization realization;
    QSignalSpy progress_spy(&realization, &aegis::ui::ImportedDesignRealization::progress_changed);

    bool finished_called = false;
    aegis::ui::RealizationStage finished_stage = aegis::ui::RealizationStage::None;
    QObject::connect(&realization, &aegis::ui::ImportedDesignRealization::finished,
        [&finished_called, &finished_stage]() {
            finished_called = true;
            finished_stage = aegis::ui::RealizationStage::AnalysisReady;
        });

    const auto base_path = repo_import_package_dir();
    REQUIRE(std::filesystem::exists(base_path));

    realization.start(make_valid_package(), base_path);
    REQUIRE(realization.wait(std::chrono::seconds(30)));

    // Process any pending Qt events so the queued signal is delivered
    QCoreApplication::processEvents();

    REQUIRE(finished_called);
    REQUIRE(finished_stage == aegis::ui::RealizationStage::AnalysisReady);

    const auto progress = realization.progress();
    REQUIRE(progress.finished);
    REQUIRE(!progress.failed);
    REQUIRE(progress.completed_stages == 5);
    REQUIRE(progress.stage == aegis::ui::RealizationStage::AnalysisReady);

    auto result = realization.take_result();
    REQUIRE(result.has_value());
    REQUIRE(result->reached_stage == aegis::ui::RealizationStage::AnalysisReady);
    REQUIRE(result->session != nullptr);
    REQUIRE(!result->scene_result.scene.items.empty());
    REQUIRE(!result->failed);
    REQUIRE(!result->cancelled);
}

TEST_CASE("ImportedDesignRealization can be cancelled", "[ui][P13-010][RealizationPipeline]")
{
    QtAppGuard guard;

    aegis::ui::ImportedDesignRealization realization;

    bool finished_called = false;
    bool finished_cancelled = false;
    QObject::connect(&realization, &aegis::ui::ImportedDesignRealization::finished,
        [&finished_called, &finished_cancelled]() {
            finished_called = true;
            finished_cancelled = true;
        });

    const auto base_path = repo_import_package_dir();
    REQUIRE(std::filesystem::exists(base_path));

    realization.start(make_valid_package(), base_path);
    realization.request_cancel();
    REQUIRE(realization.wait(std::chrono::seconds(30)));

    QCoreApplication::processEvents();

    REQUIRE(finished_called);
    REQUIRE(finished_cancelled);

    auto result = realization.take_result();
    REQUIRE(result.has_value());
    REQUIRE(result->cancelled);
    REQUIRE(!result->failed);
}

TEST_CASE("ImportedDesignRealization handles empty package gracefully", "[ui][P13-010][RealizationPipeline]")
{
    QtAppGuard guard;

    aegis::ui::ImportedDesignRealization realization;

    bool finished_called = false;
    QObject::connect(&realization, &aegis::ui::ImportedDesignRealization::finished,
        [&finished_called]() {
            finished_called = true;
        });

    const auto base_path = std::filesystem::temp_directory_path();
    realization.start(make_empty_package(), base_path);
    REQUIRE(realization.wait(std::chrono::seconds(30)));

    QCoreApplication::processEvents();

    REQUIRE(finished_called);

    auto result = realization.take_result();
    REQUIRE(result.has_value());
    REQUIRE(!result->failed);
    REQUIRE(!result->cancelled);
    REQUIRE(result->session != nullptr);
    REQUIRE(result->scene_result.scene.items.empty());
}

TEST_CASE("ImportedDesignRealization emits progress_changed during work", "[ui][P13-010][RealizationPipeline]")
{
    QtAppGuard guard;

    aegis::ui::ImportedDesignRealization realization;
    QSignalSpy progress_spy(&realization, &aegis::ui::ImportedDesignRealization::progress_changed);

    const auto base_path = repo_import_package_dir();
    REQUIRE(std::filesystem::exists(base_path));

    realization.start(make_valid_package(), base_path);
    REQUIRE(realization.wait(std::chrono::seconds(30)));

    QCoreApplication::processEvents();

    // Should have at least one progress update per stage
    REQUIRE(progress_spy.count() >= 4);
}
