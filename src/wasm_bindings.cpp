#include "markdown_parser/renderer/HtmlRenderer.hpp"
#include "markdown_parser/renderer/HtmlRendererFactory.hpp"
#include "markdown_parser/parser/parser.hpp"

#include <emscripten/bind.h>
#include <sstream>
#include <string>
#include <vector>

using namespace markdown_parser;

std::string parseMarkdown(const std::string &input) {
    HtmlRenderer hr;
    return parse(input, hr);
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
    return parse(input, hr);
}

EMSCRIPTEN_BINDINGS(markdown_parser) {
    emscripten::function("parseMarkdown", &parseMarkdown);
    emscripten::function("parseMarkdownWithFlags", &parseMarkdownWithFlags);
}
