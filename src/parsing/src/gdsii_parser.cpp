#include "aegis/parsing/gdsii_parser.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

namespace aegis::parsing {

namespace {

// ---------------------------------------------------------------------------
// Low-level binary decoding helpers
// ---------------------------------------------------------------------------

bool read_exact(std::istream& in, unsigned char* buf, std::size_t n)
{
    in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(in.gcount()) == n;
}

std::int16_t decode_i16_be(const unsigned char* p)
{
    const auto u = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
    return static_cast<std::int16_t>(u);
}

std::int32_t decode_i32_be(const unsigned char* p)
{
    const std::uint32_t u =
        (static_cast<std::uint32_t>(p[0]) << 24) |
        (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) |
        static_cast<std::uint32_t>(p[3]);
    return static_cast<std::int32_t>(u);
}

// GDSII 8-byte "real": sign / excess-64 base-16 exponent / 56-bit mantissa.
// NOT IEEE-754. See docs/design/gdsii_oasis_ingestion.md.
double decode_gds_real8(const unsigned char* p)
{
    const bool negative = (p[0] & 0x80U) != 0;
    const int exponent = static_cast<int>(p[0] & 0x7FU) - 64;

    std::uint64_t mantissa = 0;
    for (int i = 1; i < 8; ++i) {
        mantissa = (mantissa << 8) | static_cast<std::uint64_t>(p[static_cast<std::size_t>(i)]);
    }

    const double frac = static_cast<double>(mantissa) / static_cast<double>(std::uint64_t{1} << 56);
    const double magnitude = frac * std::pow(16.0, exponent);
    return negative ? -magnitude : magnitude;
}

std::string decode_ascii(const std::vector<unsigned char>& payload)
{
    std::size_t end = payload.size();
    while (end > 0 && payload[end - 1] == 0) {
        --end;
    }
    return std::string(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(end));
}

std::string record_type_label(std::uint8_t rt)
{
    switch (rt) {
    case gdsii_record::kPath: return "PATH";
    case gdsii_record::kSref: return "SREF";
    case gdsii_record::kAref: return "AREF";
    case gdsii_record::kText: return "TEXT";
    default: break;
    }
    std::array<char, 8> buf{};
    std::snprintf(buf.data(), buf.size(), "0x%02X", static_cast<unsigned>(rt));
    return std::string(buf.data());
}

// ---------------------------------------------------------------------------
// Streaming record walker
// ---------------------------------------------------------------------------

struct ParseState {
    bool have_bgnlib = false;
    bool in_structure = false;
    bool have_pending_element = false;
    std::size_t current_structure_index = 0;
    GdsiiBoundary pending;
    bool ended_lib = false;
};

bool parse_gds_stream(std::istream& in,
                       std::size_t file_size,
                       GdsiiLibraryData& lib,
                       IParserCallbacks* callbacks,
                       const ICancellationToken& token)
{
    auto emit = [&](GdsiiDiagnostic::Severity sev,
                     const std::string& code,
                     const std::string& message,
                     std::size_t offset) {
        lib.diagnostics.push_back(GdsiiDiagnostic{sev, code, message, offset});
        if (callbacks) {
            callbacks->on_error(message, static_cast<int>(offset));
        }
    };

    ParseState state;
    std::array<bool, 256> warned_unsupported{};
    std::size_t record_index = 0;
    std::size_t last_pct_reported = 0;

    auto report_progress = [&](std::size_t bytes_consumed) {
        if (!callbacks || file_size == 0) {
            return;
        }
        const std::size_t pct = (bytes_consumed * 100) / file_size;
        if (pct > last_pct_reported) {
            last_pct_reported = pct;
            callbacks->on_progress({pct, 100, "Parsed " + std::to_string(record_index) + " GDSII records"});
        }
    };

    while (true) {
        if ((record_index % 16 == 0) && token.is_cancelled()) {
            const auto pos = in.tellg();
            emit(GdsiiDiagnostic::Severity::Error, "GDS_PARSE_CANCELLED",
                 "GDSII parse cancelled by caller",
                 pos >= 0 ? static_cast<std::size_t>(pos) : std::size_t{0});
            return false;
        }

        const auto record_start_pos = in.tellg();
        const std::size_t record_start =
            record_start_pos >= 0 ? static_cast<std::size_t>(record_start_pos) : std::size_t{0};

        std::array<unsigned char, 4> header{};
        in.read(reinterpret_cast<char*>(header.data()), 4);
        const std::streamsize got = in.gcount();
        if (got == 0) {
            break; // clean EOF between records
        }
        if (got != 4) {
            emit(GdsiiDiagnostic::Severity::Error, "GDS_RECORD_TRUNCATED",
                 "GDSII stream ended mid record-header at byte " + std::to_string(record_start),
                 record_start);
            return false;
        }

        const auto length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(header[0]) << 8) | header[1]);
        const std::uint8_t record_type = header[2];
        const std::uint8_t data_type = header[3];

