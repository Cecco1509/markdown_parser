#pragma once

#include "HandlerRegistry.hpp"
#include "NodeVisitor.hpp"
#include <string>
#include <unordered_map>

namespace markdown_parser {

class HtmlRenderer : public NodeVisitor {
public:
  std::string render(const BlockNode &root);

  void visit(const BlockNode &) override;
  void visit(const InlineNode &) override;

  void registerHandler(const std::string &lang, HandlerFn fn);

protected:
  std::string out_;
  bool tight_ = false;

  virtual std::string renderFencedCode(const std::string &lang,
                                       const std::string &content);

private:
  std::unordered_map<std::string, HandlerFn> fenced_handlers_;
};

} // namespace markdown_parser
