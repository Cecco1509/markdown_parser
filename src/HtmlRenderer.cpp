#include "markdown_parser/HtmlRenderer.hpp"
#include "markdown_parser/BlockNode.hpp"
#include "markdown_parser/InlineNode.hpp"
#include "markdown_parser/Types.hpp"

#include <cassert>

// ── helpers
// ───────────────────────────────────────────────────────────────────

std::string HtmlRenderer::escapeHtml(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

// Characters that are safe to emit verbatim in an href/src attribute.
// Matches cmark's HREF_SAFE table: unreserved URI chars plus reserved chars
// that carry structural meaning in URLs, minus '&' and '"' which are
// HTML-escaped separately.
static bool isHrefSafe(unsigned char c) {
  // clang-format off
  static const bool safe[256] = {
    //       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    /* 0 */ false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    /* 1 */ false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    /* 2 */ false,true, false,true, true, true, false,false,true, true, true, true, true, true, true, true,
    //       sp    !     "     #     $     %     &     '     (     )     *     +     ,     -     .     /
    /* 3 */ true, true, true, true, true, true, true, true, true, true, true, true,false,true, false,true,
    //       0     1     2     3     4     5     6     7     8     9     :     ;     <     =     >     ?
    /* 4 */ true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    //       @     A     B     C     D     E     F     G     H     I     J     K     L     M     N     O
    /* 5 */ true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,true,
    //       P     Q     R     S     T     U     V     W     X     Y     Z     [     \     ]     ^     _
    /* 6 */ false,true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    //       `     a     b     c     d     e     f     g     h     i     j     k     l     m     n     o
    /* 7 */ true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,false,
    //       p     q     r     s     t     u     v     w     x     y     z     {     |     }     ~     DEL
    // 0x80-0xFF: all false (non-ASCII must be percent-encoded)
  };
  // clang-format on
  return safe[c];
}

static const char kHex[] = "0123456789ABCDEF";

std::string HtmlRenderer::escapeUrl(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (unsigned char c : s) {
    if (c == '&') {
      out += "&amp;";
    } else if (isHrefSafe(c)) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

// ── public API
// ────────────────────────────────────────────────────────────────

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
    // Extract first word of the info string as the language tag.
    std::string lang;
    if (!cd.info_string.empty()) {
      auto end = cd.info_string.find_first_of(" \t\n");
      lang = cd.info_string.substr(0, end);
    }
    if (lang.empty())
      out_ += "<pre><code>";
    else
      out_ += "<pre><code class=\"language-" + escapeHtml(lang) + "\">";
    out_ += escapeHtml(node.string_content);
    out_ += "</code></pre>\n";
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
    out_ += escapeHtml(node.literal);
    break;

  case InlineType::SoftBreak:
    out_ += '\n';
    break;

  case InlineType::LineBreak:
    out_ += "<br />\n";
    break;

  case InlineType::Code:
    out_ += "<code>" + escapeHtml(node.literal) + "</code>";
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
    out_ += "<a href=\"" + escapeUrl(ld.destination) + "\"";
    if (ld.title)
      out_ += " title=\"" + escapeHtml(*ld.title) + "\"";
    out_ += ">";
    for (const auto &child : node.children)
      visit(*child);
    out_ += "</a>";
    break;
  }

  case InlineType::Image: {
    const auto &ld = std::get<LinkData>(node.data);
    // Alt text is the plain text of the children (no HTML tags).
    std::string alt;
    for (const auto &child : node.children)
      if (child->type == InlineType::Text)
        alt += child->literal;
    out_ += "<img src=\"" + escapeUrl(ld.destination) + "\"";
    out_ += " alt=\"" + escapeHtml(alt) + "\"";
    if (ld.title)
      out_ += " title=\"" + escapeHtml(*ld.title) + "\"";
    out_ += " />";
    break;
  }
  }
}