        if (length < 4 || (length % 2) != 0) {
            emit(GdsiiDiagnostic::Severity::Error, "GDS_RECORD_LENGTH_INVALID",
                 "Invalid GDSII record length " + std::to_string(length) + " at byte " + std::to_string(record_start),
                 record_start);
            return false;
        }

        const auto payload_len = static_cast<std::size_t>(length) - 4;
        std::vector<unsigned char> payload(payload_len);
        if (payload_len > 0 && !read_exact(in, payload.data(), payload_len)) {
            emit(GdsiiDiagnostic::Severity::Error, "GDS_RECORD_TRUNCATED",
                 "GDSII record payload truncated at byte " + std::to_string(record_start),
                 record_start);
            return false;
        }

        ++record_index;
        report_progress(record_start + length);

        switch (record_type) {
        case gdsii_record::kHeader: {
            if (data_type == gdsii_record::data_type::kInt16 && payload.size() == 2) {
                lib.version = decode_i16_be(payload.data());
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_HEADER_INVALID", "Malformed HEADER record", record_start);
            }
            break;
        }
        case gdsii_record::kBgnLib: {
            state.have_bgnlib = true;
            break;
        }
        case gdsii_record::kLibName: {
            if (data_type == gdsii_record::data_type::kAscii) {
                lib.lib_name = decode_ascii(payload);
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_LIBNAME_INVALID", "Malformed LIBNAME record", record_start);
            }
            break;
        }
        case gdsii_record::kUnits: {
            if (data_type == gdsii_record::data_type::kReal64 && payload.size() == 16) {
                lib.db_unit_in_user_units = decode_gds_real8(payload.data());
                lib.db_unit_in_meters = decode_gds_real8(payload.data() + 8);
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_UNITS_INVALID",
                     "Malformed UNITS record; defaulting to unscaled coordinates", record_start);
            }
            break;
        }
        case gdsii_record::kBgnStr: {
            if (state.in_structure) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_NESTED_STRUCTURE",
                     "BGNSTR seen before a matching ENDSTR; closing previous structure implicitly", record_start);
            }
            GdsiiStructure structure;
            structure.byte_offset = record_start;
            lib.structures.push_back(std::move(structure));
            state.current_structure_index = lib.structures.size() - 1;
            state.in_structure = true;
            state.have_pending_element = false;
            break;
        }
        case gdsii_record::kStrName: {
            if (!state.in_structure) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_STRNAME_OUTSIDE_STRUCTURE",
                     "STRNAME seen outside BGNSTR/ENDSTR", record_start);
                break;
            }
            if (data_type == gdsii_record::data_type::kAscii) {
                lib.structures[state.current_structure_index].name = decode_ascii(payload);
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_STRNAME_INVALID", "Malformed STRNAME record", record_start);
            }
            break;
        }
        case gdsii_record::kBoundary: {
            if (!state.in_structure) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_ELEMENT_OUTSIDE_STRUCTURE",
                     "BOUNDARY seen outside BGNSTR/ENDSTR", record_start);
                break;
            }
            if (state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_ELEMENT_UNTERMINATED",
                     "BOUNDARY seen before a matching ENDEL; discarding previous element", record_start);
            }
            state.pending = GdsiiBoundary{};
            state.pending.byte_offset = record_start;
            state.have_pending_element = true;
            break;
        }
        case gdsii_record::kLayer: {
            if (!state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_LAYER_OUTSIDE_ELEMENT",
                     "LAYER seen outside a BOUNDARY element", record_start);
                break;
            }
            if (data_type == gdsii_record::data_type::kInt16 && payload.size() == 2) {
                state.pending.layer = decode_i16_be(payload.data());
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_LAYER_INVALID", "Malformed LAYER record", record_start);
            }
            break;
        }
        case gdsii_record::kDataType: {
            if (!state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_DATATYPE_OUTSIDE_ELEMENT",
                     "DATATYPE seen outside a BOUNDARY element", record_start);
                break;
            }
            if (data_type == gdsii_record::data_type::kInt16 && payload.size() == 2) {
                state.pending.datatype = decode_i16_be(payload.data());
            } else {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_DATATYPE_INVALID", "Malformed DATATYPE record", record_start);
            }
            break;
        }
        case gdsii_record::kXy: {
            if (!state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_XY_OUTSIDE_ELEMENT",
                     "XY seen outside a BOUNDARY element", record_start);
                break;
            }
            if (data_type != gdsii_record::data_type::kInt32 || payload.size() % 4 != 0) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_XY_INVALID", "Malformed XY record", record_start);
                break;
            }
            const std::size_t num_int32 = payload.size() / 4;
            const std::size_t usable = num_int32 - (num_int32 % 2);
            if (usable != num_int32) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_XY_INVALID",
                     "XY record has an odd number of coordinates; trailing value ignored", record_start);
            }
            std::vector<Point> points;
            points.reserve(usable / 2);
            for (std::size_t i = 0; i + 1 < usable; i += 2) {
                const std::int32_t xi = decode_i32_be(payload.data() + i * 4);
                const std::int32_t yi = decode_i32_be(payload.data() + (i + 1) * 4);
                points.push_back(Point{static_cast<double>(xi) * lib.db_unit_in_user_units,
                                        static_cast<double>(yi) * lib.db_unit_in_user_units});
            }
            state.pending.polygon.points = std::move(points);
            break;
        }
        case gdsii_record::kEndEl: {
            if (!state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_ENDEL_WITHOUT_ELEMENT",
                     "ENDEL seen without a matching BOUNDARY", record_start);
                break;
            }
            auto& pts = state.pending.polygon.points;
            if (pts.size() >= 2) {
                const Point first = pts.front();
                const Point last = pts.back();
                constexpr double kEps = 1e-9;
                if (std::abs(first.x - last.x) < kEps && std::abs(first.y - last.y) < kEps) {
                    pts.pop_back();
                }
            }
            if (pts.size() < 3) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_BOUNDARY_INVALID",
                     "BOUNDARY has fewer than 3 distinct vertices; element dropped", state.pending.byte_offset);
            } else {
                lib.structures[state.current_structure_index].boundaries.push_back(state.pending);
                if (callbacks) {
                    ParsedGeometry pg;
                    pg.layer = "L" + std::to_string(state.pending.layer) + "D" + std::to_string(state.pending.datatype);
                    pg.shape_type = "POLYGON";
                    pg.points.reserve(pts.size());
                    for (const auto& pt : pts) {
                        pg.points.push_back({pt.x, pt.y});
                    }
                    callbacks->on_geometry(pg);
                }
            }
            state.have_pending_element = false;
            break;
        }
        case gdsii_record::kEndStr: {
            if (!state.in_structure) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_ENDSTR_WITHOUT_STRUCTURE",
                     "ENDSTR seen without a matching BGNSTR", record_start);
                break;
            }
            if (state.have_pending_element) {
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_ELEMENT_UNTERMINATED",
                     "ENDSTR seen before a matching ENDEL; discarding open element", record_start);
                state.have_pending_element = false;
            }
            const auto& finished = lib.structures[state.current_structure_index];
            if (callbacks) {
                ParsedCell cell;
                cell.name = finished.name;
                cell.properties["gds_kind"] = "structure";
                cell.properties["gds_boundary_count"] = std::to_string(finished.boundaries.size());
                callbacks->on_cell(cell);
            }
            state.in_structure = false;
            break;
        }
        case gdsii_record::kEndLib: {
            state.ended_lib = true;
            break;
        }
        default: {
            if (!warned_unsupported[record_type]) {
                warned_unsupported[record_type] = true;
                emit(GdsiiDiagnostic::Severity::Warning, "GDS_UNSUPPORTED_RECORD",
                     "Unsupported record type " + record_type_label(record_type) + " skipped", record_start);
            }
            break;
        }
        }

        if (state.ended_lib) {
            break;
        }
    }

    if (!state.ended_lib) {
        const auto pos = in.tellg();
        emit(GdsiiDiagnostic::Severity::Warning, "GDS_MISSING_ENDLIB",
             "GDSII stream ended without ENDLIB",
             pos >= 0 ? static_cast<std::size_t>(pos) : std::size_t{0});
    }

    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// GdsiiLibraryData
