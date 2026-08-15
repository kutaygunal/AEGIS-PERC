#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "aegis/parsing/gdsii_parser.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace aegis::parsing;
using Catch::Matchers::ContainsSubstring;

// ---------------------------------------------------------------------------
// Synthetic GDSII stream builder
//
// Builds real GDSII binary records (2-byte big-endian length, 1-byte record
// type, 1-byte data type, payload) so tests exercise the actual byte-level
// parser rather than a mocked abstraction. The GDSII 8-byte "real" encoder
// used for UNITS is implemented independently of GdsiiParser's decoder (both
// derive from the public GDSII spec, not from each other) so the tests
// double as a correctness check on the decoder, not a round-trip tautology.
// ---------------------------------------------------------------------------

namespace {

std::array<unsigned char, 8> encode_gds_real8(double value)
{
    std::array<unsigned char, 8> out{};
    if (value == 0.0) {
        return out;
    }

    const bool negative = value < 0.0;
    double mag = std::fabs(value);
    int exponent = 0;
    while (mag >= 1.0) {
        mag /= 16.0;
        ++exponent;
    }
    while (mag < (1.0 / 16.0)) {
        mag *= 16.0;
        --exponent;
    }

    const double scale = static_cast<double>(std::uint64_t{1} << 56);
    auto mantissa = static_cast<std::uint64_t>(std::llround(mag * scale));
    if (mantissa >= (std::uint64_t{1} << 56)) {
        mantissa >>= 4;
        ++exponent;
    }

    const int biased = exponent + 64;
    out[0] = static_cast<unsigned char>((negative ? 0x80 : 0x00) | (biased & 0x7F));
    for (int i = 7; i >= 1; --i) {
        out[static_cast<std::size_t>(i)] = static_cast<unsigned char>(mantissa & 0xFFU);
        mantissa >>= 8;
    }
    return out;
}

class GdsBuilder {
public:
    void header(std::int16_t version) { record_int16(gdsii_record::kHeader, {version}); }
    void bgnlib() { record_int16(gdsii_record::kBgnLib, std::vector<std::int16_t>(12, 0)); }
    void libname(const std::string& name) { record_ascii(gdsii_record::kLibName, name); }
    void units(double user_per_db, double meters_per_db) { record_real8(gdsii_record::kUnits, {user_per_db, meters_per_db}); }
    void bgnstr() { record_int16(gdsii_record::kBgnStr, std::vector<std::int16_t>(12, 0)); }
    void strname(const std::string& name) { record_ascii(gdsii_record::kStrName, name); }
    void boundary() { record_empty(gdsii_record::kBoundary); }
    void layer(std::int16_t value) { record_int16(gdsii_record::kLayer, {value}); }
    void datatype(std::int16_t value) { record_int16(gdsii_record::kDataType, {value}); }
    void xy(std::vector<std::int32_t> coords) { record_int32(gdsii_record::kXy, std::move(coords)); }
    void endel() { record_empty(gdsii_record::kEndEl); }
    void endstr() { record_empty(gdsii_record::kEndStr); }
    void endlib() { record_empty(gdsii_record::kEndLib); }
    void unsupported_record(std::uint8_t record_type) { record_empty(record_type); }

