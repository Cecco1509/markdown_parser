#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/HtmlRendererFactory.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <emscripten/bind.h>
#include <sstream>
#include <string>
#include <vector>

static std::string runParser(const std::string &input, HtmlRenderer &hr) {
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
    return hr.render(*doc);
}

std::string parseMarkdown(const std::string &input) {
    HtmlRenderer hr;
    return runParser(input, hr);
}

std::string parseMarkdownWithFlags(const std::string &input,
                                   const std::vector<std::string> &flags) {
    HtmlRenderer hr = HtmlRendererFactory::create(flags);
    return runParser(input, hr);
}

EMSCRIPTEN_BINDINGS(markdown_parser) {
    emscripten::function("parseMarkdown", &parseMarkdown);
    emscripten::function("parseMarkdownWithFlags", &parseMarkdownWithFlags);
    emscripten::register_vector<std::string>("VectorString");
}
