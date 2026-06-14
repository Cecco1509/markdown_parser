#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/HtmlRendererFactory.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <emscripten/bind.h>
#include <sstream>
#include <string>
#include <vector>

using namespace markdown_parser;

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

// flags_csv: comma-separated flag names, e.g. "mermaid,math"
std::string parseMarkdownWithFlags(const std::string &input,
                                   const std::string &flags_csv) {
    std::vector<std::string> flags;
    std::istringstream ss(flags_csv);
    std::string token;
    while (std::getline(ss, token, ','))
        if (!token.empty())
            flags.push_back(token);

    HtmlRenderer hr = HtmlRendererFactory::create(flags);
    return runParser(input, hr);
}

EMSCRIPTEN_BINDINGS(markdown_parser) {
    emscripten::function("parseMarkdown", &parseMarkdown);
    emscripten::function("parseMarkdownWithFlags", &parseMarkdownWithFlags);
}
