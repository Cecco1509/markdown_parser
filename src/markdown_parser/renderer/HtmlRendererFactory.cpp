#include "markdown_parser/renderer/HtmlRendererFactory.hpp"
#include "markdown_parser/handlers/HandlerRegistry.hpp"

namespace markdown_parser {

HtmlRenderer HtmlRendererFactory::create(const std::vector<std::string> &flags) {
  HtmlRenderer hr;
  for (const auto &flag : flags)
    if (auto fn = HandlerRegistry::getGroupFn(flag))
      for (const auto &alias : HandlerRegistry::getGroup(flag))
        hr.registerHandler(alias, fn);
  return hr;
}

} // namespace markdown_parser
