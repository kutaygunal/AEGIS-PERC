#include <catch2/catch_test_macros.hpp>

#include "aegis/rules/waivers.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace aegis::rules;

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

void write_all(const fs::path& path, const std::string& content)
{
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.good());
    f << content;
    f.close();
}

} // namespace

TEST_CASE("Waivers CSV parses identity_key rows and warns on unknown columns",
          "[Waivers][S1-002][fast]")
{
    TempFile tmp("aegis_waivers_test.csv");
    write_all(tmp.path,
              "identity_key,rule_id,comment,unknown_col\n"
              "v1|rule=R|layer=|net=n1|pin=|device=|pt=,R,\"note\",x\n");

    const auto parsed = parse_waivers_csv_file(tmp.path);
    REQUIRE_FALSE(parsed.has_errors());
    REQUIRE(parsed.waivers.size() == 1);
    CHECK(parsed.waivers[0].identity_key.has_value());
    CHECK(parsed.waivers[0].rule_id.value() == "R");
    CHECK_FALSE(parsed.diagnostics.empty());
}

TEST_CASE("Waivers JSON parses array and rejects entries missing match keys",
          "[Waivers][S1-002][fast]")
{
    TempFile tmp("aegis_waivers_test.json");
    write_all(tmp.path,
              R"([
  {"identity_key":"k1","rule_id":"R1","comment":"c"},
  {"comment":"missing keys"}
])");

    const auto parsed = parse_waivers_json_file(tmp.path);
    REQUIRE(parsed.has_errors());
    REQUIRE(parsed.waivers.size() == 1);
    CHECK(parsed.waivers[0].identity_key.value() == "k1");
}

TEST_CASE("Waivers YAML parses minimal list-of-maps form",
          "[Waivers][S1-002][fast]")
{
    TempFile tmp("aegis_waivers_test.yaml");
    write_all(tmp.path,
              "waivers:\n"
              "  - identity_key: \"k1\"\n"
              "    rule_id: R1\n"
              "    owner: alice\n");

    const auto parsed = parse_waivers_yaml_file(tmp.path);
    REQUIRE_FALSE(parsed.has_errors());
    REQUIRE(parsed.waivers.size() == 1);
    CHECK(parsed.waivers[0].identity_key.value() == "k1");
    CHECK(parsed.waivers[0].rule_id.value() == "R1");
    CHECK(parsed.waivers[0].owner.value() == "alice");
}

