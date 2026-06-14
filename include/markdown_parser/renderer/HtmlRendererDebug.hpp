#pragma once

#include "markdown_parser/renderer/HtmlRenderer.hpp"
#include <string>

namespace markdown_parser {

class HtmlRendererDebug : public HtmlRenderer {
public:
  std::string render(const BlockNode &root);
};

} // namespace markdown_parser
