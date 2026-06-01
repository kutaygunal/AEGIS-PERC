#include <catch2/catch_test_macros.hpp>

#include "aegis/reporting/baseline.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using aegis::reporting::BaselineFile;
using aegis::reporting::build_baseline;
using aegis::reporting::read_baseline_json_file;
using aegis::reporting::write_baseline_json_file;
using aegis::rules::Severity;
using aegis::rules::Violation;
using aegis::rules::ViolationLocation;

namespace {

struct TempFile {
    fs::path path;
    explicit TempFile(std::string_view name)
    {
        path = fs::temp_directory_path() / std::string(name);
        std::ofstream(path, std::ios::binary).close();
    }
    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

} // namespace

TEST_CASE("BaselineIO round-trips baseline JSON and preserves deterministic ordering",
          "[BaselineIO][S1-004][fast]")
{
    Violation a{"R1", Severity::Error, "msg", "n2"};
    Violation b{"R1", Severity::Error, "msg", "n1"};

    const auto baseline = build_baseline(std::vector<Violation>{a, b}, "P");
    REQUIRE(baseline.schema_version == BaselineFile::baseline_schema_version);
    REQUIRE(baseline.records.size() == 2);
    CHECK(baseline.records[0].identity_key < baseline.records[1].identity_key);

    TempFile tmp("aegis_baseline_test.json");
    write_baseline_json_file(tmp.path, baseline);
    const auto restored = read_baseline_json_file(tmp.path);

    CHECK(restored == baseline);
}

TEST_CASE("BaselineIO rejects mismatched schema versions",
          "[BaselineIO][S1-004][fast]")
{
    TempFile tmp("aegis_baseline_badver.json");
    std::ofstream out(tmp.path, std::ios::binary);
    out << R"({"baseline_schema_version":999,"records":[]})";
    out.close();

    CHECK_THROWS_AS(read_baseline_json_file(tmp.path), std::invalid_argument);
}