    void raw_bytes(std::initializer_list<unsigned char> bytes)
    {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    // Convenience: a complete minimal single-structure, single-boundary library.
    static GdsBuilder minimal_library(const std::string& lib_name = "MYLIB",
                                       const std::string& struct_name = "TOP",
                                       std::int16_t layer_num = 1,
                                       std::int16_t datatype_num = 0)
    {
        GdsBuilder b;
        b.header(600);
        b.bgnlib();
        b.libname(lib_name);
        b.units(0.001, 1e-9);
        b.bgnstr();
        b.strname(struct_name);
        b.boundary();
        b.layer(layer_num);
        b.datatype(datatype_num);
        // Closed square: (0,0) (0,1000) (1000,1000) (1000,0) (0,0) in database units.
        b.xy({0, 0, 0, 1000, 1000, 1000, 1000, 0, 0, 0});
        b.endel();
        b.endstr();
        b.endlib();
        return b;
    }

    [[nodiscard]] const std::vector<unsigned char>& bytes() const { return buffer_; }

private:
    void record_int16(std::uint8_t record_type, std::vector<std::int16_t> values)
    {
        std::vector<unsigned char> payload;
        payload.reserve(values.size() * 2);
        for (auto v : values) {
            const auto u = static_cast<std::uint16_t>(v);
            payload.push_back(static_cast<unsigned char>((u >> 8) & 0xFFU));
            payload.push_back(static_cast<unsigned char>(u & 0xFFU));
        }
        append_record(record_type, gdsii_record::data_type::kInt16, payload);
    }

    void record_int32(std::uint8_t record_type, std::vector<std::int32_t> values)
    {
        std::vector<unsigned char> payload;
        payload.reserve(values.size() * 4);
        for (auto v : values) {
            const auto u = static_cast<std::uint32_t>(v);
            payload.push_back(static_cast<unsigned char>((u >> 24) & 0xFFU));
            payload.push_back(static_cast<unsigned char>((u >> 16) & 0xFFU));
            payload.push_back(static_cast<unsigned char>((u >> 8) & 0xFFU));
            payload.push_back(static_cast<unsigned char>(u & 0xFFU));
        }
        append_record(record_type, gdsii_record::data_type::kInt32, payload);
    }

    void record_ascii(std::uint8_t record_type, const std::string& text)
    {
        std::vector<unsigned char> payload(text.begin(), text.end());
        if (payload.size() % 2 != 0) {
            payload.push_back(0);
        }
        append_record(record_type, gdsii_record::data_type::kAscii, payload);
    }

    void record_real8(std::uint8_t record_type, std::vector<double> values)
    {
        std::vector<unsigned char> payload;
        payload.reserve(values.size() * 8);
        for (double v : values) {
            const auto bytes = encode_gds_real8(v);
            payload.insert(payload.end(), bytes.begin(), bytes.end());
        }
        append_record(record_type, gdsii_record::data_type::kReal64, payload);
    }

    void record_empty(std::uint8_t record_type)
    {
        append_record(record_type, gdsii_record::data_type::kNoData, {});
    }

    void append_record(std::uint8_t record_type, std::uint8_t data_type, const std::vector<unsigned char>& payload)
    {
        const auto length = static_cast<std::uint16_t>(4 + payload.size());
        buffer_.push_back(static_cast<unsigned char>((length >> 8) & 0xFFU));
        buffer_.push_back(static_cast<unsigned char>(length & 0xFFU));
        buffer_.push_back(record_type);
        buffer_.push_back(data_type);
        buffer_.insert(buffer_.end(), payload.begin(), payload.end());
    }

    std::vector<unsigned char> buffer_;
};

fs::path write_temp_gds(const std::vector<unsigned char>& bytes)
{
    auto tmp = fs::temp_directory_path() /
        ("aegis_gds_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gds");
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return tmp;
}

void remove_temp(const fs::path& p)
{
    std::error_code ec;
    fs::remove(p, ec);
}

struct RecordingCallbacks : public IParserCallbacks {
    fs::path begin_path;
    std::vector<Progress> progresses;
    std::vector<ParsedCell> cells;
    std::vector<ParsedGeometry> geometries;
    std::vector<std::pair<std::string, std::optional<int>>> errors;
    std::optional<bool> end_success;

    void on_begin(const fs::path& p) override { begin_path = p; }
    void on_progress(const Progress& p) override { progresses.push_back(p); }
    void on_cell(const ParsedCell& c) override { cells.push_back(c); }
    void on_net(const ParsedNet&) override {}
    void on_pin(const ParsedPin&) override {}
    void on_geometry(const ParsedGeometry& g) override { geometries.push_back(g); }
    void on_error(const std::string& msg, std::optional<int> line) override { errors.emplace_back(msg, line); }
    void on_end(bool success) override { end_success = success; }
};

struct CancellingCallbacks : public RecordingCallbacks {
    CancellationToken* token = nullptr;
    std::size_t cancel_after_progress = 1;

