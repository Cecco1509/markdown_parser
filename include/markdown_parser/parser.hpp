#pragma once

#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"

#include <string>
#include <sstream>

namespace markdown_parser {

// Top-level entry point: parse Markdown source and return rendered HTML.
// HTML serialization is not yet implemented; returns empty string as placeholder.
inline std::string parse(const std::string& source) {
    PreScanner   scanner;
    InlineParser inline_parser;
    SpineHandler spine(scanner, inline_parser);

    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        line += '\n';
        spine.processLine(line);
    }
    spine.finalize();

    // TODO: serialize spine.releaseDocument() to HTML
    return "";
}

} // namespace markdown_parser
