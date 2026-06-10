#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <emscripten/bind.h>
#include <sstream>
#include <string>

std::string parseMarkdown(const std::string& input) {
    InlineParser inline_parser;
    SpineHandler spine(inline_parser, false);

    std::istringstream stream(input);
    std::string line;
    while (std::getline(stream, line)) {
        line += '\n';
        spine.processLine(line);
    }
    spine.finalize();

    auto doc = spine.releaseDocument();
    HtmlRenderer hr;
    return hr.render(*doc);
}

EMSCRIPTEN_BINDINGS(markdown_parser) {
    emscripten::function("parseMarkdown", &parseMarkdown);
}
