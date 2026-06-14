#pragma once

#include "markdown_parser/renderer/HtmlRenderer.hpp"
#include <string>
#include <vector>

namespace markdown_parser {

class HtmlRendererFactory {
public:
  static HtmlRenderer create(const std::vector<std::string> &flags);
};

} // namespace markdown_parser
