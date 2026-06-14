#pragma once

#include "NodeVisitor.hpp"
#include <functional>
#include <string>
#include <unordered_map>

using HandlerFn = std::function<std::string(const std::string &)>;

class HtmlRenderer : public NodeVisitor {
public:
  // Render the Document BlockNode to a complete HTML string.
  std::string render(const BlockNode &root);

  void visit(const BlockNode &) override;
  void visit(const InlineNode &) override;

  // Register a fenced-code handler for a specific lang tag.
  void registerHandler(const std::string &lang, HandlerFn fn);

protected:
  std::string out_;
  bool tight_ = false; // true while inside a tight list

  // Render a fenced code block. Checks registered handlers before default <pre><code>.
  virtual std::string renderFencedCode(const std::string &lang,
                                       const std::string &content);

  static std::string escapeHtml(const std::string &s);
  static std::string escapeUrl(const std::string &s);

private:
  std::unordered_map<std::string, HandlerFn> fenced_handlers_;
};
