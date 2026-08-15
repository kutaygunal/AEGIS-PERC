#pragma once

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aegis::parsing {

// ---------------------------------------------------------------------------
// GDSII Stream record constants (data-type + record-type codes).
//
// See docs/design/gdsii_oasis_ingestion.md for the binary record header
// layout and the exact record subset this parser understands.
// ---------------------------------------------------------------------------

namespace gdsii_record {

constexpr std::uint8_t kHeader   = 0x00;
constexpr std::uint8_t kBgnLib   = 0x01;
constexpr std::uint8_t kLibName  = 0x02;
constexpr std::uint8_t kUnits    = 0x03;
constexpr std::uint8_t kEndLib   = 0x04;
constexpr std::uint8_t kBgnStr   = 0x05;
constexpr std::uint8_t kStrName  = 0x06;
constexpr std::uint8_t kEndStr   = 0x07;
constexpr std::uint8_t kBoundary = 0x08;
constexpr std::uint8_t kPath     = 0x09; // unsupported in this increment (deferred)
constexpr std::uint8_t kSref     = 0x0A; // unsupported in this increment (deferred)
constexpr std::uint8_t kAref     = 0x0B; // unsupported in this increment (deferred)
constexpr std::uint8_t kText     = 0x0C; // unsupported in this increment (deferred)
constexpr std::uint8_t kLayer    = 0x0D;
constexpr std::uint8_t kDataType = 0x0E;
constexpr std::uint8_t kXy       = 0x10;
constexpr std::uint8_t kEndEl    = 0x11;

namespace data_type {
constexpr std::uint8_t kNoData   = 0;
constexpr std::uint8_t kBitArray = 1;
constexpr std::uint8_t kInt16    = 2;
constexpr std::uint8_t kInt32    = 3;
constexpr std::uint8_t kReal32   = 4;
constexpr std::uint8_t kReal64   = 5;
constexpr std::uint8_t kAscii    = 6;
} // namespace data_type

} // namespace gdsii_record

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

struct GdsiiDiagnostic {
    enum class Severity {
        Info,
        Warning,
        Error
    };

    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    std::optional<std::size_t> byte_offset;
};

std::string to_string(GdsiiDiagnostic::Severity severity);

// ---------------------------------------------------------------------------
// Typed intermediate model (parallels LefLibraryData for the GDSII subset)
// ---------------------------------------------------------------------------

struct GdsiiBoundary {
    std::int32_t layer    = 0;
    std::int32_t datatype = 0;
    Polygon      polygon; // physical (user-unit) coordinates; see UNITS scaling
    std::size_t  byte_offset = 0;

    [[nodiscard]] bool operator==(const GdsiiBoundary& other) const noexcept = default;
};

struct GdsiiStructure {
    std::string name;
    std::vector<GdsiiBoundary> boundaries;
    std::size_t byte_offset = 0;

    [[nodiscard]] bool operator==(const GdsiiStructure& other) const noexcept = default;
};

struct GdsiiLibraryData {
    std::string lib_name;
    std::optional<std::int16_t> version;

    // From UNITS: see docs/design/gdsii_oasis_ingestion.md "Physical scale".
    // Defaults to 1.0 (raw pass-through) if UNITS is absent/malformed.
    double db_unit_in_user_units = 1.0;
    double db_unit_in_meters     = 0.0;

    std::vector<GdsiiStructure>  structures;
    std::vector<GdsiiDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

/**
 * Native GDSII Stream binary parser (scoped record subset).
 *
 * Implements the same IParser streaming contract as DefParser (on_begin /
 * on_cell / on_geometry / on_progress / on_error / on_end), and additionally
 * exposes a typed intermediate model (GdsiiLibraryData, parallel to
 * LefParser's LefLibraryData) plus a LayoutIR convenience conversion
 * (parallel to DefParser::parse_to_layout_ir()).
 *
 * Supported record subset: HEADER, BGNLIB, LIBNAME, UNITS, BGNSTR, STRNAME,
 * BOUNDARY, LAYER, DATATYPE, XY, ENDEL, ENDSTR, ENDLIB. Every other record
 * (PATH, SREF, AREF, TEXT, ...) is skipped with a Warning diagnostic rather
 * than treated as fatal -- see docs/design/gdsii_oasis_ingestion.md.
 *
 * GDSII is a binary format with no line numbers. Wherever the IParser
 * contract has a "line" (IParserCallbacks::on_error's `line` parameter),
 * this parser reports a byte offset into the stream instead.
 *
 * Cancellation is polled periodically between records; parsing large
 * streams does not require loading the whole file into memory (records are
 * read and processed one at a time).
 */
class GdsiiParser : public IParser {
public:
    std::string format_name() const override;

    /// Streaming parse entry-point (IParser contract).
    bool parse(const std::filesystem::path& path,
               IParserCallbacks& callbacks,
               const ICancellationToken& token) override;

    /// Parse into the typed GDSII intermediate model.
    GdsiiLibraryData parse_to_library(const std::filesystem::path& path,
                                       const ICancellationToken& token = NullCancellationToken{});

    /**
     * Convenience overload that builds a LayoutIR directly.
     *
     * Every structure's BOUNDARY geometry in the stream is flattened into
     * one LayoutIR document -- see "Known limitation" in
     * docs/design/gdsii_oasis_ingestion.md (no SREF/AREF in this
     * increment, so there is currently no cross-structure placement to
     * lose by flattening).
     *
     * @throws LayoutIRError if a fatal diagnostic occurred during parsing.
     */
    LayoutIR parse_to_layout_ir(const std::filesystem::path& path,
                                 const ICancellationToken& token = NullCancellationToken{});
};

} // namespace aegis::parsing
