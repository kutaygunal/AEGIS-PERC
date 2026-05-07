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
 * Supported subset:
 *   .SUBCKT <name> <node>...
 *   .ENDS [<name>]
 *   R<name> <node1> <node2> <value> [key=value]...
 *   C<name> <node1> <node2> <value> [key=value]...
 *   M<name> <drain> <gate> <source> <bulk> <model> [key=value]...
 *   V<name> <node+> <node-> <value> [key=value]...
 *   I<name> <node+> <node-> <value> [key=value]...
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
