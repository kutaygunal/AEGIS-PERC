#pragma once

#include "aegis/parsing/layout_ir.hpp"
#include "aegis/parsing/parser_interface.hpp"

#include <vector>

namespace aegis::parsing {

/**
 * A gate-level Verilog/SystemVerilog netlist parser.
 *
 * Supported MVP subset:
 *   - module / endmodule
 *   - ANSI and non-ANSI port lists
 *   - input / output / inout declarations
 *   - wire / tri / logic / reg declarations
 *   - assign statements
 *   - gate/cell instances with named or positional connectivity
 *
 * Unsupported procedural HDL constructs are skipped when possible so imported
 * customer packages can still normalize connectivity from mixed netlist files.
 */
class VerilogParser : public IParser {
public:
    std::string format_name() const override;

    bool parse(const std::filesystem::path& path,
               IParserCallbacks& callbacks,
               const ICancellationToken& token) override;

    LayoutIR parse_to_layout_ir(const std::filesystem::path& path,
                                const ICancellationToken& token = NullCancellationToken{});

private:
    struct Context {
        LayoutIR ir;
        std::vector<std::string> errors;
    };

    bool parse_file(const std::filesystem::path& path,
                    Context& ctx,
                    IParserCallbacks* callbacks,
                    const ICancellationToken& token);
};

} // namespace aegis::parsing
