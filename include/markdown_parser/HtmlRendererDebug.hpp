#pragma once

#include "HtmlRenderer.hpp"
#include <string>

class HtmlRendererDebug : public HtmlRenderer {
public:
  // Render the Document BlockNode to a complete HTML string.
  std::string render(const BlockNode &root);
};
