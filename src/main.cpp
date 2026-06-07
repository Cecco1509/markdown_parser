#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/JsonRenderer.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
  // Usage: program [--json] <input_file>
  bool json_mode = false;
  std::string input_file;

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--json") {
      json_mode = true;
    } else {
      input_file = argv[i];
    }
  }

  if (input_file.empty()) {
    std::cerr << "Usage: " << argv[0] << " [--json] <input_file>\n";
    return 1;
  }

  std::ifstream file(input_file);
  if (!file) {
    std::cerr << "Error: cannot open file '" << input_file << "'\n";
    return 1;
  }

  InlineParser inline_parser;
  SpineHandler spine(inline_parser, /*debug=*/true);

  std::string line;
  while (std::getline(file, line)) {
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
