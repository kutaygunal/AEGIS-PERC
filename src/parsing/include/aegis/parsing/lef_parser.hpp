#pragma once

#include "aegis/parsing/parser_interface.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aegis::parsing {

struct LefDiagnostic {
    enum class Severity {
        Info,
        Warning,
        Error
    };

    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    std::optional<std::size_t> line;
};

struct LefPoint {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] bool operator==(const LefPoint& other) const noexcept = default;
};

struct LefAxisValue {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] bool operator==(const LefAxisValue& other) const noexcept = default;
};

enum class LefSiteClass {
    Unknown,
    Core,
    Pad
};

enum class LefLayerType {
    Unknown,
    Routing,
    Cut,
    Masterslice,
    Overlap,
    Implant
};

enum class LefRoutingDirection {
    Unknown,
    Horizontal,
    Vertical,
    Diag45,
    Diag135
};

enum class LefPinDirection {
    Unknown,
    Input,
    Output,
    Inout,
    Feedthru
};

enum class LefPinUse {
    Unknown,
    Signal,
    Power,
    Ground,
    Clock,
    Analog,
    Reset,
    Scan,
    Tieoff
};

enum class LefMacroClass {
    Unknown,
    Core,
    Block,
    Pad,
    Endcap,
    Cover,
    Ring,
    Spacer
};

struct LefRect {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    std::size_t source_line = 0;

    [[nodiscard]] double width() const noexcept { return x2 - x1; }
    [[nodiscard]] double height() const noexcept { return y2 - y1; }
    [[nodiscard]] bool operator==(const LefRect& other) const noexcept = default;
};

struct LefPolygon {
    std::vector<LefPoint> points;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefPolygon& other) const noexcept = default;
};

struct LefPath {
    std::vector<LefPoint> points;
    std::optional<double> width;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefPath& other) const noexcept = default;
};

struct LefViaPlacement {
    double x = 0.0;
    double y = 0.0;
    std::string via_name;
    std::string orientation;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefViaPlacement& other) const noexcept = default;
};

struct LefLayerGeometry {
    std::string layer_name;
    std::vector<LefRect> rects;
    std::vector<LefPolygon> polygons;
    std::vector<LefPath> paths;
    std::vector<LefViaPlacement> via_placements;
    std::optional<double> path_width_hint;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefLayerGeometry& other) const noexcept = default;
};

struct LefPort {
    std::vector<LefLayerGeometry> layers;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefPort& other) const noexcept = default;
};

struct LefPin {
    std::string name;
    std::string direction;
    std::string use;
    std::vector<LefPort> ports;
    std::map<std::string, std::string> properties;
    LefPinDirection direction_kind = LefPinDirection::Unknown;
    bool is_tristate = false;
    LefPinUse use_kind = LefPinUse::Unknown;
    std::optional<std::string> shape;
    std::optional<double> capacitance;
    std::optional<double> antenna_gate_area;
    std::optional<double> antenna_diff_area;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefPin& other) const noexcept = default;
};

struct LefObs {
    std::vector<LefLayerGeometry> layers;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefObs& other) const noexcept = default;
};

struct LefMacro {
    std::string name;
    std::string macro_class;
    std::string foreign_name;
    double origin_x = 0.0;
    double origin_y = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::vector<LefPin> pins;
    std::optional<LefObs> obstruction;
    std::map<std::string, std::string> properties;
    LefMacroClass class_kind = LefMacroClass::Unknown;
    std::vector<std::string> symmetries;
    std::optional<std::string> site_name;
    std::optional<std::string> source;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefMacro& other) const noexcept = default;
};

struct LefSite {
    std::string name;
    std::string site_class;
    double width = 0.0;
    double height = 0.0;
    std::map<std::string, std::string> properties;
    LefSiteClass class_kind = LefSiteClass::Unknown;
    std::vector<std::string> symmetries;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefSite& other) const noexcept = default;
};

struct LefLayer {
    std::string name;
    std::string type;
    std::map<std::string, std::string> properties;
    LefLayerType type_kind = LefLayerType::Unknown;
    LefRoutingDirection routing_direction = LefRoutingDirection::Unknown;
    std::optional<double> width_value;
    std::optional<double> spacing_value;
    std::optional<LefAxisValue> pitch;
    std::optional<LefAxisValue> offset;
    std::optional<double> resistance;
    std::optional<double> capacitance;
    std::optional<double> edge_capacitance;
    std::optional<double> area;
    std::optional<double> thickness;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefLayer& other) const noexcept = default;
};

struct LefVia {
    std::string name;
    std::vector<LefLayerGeometry> layers;
    std::map<std::string, std::string> properties;
    bool is_default = false;
    std::optional<double> resistance;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefVia& other) const noexcept = default;
};

struct LefViaRule {
    std::string name;
    bool generate = false;
    std::map<std::string, std::string> properties;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefViaRule& other) const noexcept = default;
};

struct LefNonDefaultRule {
    std::string name;
    bool hard_spacing = false;
    std::map<std::string, std::string> properties;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefNonDefaultRule& other) const noexcept = default;
};

struct LefLibraryData {
    std::string version;
    std::string divider_char;
    std::string bus_bit_chars;
    std::map<std::string, std::string> properties;
    std::vector<LefSite> sites;
    std::vector<LefLayer> layers;
    std::vector<LefVia> vias;
    std::vector<LefViaRule> via_rules;
    std::vector<LefNonDefaultRule> non_default_rules;
    std::vector<LefMacro> macros;
    std::vector<LefDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

using LefProgressCallback = std::function<void(const Progress&)>;

class LefParser {
public:
    LefLibraryData parse_string(const std::string& content,
                                const std::string& source_name = "",
                                const ICancellationToken& token = NullCancellationToken{},
                                LefProgressCallback progress = {}) const;

    LefLibraryData parse_file(const std::filesystem::path& path,
                              const ICancellationToken& token = NullCancellationToken{},
                              LefProgressCallback progress = {}) const;
};

std::string to_string(LefDiagnostic::Severity severity);
std::string to_string(LefSiteClass value);
std::string to_string(LefLayerType value);
std::string to_string(LefRoutingDirection value);
std::string to_string(LefPinDirection value);
std::string to_string(LefPinUse value);
std::string to_string(LefMacroClass value);

} // namespace aegis::parsing
