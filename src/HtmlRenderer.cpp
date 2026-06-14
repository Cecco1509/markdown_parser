#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/BlockNode.hpp"
#include "markdown_parser/InlineNode.hpp"
#include "markdown_parser/Types.hpp"
#include "markdown_parser/string_utils.hpp"

#include <cassert>
#include <stdexcept>

namespace markdown_parser {

// ── public API
// ────────────────────────────────────────────────────────────────

void HtmlRenderer::registerHandler(const std::string &lang, HandlerFn fn) {
  if (!fn) throw std::invalid_argument("null handler for lang: " + lang);
  fenced_handlers_[lang] = std::move(fn);
}

std::string HtmlRenderer::renderFencedCode(const std::string &lang,
                                           const std::string &content) {
  auto it = fenced_handlers_.find(lang);
  if (it != fenced_handlers_.end())
    return it->second(content);
  if (lang.empty())
    return "<pre><code>" + string_utils::escapeHtml(content) + "</code></pre>\n";
  return "<pre><code class=\"language-" + string_utils::escapeHtml(lang) + "\">" +
         string_utils::escapeHtml(content) + "</code></pre>\n";
}

std::string HtmlRenderer::render(const BlockNode &root) {
  out_.clear();
  tight_ = false;
  visit(root);
  return out_;
}

// ── BlockNode
// ─────────────────────────────────────────────────────────────────

void HtmlRenderer::visit(const BlockNode &node) {
  switch (node.type) {

  case NodeType::Document:
    for (const auto &child : node.children)
      visit(*child);
    break;

  case NodeType::Paragraph:
    if (tight_) {
      // In tight lists paragraphs emit only their inline content.
      for (const auto &il : node.inline_children)
        visit(*il);
    } else {
      out_ += "<p>";
      for (const auto &il : node.inline_children)
        visit(*il);
      out_ += "</p>\n";
    }
    break;

  case NodeType::Heading: {
    const auto &hd = std::get<HeadingData>(node.data);
    out_ += "<h" + std::to_string(hd.level) + ">";
    for (const auto &il : node.inline_children)
      visit(*il);
    out_ += "</h" + std::to_string(hd.level) + ">\n";
    break;
  }

  case NodeType::ThematicBreak:
    out_ += !out_.empty() && out_.back() != '\n' ? "\n<hr />\n" : "<hr />\n";
    break;

  case NodeType::BlockQuote: {
    out_ += "<blockquote>\n";
    bool prev = tight_;
    tight_ = false;
    for (const auto &child : node.children)
      visit(*child);
    tight_ = prev;
    out_ += "</blockquote>\n";
    break;
  }

  case NodeType::List: {
    const auto &ld = std::get<ListData>(node.data);
    bool prev = tight_;
    tight_ = ld.tight;

    if (ld.list_type == ListType::Ordered) {
      if (ld.start != 1)
        out_ += "<ol start=\"" + std::to_string(ld.start) + "\">\n";
      else
        out_ += "<ol>\n";
    } else {
      out_ += "<ul>\n";
    }

    for (const auto &child : node.children)
      visit(*child);

    out_ += (ld.list_type == ListType::Ordered) ? "</ol>\n" : "</ul>\n";
    tight_ = prev;
    break;
  }

  case NodeType::Item:
    if (!tight_) {
      if (node.children.empty()) {
        out_ += "<li></li>\n";
        break;
      }
      out_ += "<li>\n";
      for (const auto &child : node.children)
        visit(*child);
    } else {
      bool first_is_block = !node.children.empty() &&
                            node.children[0]->type != NodeType::Paragraph;
      out_ += first_is_block ? "<li>\n" : "<li>";
      for (size_t i = 0; i < node.children.size(); ++i) {
        const auto &child = *node.children[i];
        if (i == 0 && child.type == NodeType::Paragraph) {
          // Render first paragraph's inline content right after <li>
          for (const auto &il : child.inline_children)
            visit(*il);
          if (node.children.size() > 1)
            out_ += '\n';
        } else {
          visit(child);
        }
      }
    }
    out_ += "</li>\n";
    break;

  case NodeType::CodeBlock: {
    const auto &cd = std::get<CodeBlockData>(node.data);
    std::string lang;
    if (!cd.info_string.empty()) {
      auto end = cd.info_string.find_first_of(" \t\n");
      lang = cd.info_string.substr(0, end);
    }
    out_ += renderFencedCode(lang, node.string_content);
    break;
  }

  case NodeType::HtmlBlock:
    out_ += node.string_content;
    break;
  }
}

// ── InlineNode
// ────────────────────────────────────────────────────────────────

void HtmlRenderer::visit(const InlineNode &node) {
  switch (node.type) {

  case InlineType::Text:
    out_ += string_utils::escapeHtml(node.literal);
    break;

  case InlineType::SoftBreak:
    out_ += '\n';
    break;

  case InlineType::LineBreak:
    out_ += "<br />\n";
    break;

  case InlineType::Code:
    out_ += "<code>" + string_utils::escapeHtml(node.literal) + "</code>";
    break;

  case InlineType::HtmlInline:
    out_ += node.literal;
    break;

  case InlineType::Emph:
    out_ += "<em>";
    for (const auto &child : node.children)
      visit(*child);
    out_ += "</em>";
    break;

  case InlineType::Strong:
    out_ += "<strong>";
    for (const auto &child : node.children)
      visit(*child);
    out_ += "</strong>";
    break;

  case InlineType::Link: {
    const auto &ld = std::get<LinkData>(node.data);
    out_ += "<a href=\"" + string_utils::escapeUrl(ld.destination) + "\"";
    if (ld.title)
      out_ += " title=\"" + string_utils::escapeHtml(*ld.title) + "\"";
    out_ += ">";
    for (const auto &child : node.children)
      visit(*child);
    out_ += "</a>";
    break;
  }

  case InlineType::Image: {
    const auto &ld = std::get<LinkData>(node.data);
    // Alt text is the plain-text content of the whole subtree (no HTML tags).
    std::string alt;
    std::function<void(const InlineNode &)> collectAlt = [&](const InlineNode &n) {
      if (n.type == InlineType::Text || n.type == InlineType::Code)
        alt += n.literal;
      for (const auto &c : n.children)
        collectAlt(*c);
    };
    for (const auto &child : node.children)
      collectAlt(*child);
    out_ += "<img src=\"" + string_utils::escapeUrl(ld.destination) + "\"";
    out_ += " alt=\"" + string_utils::escapeHtml(alt) + "\"";
    if (ld.title)
      out_ += " title=\"" + string_utils::escapeHtml(*ld.title) + "\"";
    out_ += " />";
    break;
  }
  }
}

} // namespace markdown_parser
