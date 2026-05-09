#pragma once

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <vector>

namespace aegis::parsing {

/**
 * A SPICE-like netlist parser prototype.
 *
 * Reads a reduced SPICE subset and produces either:
 *   - streaming events via IParserCallbacks (implements IParser)
 *   - a fully-populated LayoutIR via parse_to_layout_ir()
 *
 * Supported MVP subset:
 *   .SUBCKT <name> <node>... [PARAMS: key=value ...]
 *   .ENDS [<name>]
 *   .INCLUDE / .INC <file>
 *   .LIB <file> [section]
 *   .MODEL <name> <type> [key=value]...
 *   .PARAM key=value ...
 *   .GLOBAL <net>...
 *   R/C/L/M/D/Q/V/I primitive elements with key=value properties
 *   X<name> ... <subckt> [key=value]... subcircuit instances
 *
 * Unsupported directives are surfaced as explicit diagnostics.
 *
 * Comments:  * at line start, ; inline, $ inline, // inline
 * Continuation: + at line start continues previous line.
 */
class SpiceParser : public IParser {
public:
    std::string format_name() const override;

    /**
     * Streaming parse entry-point (IParser contract).
     */
    bool parse(const std::filesystem::path& path,
               IParserCallbacks& callbacks,
               const ICancellationToken& token) override;

    /**
     * Convenience overload that builds a LayoutIR directly.
     */
    LayoutIR parse_to_layout_ir(const std::filesystem::path& path,
                                 const ICancellationToken& token = NullCancellationToken{});

private:
    struct Context {
        LayoutIR    ir;
        std::vector<std::string> errors;
    };

    bool parse_file(const std::filesystem::path& path,
                    Context& ctx,
                    IParserCallbacks* callbacks,
                    const ICancellationToken& token);
};

} // namespace aegis::parsing
