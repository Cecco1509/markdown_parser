#include "markdown_parser/BlockNode.hpp"
#include "markdown_parser/HtmlRendererDebug.hpp"

namespace markdown_parser {

const char DEBUGWHITE_SPACE = '#';

std::string HtmlRendererDebug::render(const BlockNode &root) {
  out_.clear();
  tight_ = false;
  this->visit(root);

  // replace every space with a visible character so we can see where they are
  // in the output
  for (int i = 0; i < out_.size(); ++i) {
    if (out_[i] == ' ')
      out_[i] = DEBUGWHITE_SPACE;
  }

  return out_;
}

} // namespace markdown_parser
