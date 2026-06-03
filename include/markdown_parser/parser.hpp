#pragma once

#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/JsonRenderer.hpp"
#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/SpineHandler.hpp"

#include <sstream>
#include <string>

namespace markdown_parser {

enum class OutputFormat { Html, Json };

inline std::string parse(const std::string &source,
                         OutputFormat fmt = OutputFormat::Html) {
  PreScanner scanner;
  InlineParser inline_parser;
  SpineHandler spine(scanner, inline_parser);

  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    line += "\n";
     spine.processLine(line);
  }
  spine.finalize();

  auto doc = spine.releaseDocument();

  if (fmt == OutputFormat::Json) {
    JsonRenderer jr;
    return jr.render(*doc);
  }

  HtmlRenderer hr;
  return hr.render(*doc);
}

} // namespace markdown_parser
