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

struct LefLayerGeometry {
    std::string layer_name;
    std::vector<LefRect> rects;
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
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefMacro& other) const noexcept = default;
};

struct LefSite {
    std::string name;
    std::string site_class;
    double width = 0.0;
    double height = 0.0;
    std::map<std::string, std::string> properties;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefSite& other) const noexcept = default;
};

struct LefLayer {
    std::string name;
    std::string type;
    std::map<std::string, std::string> properties;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefLayer& other) const noexcept = default;
};

struct LefVia {
    std::string name;
    std::vector<LefLayerGeometry> layers;
    std::map<std::string, std::string> properties;
    std::size_t source_line = 0;

    [[nodiscard]] bool operator==(const LefVia& other) const noexcept = default;
};

struct LefLibraryData {
    std::string version;
    std::string divider_char;
    std::string bus_bit_chars;
    std::map<std::string, std::string> properties;
    std::vector<LefSite> sites;
    std::vector<LefLayer> layers;
    std::vector<LefVia> vias;
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

} // namespace aegis::parsing