// ---------------------------------------------------------------------------

bool GdsiiLibraryData::has_errors() const noexcept
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == GdsiiDiagnostic::Severity::Error) {
            return true;
        }
    }
    return false;
}

std::string to_string(GdsiiDiagnostic::Severity severity)
{
    switch (severity) {
    case GdsiiDiagnostic::Severity::Info: return "Info";
    case GdsiiDiagnostic::Severity::Warning: return "Warning";
    case GdsiiDiagnostic::Severity::Error: return "Error";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// GdsiiParser
// ---------------------------------------------------------------------------

std::string GdsiiParser::format_name() const
{
    return "GDSII";
}

bool GdsiiParser::parse(const std::filesystem::path& path,
                         IParserCallbacks& callbacks,
                         const ICancellationToken& token)
{
    callbacks.on_begin(path);

    GdsiiLibraryData lib;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        const std::string msg = "Cannot open file: " + path.string();
        lib.diagnostics.push_back(GdsiiDiagnostic{GdsiiDiagnostic::Severity::Error, "GDS_FILE_OPEN_FAILED", msg, std::nullopt});
        callbacks.on_error(msg, std::nullopt);
        callbacks.on_end(false);
        return false;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);

    const bool ok = parse_gds_stream(file, ec ? std::size_t{0} : file_size, lib, &callbacks, token);
    callbacks.on_end(ok);
    return ok;
}

GdsiiLibraryData GdsiiParser::parse_to_library(const std::filesystem::path& path,
                                                const ICancellationToken& token)
{
    GdsiiLibraryData lib;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        lib.diagnostics.push_back(GdsiiDiagnostic{
            GdsiiDiagnostic::Severity::Error, "GDS_FILE_OPEN_FAILED", "Cannot open file: " + path.string(), std::nullopt});
        return lib;
    }

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    parse_gds_stream(file, ec ? std::size_t{0} : file_size, lib, nullptr, token);
    return lib;
}

