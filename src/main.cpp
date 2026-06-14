#include "markdown_parser/renderer/HtmlRenderer.hpp"
#include "markdown_parser/renderer/HtmlRendererDebug.hpp"
#include "markdown_parser/renderer/HtmlRendererFactory.hpp"
#include "markdown_parser/renderer/JsonRenderer.hpp"
#include "markdown_parser/parser/parser.hpp"

#include <fstream>
#include <iostream>
#include <string>

using namespace markdown_parser;

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

  std::string source((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  if (json_mode) {
    JsonRenderer jr;
    std::cout << parse(source, jr, debug_mode) << '\n';
  } else if (debug_mode) {
    HtmlRendererDebug hr;
    std::cout << parse(source, hr, debug_mode);
  } else {
    HtmlRenderer hr = HtmlRendererFactory::create(active_flags);
    std::cout << parse(source, hr);
  }

  return 0;
}
