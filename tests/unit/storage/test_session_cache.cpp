
#include <catch2/catch_test_macros.hpp>

#include "aegis/storage/imported_design_session.hpp"
#include "aegis/storage/session_cache.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace aegis::storage;

namespace {

void write_text(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << content;
}

ProjectPackage make_minimal_package(const fs::path& root)
{
    write_text(root / "layout/tech.lef",
               "VERSION 5.8 ;\n"
               "LAYER M1\n"
               "  TYPE ROUTING ;\n"
               "  WIDTH 0.10 ;\n"
               "  PITCH 0.20 ;\n"
               "END M1\n"
               "END LIBRARY\n");
    write_text(root / "layout/top.def",
               "VERSION 5.8 ;\n"
               "DESIGN top ;\n"
               "DIEAREA ( 0 0 ) ( 100 100 ) ;\n"
               "PINS 1 ;\n"
               "- A + NET A + DIRECTION INPUT + PLACED ( 10 10 ) N ;\n"
               "END PINS\n"
               "NETS 1 ;\n"
               "- A ( PIN A ) ;\n"
               "END NETS\n"
               "END DESIGN\n");
    write_text(root / "netlist/top.v", "module top(input A, output Y); assign Y = A; endmodule\n");
    write_text(root / "rules/aegis_rules.yaml", "rules:\n  - id: FLOATING_NET\n");

    ProjectPackage package;
    package.set_manifest_version(1);
    package.project().name = "CacheTest";
    package.artifacts().push_back({"tech-1", "layout/tech.lef", ArtifactRole::Lef, ArtifactCategory::Technology, false, "manifest"});
    package.artifacts().push_back({"layout-1", "layout/top.def", ArtifactRole::Def, ArtifactCategory::Layout, false, "manifest"});
    package.artifacts().push_back({"netlist-1", "netlist/top.v", ArtifactRole::Verilog, ArtifactCategory::Netlist, false, "manifest"});
    package.artifacts().push_back({"rules-1", "rules/aegis_rules.yaml", ArtifactRole::AegisRulePack, ArtifactCategory::Rules, false, "manifest"});
    package.rebuild_normalized_view();
    return package;
}

} // namespace

TEST_CASE("SessionCache saves and loads a session round-trip", "[storage][P13-009][SessionCache]")
{
    const fs::path root = fs::temp_directory_path() / "aegis_perc_p13_009_cache_test";
    std::error_code ec;
    fs::remove_all(root, ec);

    const auto package = make_minimal_package(root);
    ImportedDesignSessionBuilder builder;
    ImportedDesignSession session = builder.build(package, root);

    REQUIRE(session.package().project().name == "CacheTest");
    REQUIRE(session.object_count(ImportedDesignObjectKind::Net) > 0);

    SessionCache cache;
    REQUIRE(cache.save(session, root));
    REQUIRE(cache.is_cache_valid(root));

    const auto loaded = cache.load(root);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->package().project().name == "CacheTest");
    REQUIRE(loaded->base_path() == root);
    REQUIRE(loaded->object_count(ImportedDesignObjectKind::Net) > 0);
    REQUIRE(loaded->graph().node_count() > 0);

    fs::remove_all(root, ec);
}

TEST_CASE("SessionCache invalidates when artifact changes", "[storage][P13-009][SessionCache]")
{
    const fs::path root = fs::temp_directory_path() / "aegis_perc_p13_009_invalidation";
    std::error_code ec;
    fs::remove_all(root, ec);

    const auto package = make_minimal_package(root);
    ImportedDesignSessionBuilder builder;
    ImportedDesignSession session = builder.build(package, root);

    SessionCache cache;
    REQUIRE(cache.save(session, root));
    REQUIRE(cache.is_cache_valid(root));

    // Modify an artifact
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    write_text(root / "rules/aegis_rules.yaml", "rules:\n  - id: CHANGED_RULE\n");

    REQUIRE_FALSE(cache.is_cache_valid(root));
    REQUIRE_FALSE(cache.load(root).has_value());

    fs::remove_all(root, ec);
}

TEST_CASE("SessionCache degrades gracefully when cache file is missing", "[storage][P13-009][SessionCache]")
{
    const fs::path root = fs::temp_directory_path() / "aegis_perc_p13_009_missing";
    std::error_code ec;
    fs::remove_all(root, ec);

    SessionCache cache;
    REQUIRE_FALSE(cache.is_cache_valid(root));
    REQUIRE_FALSE(cache.load(root).has_value());

    fs::remove_all(root, ec);
}