LayoutIR GdsiiParser::parse_to_layout_ir(const std::filesystem::path& path,
                                          const ICancellationToken& token)
{
    GdsiiLibraryData lib = parse_to_library(path, token);
    if (lib.has_errors()) {
        std::string first_error;
        for (const auto& diagnostic : lib.diagnostics) {
            if (diagnostic.severity == GdsiiDiagnostic::Severity::Error) {
                first_error = diagnostic.message;
                break;
            }
        }
        throw LayoutIRError(first_error.empty() ? "GDSII parse failed" : first_error);
    }

    LayoutIR ir;
    ir.design_name = lib.lib_name;
    ir.metadata["gds_structure_count"] = std::to_string(lib.structures.size());
    ir.metadata["gds_db_unit_in_user_units"] = std::to_string(lib.db_unit_in_user_units);
    ir.metadata["gds_db_unit_in_meters"] = std::to_string(lib.db_unit_in_meters);

    std::string names;
    for (const auto& structure : lib.structures) {
        if (!names.empty()) {
            names += ",";
        }
        names += structure.name;
    }
    ir.metadata["gds_structure_names"] = names;

    std::map<std::string, std::size_t> layer_order;
    for (const auto& structure : lib.structures) {
        for (const auto& boundary : structure.boundaries) {
            const std::string layer_name = "L" + std::to_string(boundary.layer) + "D" + std::to_string(boundary.datatype);
            if (layer_order.find(layer_name) == layer_order.end()) {
                const std::size_t order = layer_order.size();
                layer_order[layer_name] = order;
                ir.layers.push_back(Layer{layer_name, "gdsii", static_cast<int>(order), std::string()});
            }
            ir.geometries.push_back(Geometry{layer_name, boundary.polygon});
        }
    }

    return ir;
}

} // namespace aegis::parsing
