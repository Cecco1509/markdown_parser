#include "markdown_parser/HandlerRegistry.hpp"

static bool registered = markdown_parser::HandlerRegistry::add(
    "mermaid", {"mermaid"}, [](const std::string &src) {
      return "<div class=\"mermaid\">\n" + src + "</div>\n";
    });
