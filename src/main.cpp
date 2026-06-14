#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/HtmlRendererDebug.hpp"
#include "markdown_parser/HtmlRendererFactory.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/JsonRenderer.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
  bool json_mode    = false;
  bool debug_mode   = false;
  std::string input_file;
  std::vector<std::string> active_flags;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--json") {
      json_mode = true;
    } else if (arg == "--debug") {
      debug_mode = true;
    } else if (arg == "--parse-mermaid") {
      active_flags.push_back("mermaid");
    } else if (arg == "--parse-math") {
      active_flags.push_back("math");
    } else {
      input_file = arg;
    }
  }

  if (input_file.empty()) {
    std::cerr << "Usage: " << argv[0]
              << " [--json] [--debug] [--parse-mermaid] [--parse-math]"
              << " <input_file>\n";
    return 1;
  }

  std::ifstream file(input_file);
  if (!file) {
    std::cerr << "Error: cannot open file '" << input_file << "'\n";
    return 1;
  }

  InlineParser inline_parser;
  SpineHandler spine(inline_parser, /*debug=*/debug_mode);

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
  } else if (debug_mode) {
    HtmlRendererDebug hr;
    std::cout << hr.render(*doc);
  } else {
    HtmlRenderer hr = HtmlRendererFactory::create(active_flags);
    std::cout << hr.render(*doc);
  }

  return 0;
}
