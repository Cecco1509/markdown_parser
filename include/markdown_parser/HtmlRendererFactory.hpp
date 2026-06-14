#pragma once

#include "HtmlRenderer.hpp"
#include <string>
#include <vector>

class HtmlRendererFactory {
public:
  // Create an HtmlRenderer with handlers activated for each named group flag.
  static HtmlRenderer create(const std::vector<std::string> &flags);
};
