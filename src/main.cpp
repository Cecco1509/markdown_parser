#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"

#include <iostream>
#include <string>

int main() {
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

    std::cout << "Parsed document with "
              << doc->children.size()
              << " top-level block(s).\n";
    return 0;
}
