#include "markdown_parser/HtmlRendererFactory.hpp"
#include "markdown_parser/HandlerRegistry.hpp"

namespace markdown_parser {

HtmlRenderer HtmlRendererFactory::create(const std::vector<std::string> &flags) {
  HtmlRenderer hr;
  for (const auto &flag : flags)
    for (const auto &alias : HandlerRegistry::getGroup(flag))
      if (auto fn = HandlerRegistry::get(alias))
        hr.registerHandler(alias, fn);
  return hr;
}

} // namespace markdown_parser
