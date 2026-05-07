#pragma once

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <vector>

namespace aegis::parsing {

/**
 * A basic DEF-like parser prototype.
 *
 * Reads a simplified line-oriented DEF subset and produces either:
 *   - streaming events via IParserCallbacks (implements IParser)
 *   - a fully-populated LayoutIR via parse_to_layout_ir()
 *
 * Simplified format (one statement per line, # comments):
 *
 *   DESIGN <name>
 *   UNITS <microns>
 *
 *   LAYER <name> <purpose> <order> <color>
 *   RECT  <layer> <x> <y> <width> <height>
 *   POLY  <layer> <x1> <y1> <x2> <y2> ...
 *
 *   DEVICE <name> <type> <key=value>...
 *   PORT   <name> <direction> <net_name> [layer] <x> <y>
 *   NET    <name> <pin_name>... <key=value>...
 *
 *   ANNOTATION <key> <value> [layer] <x> <y>
 *
 *   END DESIGN
 */
class DefParser : public IParser {
public:
    std::string format_name() const override;

    /**
     * Streaming parse entry-point (IParser contract).
     *
     * Emits on_cell / on_net / on_pin / on_geometry / on_progress
     * for supported entities.  Layer / annotation data are not
     * emitted via callbacks (no on_layer / on_annotation hooks exist)
     * but are recorded silently for internal use.
     */
    bool parse(const std::filesystem::path& path,
               IParserCallbacks& callbacks,
               const ICancellationToken& token) override;

    /**
     * Convenience overload that builds a LayoutIR directly.
     *
     * Internally streams through the parser and accumulates
     * everything into a LayoutIR document.
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
