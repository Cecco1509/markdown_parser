#include "markdown_parser/HtmlRendererDebug.hpp"

namespace markdown_parser {

std::string HtmlRendererDebug::render(const BlockNode &root) {
  std::string result = HtmlRenderer::render(root);
  for (char &c : result)
    if (c == ' ') c = '#';
  return result;
}

} // namespace markdown_parser
