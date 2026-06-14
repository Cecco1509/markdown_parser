#pragma once

#include "markdown_parser/parser/InlineParser.hpp"
#include "markdown_parser/parser/SpineHandler.hpp"
#include "markdown_parser/renderer/renderer_concept.hpp"

#include <sstream>
#include <string>

namespace markdown_parser {

template <Renderer R>
std::string parse(const std::string &source, R &renderer,
                  bool debug = false) {
  InlineParser inline_parser;
  SpineHandler spine(inline_parser, debug);

  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    line += '\n';
    spine.processLine(line);
  }
  spine.finalize();

  auto doc = spine.releaseDocument();
  return renderer.render(*doc);
}

} // namespace markdown_parser
