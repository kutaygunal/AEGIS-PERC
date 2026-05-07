#include <catch2/catch_test_macros.hpp>
#include "aegis/parsing/parser_interface.hpp"

using namespace aegis::parsing;

// ---------------------------------------------------------------------------
// Mock parser for interface contract verification
// ---------------------------------------------------------------------------
class MockParser : public IParser {
public:
    bool open(const std::string& path) override {
        m_path = path;
        m_open = true;
        return true;
    }
    void close() override { m_open = false; }
    bool is_open() const override { return m_open; }
private:
    std::string m_path;
    bool m_open = false;
};

TEST_CASE("Mock parser implements IParser contract", "[parsing][p1-010][fast][ModuleBoundary]")
{
    MockParser parser;
    REQUIRE(!parser.is_open());

    REQUIRE(parser.open("test.def"));
    REQUIRE(parser.is_open());

    parser.close();
    REQUIRE(!parser.is_open());
}
