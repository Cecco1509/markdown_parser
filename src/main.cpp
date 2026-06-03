#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/JsonRenderer.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Optional flag: --json  (default is HTML)
    bool json_mode = (argc >= 2 && std::string(argv[1]) == "--json");

    PreScanner   scanner;
    InlineParser inline_parser;
    SpineHandler spine(scanner, inline_parser);

    std::string line;
    while (std::getline(std::cin, line)) {
        line += '\n';
        spine.processLine(line);
    }
    spine.finalize();

    auto doc = spine.releaseDocument();

    if (json_mode) {
        JsonRenderer jr;
        std::cout << jr.render(*doc) << '\n';
    } else {
        HtmlRenderer hr;
        std::cout << hr.render(*doc);
    }

    return 0;
}
