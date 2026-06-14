#include "markdown_parser/handlers/HandlerRegistry.hpp"

static bool registered = markdown_parser::HandlerRegistry::add(
    "math", {"math", "latex", "tex"}, [](const std::string &src) {
      return "<div class=\"math\">\n" + src + "</div>\n";
    });