    void on_progress(const Progress& p) override
    {
        RecordingCallbacks::on_progress(p);
        if (token && progresses.size() >= cancel_after_progress) {
            token->cancel();
        }
    }
};

bool has_diagnostic_code(const GdsiiLibraryData& lib, const std::string& code)
{
    for (const auto& d : lib.diagnostics) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

std::size_t count_diagnostic_code(const GdsiiLibraryData& lib, const std::string& code)
{
    std::size_t count = 0;
    for (const auto& d : lib.diagnostics) {
        if (d.code == code) {
            ++count;
        }
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("GdsiiParser format_name returns GDSII", "[parsing][Gdsii][fast]")
{
    GdsiiParser parser;
    REQUIRE(parser.format_name() == "GDSII");
}

TEST_CASE("GdsiiLibraryData has_errors is false when default constructed", "[parsing][Gdsii][fast]")
{
    GdsiiLibraryData lib;
    REQUIRE_FALSE(lib.has_errors());
}

TEST_CASE("GdsiiParser minimal valid stream round-trips into typed library data", "[parsing][Gdsii][fast]")
{
    const auto builder = GdsBuilder::minimal_library("MYLIB", "TOP", 1, 0);
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    GdsiiLibraryData lib = parser.parse_to_library(tmp, token);
    remove_temp(tmp);

    REQUIRE_FALSE(lib.has_errors());
    REQUIRE(lib.lib_name == "MYLIB");
    REQUIRE(lib.version.has_value());
    REQUIRE(lib.version.value() == 600);
    REQUIRE(lib.db_unit_in_user_units == Catch::Approx(0.001).epsilon(1e-6));
    REQUIRE(lib.db_unit_in_meters == Catch::Approx(1e-9).epsilon(1e-6));

    REQUIRE(lib.structures.size() == 1);
    const auto& structure = lib.structures.front();
    REQUIRE(structure.name == "TOP");
    REQUIRE(structure.boundaries.size() == 1);

    const auto& boundary = structure.boundaries.front();
    REQUIRE(boundary.layer == 1);
    REQUIRE(boundary.datatype == 0);
    // The closing duplicate vertex (0,0) must be dropped: 5 raw pairs -> 4 stored points.
    REQUIRE(boundary.polygon.points.size() == 4);
    REQUIRE(boundary.polygon.points[0].x == Catch::Approx(0.0));
    REQUIRE(boundary.polygon.points[0].y == Catch::Approx(0.0));
    REQUIRE(boundary.polygon.points[1].x == Catch::Approx(0.0));
    REQUIRE(boundary.polygon.points[1].y == Catch::Approx(1.0));
    REQUIRE(boundary.polygon.points[2].x == Catch::Approx(1.0));
    REQUIRE(boundary.polygon.points[2].y == Catch::Approx(1.0));
    REQUIRE(boundary.polygon.points[3].x == Catch::Approx(1.0));
    REQUIRE(boundary.polygon.points[3].y == Catch::Approx(0.0));
}

TEST_CASE("GdsiiParser minimal valid stream maps onto LayoutIR", "[parsing][Gdsii][fast]")
{
    const auto builder = GdsBuilder::minimal_library("MYLIB", "TOP", 1, 0);
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    const LayoutIR ir = parser.parse_to_layout_ir(tmp, token);
    remove_temp(tmp);

    REQUIRE(ir.design_name == "MYLIB");
    REQUIRE(ir.metadata.at("gds_structure_count") == "1");
    REQUIRE(ir.metadata.at("gds_structure_names") == "TOP");

    REQUIRE(ir.layers.size() == 1);
    REQUIRE(ir.layers[0].name == "L1D0");

    REQUIRE(ir.geometries.size() == 1);
    REQUIRE(ir.geometries[0].layer == "L1D0");
    REQUIRE(std::holds_alternative<Polygon>(ir.geometries[0].shape));
    REQUIRE(std::get<Polygon>(ir.geometries[0].shape).points.size() == 4);
}

TEST_CASE("GdsiiParser streaming parse emits structure and geometry events", "[parsing][Gdsii][fast]")
{
    const auto builder = GdsBuilder::minimal_library("MYLIB", "TOP", 2, 3);
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;
    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE(ok);
    REQUIRE(cb.end_success.value_or(false));
    REQUIRE(cb.begin_path == tmp);
    REQUIRE(cb.errors.empty());

    REQUIRE(cb.cells.size() == 1);
    REQUIRE(cb.cells[0].name == "TOP");
    REQUIRE(cb.cells[0].properties.at("gds_boundary_count") == "1");

    REQUIRE(cb.geometries.size() == 1);
    REQUIRE(cb.geometries[0].layer == "L2D3");
    REQUIRE(cb.geometries[0].shape_type == "POLYGON");
    REQUIRE(cb.geometries[0].points.size() == 4);
}

TEST_CASE("GdsiiParser malformed record length produces a diagnostic, not a crash", "[parsing][Gdsii][fast]")
{
    // A 4-byte "header" declaring length=3, which is both odd and less than
    // the mandatory 4-byte header size -- structurally invalid.
    GdsBuilder builder;
    builder.raw_bytes({0x00, 0x03, gdsii_record::kBoundary, gdsii_record::data_type::kNoData});
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;
    const bool ok = parser.parse(tmp, cb, token);

    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(cb.end_success.value_or(true));
    REQUIRE_FALSE(cb.errors.empty());
    bool saw_length_error = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("Invalid GDSII record length") != std::string::npos) {
            saw_length_error = true;
        }
    }
    REQUIRE(saw_length_error);

    // Same input via the typed-model entry point must also fail gracefully.
    GdsiiParser parser2;
    NullCancellationToken token2;
    GdsiiLibraryData lib = parser2.parse_to_library(tmp, token2);
    remove_temp(tmp);

    REQUIRE(lib.has_errors());
    REQUIRE(has_diagnostic_code(lib, "GDS_RECORD_LENGTH_INVALID"));
}

TEST_CASE("GdsiiParser truncated record payload produces a diagnostic, not a crash", "[parsing][Gdsii][fast]")
{
    // Declare a LAYER record (data_type INT16, expects 2 payload bytes -> total
    // length 6) but only supply 1 payload byte before the file ends.
    GdsBuilder builder;
    builder.raw_bytes({0x00, 0x06, gdsii_record::kLayer, gdsii_record::data_type::kInt16, 0x00});
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    GdsiiLibraryData lib = parser.parse_to_library(tmp, token);
    remove_temp(tmp);

    REQUIRE(lib.has_errors());
    REQUIRE(has_diagnostic_code(lib, "GDS_RECORD_TRUNCATED"));
}

TEST_CASE("GdsiiParser skips unsupported record types with a warning instead of failing", "[parsing][Gdsii][fast]")
{
    GdsBuilder b;
    b.header(600);
    b.bgnlib();
    b.libname("MYLIB");
    b.units(0.001, 1e-9);
    b.bgnstr();
    b.strname("TOP");
    b.unsupported_record(gdsii_record::kPath);
    b.unsupported_record(gdsii_record::kPath); // duplicate: should be deduplicated in diagnostics
    b.unsupported_record(gdsii_record::kText);
    b.boundary();
    b.layer(1);
    b.datatype(0);
    b.xy({0, 0, 0, 1000, 1000, 1000, 1000, 0, 0, 0});
    b.endel();
    b.endstr();
    b.endlib();
    auto tmp = write_temp_gds(b.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    GdsiiLibraryData lib = parser.parse_to_library(tmp, token);
    remove_temp(tmp);

    REQUIRE_FALSE(lib.has_errors());
    REQUIRE(has_diagnostic_code(lib, "GDS_UNSUPPORTED_RECORD"));
    REQUIRE(count_diagnostic_code(lib, "GDS_UNSUPPORTED_RECORD") == 2); // PATH once (deduped), TEXT once

    for (const auto& d : lib.diagnostics) {
        if (d.code == "GDS_UNSUPPORTED_RECORD") {
            REQUIRE(d.severity == GdsiiDiagnostic::Severity::Warning);
        }
    }

    // Parsing still completed and the real geometry after the unsupported
    // records was captured normally.
    REQUIRE(lib.structures.size() == 1);
    REQUIRE(lib.structures.front().boundaries.size() == 1);
}

TEST_CASE("GdsiiParser degenerate boundary (fewer than 3 vertices) is dropped with a diagnostic", "[parsing][Gdsii][fast]")
{
    GdsBuilder b;
    b.header(600);
    b.bgnlib();
    b.libname("MYLIB");
    b.units(0.001, 1e-9);
    b.bgnstr();
    b.strname("TOP");
    b.boundary();
    b.layer(1);
    b.datatype(0);
    b.xy({0, 0, 1000, 1000}); // only 2 points -- not a valid polygon
    b.endel();
    b.endstr();
    b.endlib();
    auto tmp = write_temp_gds(b.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    GdsiiLibraryData lib = parser.parse_to_library(tmp, token);
    remove_temp(tmp);

    REQUIRE_FALSE(lib.has_errors());
    REQUIRE(has_diagnostic_code(lib, "GDS_BOUNDARY_INVALID"));
    REQUIRE(lib.structures.size() == 1);
    REQUIRE(lib.structures.front().boundaries.empty());
}

TEST_CASE("GdsiiParser cancellation token stops parsing mid-stream", "[parsing][Gdsii][fast]")
{
    GdsBuilder b;
    b.header(600);
    b.bgnlib();
    b.libname("BIGLIB");
    b.units(0.001, 1e-9);
    for (int s = 0; s < 200; ++s) {
        b.bgnstr();
        b.strname("STRUCT_" + std::to_string(s));
        b.boundary();
        b.layer(1);
        b.datatype(0);
        b.xy({0, 0, 0, 1000, 1000, 1000, 1000, 0, 0, 0});
        b.endel();
        b.endstr();
    }
    b.endlib();
    auto tmp = write_temp_gds(b.bytes());

    CancellationToken token;
    CancellingCallbacks cb;
    cb.token = &token;
    cb.cancel_after_progress = 2;

    GdsiiParser parser;
    const bool ok = parser.parse(tmp, cb, token);
    remove_temp(tmp);

    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(cb.end_success.value_or(true));
    REQUIRE_FALSE(cb.progresses.empty());
    // Cancellation must have prevented all 200 structures from completing.
    REQUIRE(cb.cells.size() < 200);

    bool saw_cancel_error = false;
    for (const auto& [msg, line] : cb.errors) {
        if (msg.find("cancelled") != std::string::npos) {
            saw_cancel_error = true;
        }
    }
    REQUIRE(saw_cancel_error);
}

TEST_CASE("GdsiiParser missing ENDLIB is a non-fatal warning", "[parsing][Gdsii][fast]")
{
    GdsBuilder b;
    b.header(600);
    b.bgnlib();
    b.libname("MYLIB");
    b.units(0.001, 1e-9);
    b.bgnstr();
    b.strname("TOP");
    b.boundary();
    b.layer(1);
    b.datatype(0);
    b.xy({0, 0, 0, 1000, 1000, 1000, 1000, 0, 0, 0});
    b.endel();
    b.endstr();
    // Deliberately no endlib().
    auto tmp = write_temp_gds(b.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    GdsiiLibraryData lib = parser.parse_to_library(tmp, token);
    remove_temp(tmp);

    REQUIRE_FALSE(lib.has_errors());
    REQUIRE(has_diagnostic_code(lib, "GDS_MISSING_ENDLIB"));
    REQUIRE(lib.structures.size() == 1);
}

TEST_CASE("GdsiiParser reports a diagnostic instead of throwing when the file cannot be opened", "[parsing][Gdsii][fast]")
{
    const fs::path missing = fs::temp_directory_path() / "aegis_gds_does_not_exist_12345.gds";

    GdsiiParser parser;
    RecordingCallbacks cb;
    NullCancellationToken token;
    const bool ok = parser.parse(missing, cb, token);

    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(cb.end_success.value_or(true));
    REQUIRE_FALSE(cb.errors.empty());
}

TEST_CASE("GdsiiParser parse_to_layout_ir throws LayoutIRError on fatal parse failure", "[parsing][Gdsii][fast]")
{
    GdsBuilder builder;
    builder.raw_bytes({0x00, 0x03, gdsii_record::kBoundary, gdsii_record::data_type::kNoData});
    auto tmp = write_temp_gds(builder.bytes());

    GdsiiParser parser;
    NullCancellationToken token;
    REQUIRE_THROWS_AS(parser.parse_to_layout_ir(tmp, token), LayoutIRError);
    remove_temp(tmp);
}
