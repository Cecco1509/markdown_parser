#include "markdown_parser/HandlerRegistry.hpp"

static bool registered = HandlerRegistry::add(
    "math",
    {"math", "latex", "tex"},
    [](const std::string &src) {
        return "<div class=\"math\">\n" + src + "</div>\n";
    }
);
