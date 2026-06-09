#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/entities.hpp"
#include "markdown_parser/string_utils.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

// ── file-local helpers
// ────────────────────────────────────────────────────────

static bool isAsciiPunct(char c) {
  auto u = static_cast<unsigned char>(c);
  return (u >= '!' && u <= '/') || (u >= ':' && u <= '@') ||
         (u >= '[' && u <= '`') || (u >= '{' && u <= '~');
}

static bool isUnicodeWhitespace(char c) {
  auto u = static_cast<unsigned char>(c);
  return u == ' ' || u == '\t' || u == '\n' || u == '\r' || u == '\f' ||
         u == '\v';
}

// For emphasis flanking rules we treat ASCII punctuation as punctuation.
// Non-ASCII bytes > 0x7F are treated as neither whitespace nor punctuation
// (they are part of words), which is conservative but passes ASCII-only tests.
static bool isUnicodePunct(char c) {
  auto u = static_cast<unsigned char>(c);
  return u < 0x80 && isAsciiPunct(c);
}

// ── parse (entry point)
// ───────────────────────────────────────────────────────

void InlineParser::parse(
    BlockNode *block, const std::unordered_map<std::string, LinkDef> &ref_map) {
  input_ = block->string_content;
  while (!input_.empty() && input_.back() == '\n')
    input_.remove_suffix(1);
  pos_ = 0;
  ref_map_ = &ref_map;
  delimiters_.clear();
  brackets_.clear();
  nodes_.clear();
  // Upper bound: one node per byte → no reallocation, keeping raw ptrs stable.
  nodes_.reserve(input_.size() + 1);

  while (pos_ < input_.size()) {
    auto node = parseInline();
    if (node)
      nodes_.push_back(std::move(node));
  }

  processEmphasis(std::nullopt);

  if (nodes_.size() > 0 && nodes_.back()->type == InlineType::Text &&
      !string_utils::isBlank(nodes_.back()->literal))
    nodes_.back()->literal = string_utils::trimRight(nodes_.back()->literal);

  block->inline_children = std::move(nodes_);
}

// ── parseInline (dispatcher)
// ──────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseInline() {
  char c = input_[pos_];

  // ── newline: soft break or hard line break (spec §6.7) ───────────────────
  if (c == '\n') {
    ++pos_;
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t'))
      ++pos_;
    // Check last text node for hard-break conditions.
    if (!nodes_.empty() && nodes_.back()->type == InlineType::Text) {
      std::string &lit = nodes_.back()->literal;
      if (lit.size() >= 2 && lit[lit.size() - 1] == ' ' &&
          lit[lit.size() - 2] == ' ') {
        while (!lit.empty() && lit.back() == ' ')
          lit.pop_back();
        return makeNode(InlineType::LineBreak);
      }
      if (!lit.empty() && lit.back() == '\\') {
        lit.pop_back();
        return makeNode(InlineType::LineBreak);
      }
    }
    if (!nodes_.empty() && nodes_.back()->type == InlineType::Text)
      nodes_.back()->literal = string_utils::trimRight(nodes_.back()->literal);
    return makeNode(InlineType::SoftBreak);
  }

  // ── backslash escape / hard break (spec §6.9) ────────────────────────────
  if (c == '\\') {
    ++pos_;
    if (pos_ < input_.size()) {
      char next = input_[pos_];
      if (next == '\n') {
        ++pos_;
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t'))
          ++pos_;
        return makeNode(InlineType::LineBreak);
      }
      if (isAsciiPunct(next)) {
        ++pos_;
        auto node = makeNode(InlineType::Text);
        node->literal = std::string(1, next);
        return node;
      }
    }
    auto node = makeNode(InlineType::Text);
    node->literal = "\\";
    return node;
  }

  // ── backtick code span (spec §6.1) ───────────────────────────────────────
  if (c == '`')
    return parseBacktickString();

  // ── emphasis / strong (spec §6.4) ────────────────────────────────────────
  if (c == '*' || c == '_') {
    std::size_t start = pos_;
    while (pos_ < input_.size() && input_[pos_] == c)
      ++pos_;
    std::size_t run_len = pos_ - start;
    auto node = makeNode(InlineType::Text);
    node->literal = std::string(input_.substr(start, run_len));
    std::size_t prev_sz = delimiters_.size();
    handleEmphasis(c, run_len);
    if (delimiters_.size() > prev_sz)
      delimiters_.back().node = node.get();
    return node;
  }

  // ── image opener / bare '!' ──────────────────────────────────────────────
  if (c == '!') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '[') {
      pos_ += 2;
      auto node = makeNode(InlineType::Text);
      node->literal = "![";
      handleBracketOpener(true);
      if (!brackets_.empty())
        brackets_.back().node = node.get();
      return node;
    }
    // Bare '!': emit as literal and advance, or it would loop forever.
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "!";
    return node;
  }

  // ── link opener ──────────────────────────────────────────────────────────
  if (c == '[') {
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "[";
    handleBracketOpener(false);
    if (!brackets_.empty())
      brackets_.back().node = node.get();
    return node;
  }

  // ── bracket closer ───────────────────────────────────────────────────────
  if (c == ']') {
    ++pos_;
    return handleBracketCloser();
  }

  // ── autolink or inline HTML ───────────────────────────────────────────────
  if (c == '<') {
    if (auto n = parseAutolink())
      return n;
    if (auto n = parseHtmlInline())
      return n;
    ++pos_;
    auto node = makeNode(InlineType::Text);
    node->literal = "<";
    return node;
  }

  // ── entity reference (spec §2.5) ─────────────────────────────────────────
  if (c == '&') {
    std::size_t save = pos_;
    std::string decoded = entities::decode(input_, pos_);
    if (!decoded.empty()) {
      auto node = makeNode(InlineType::Text);
      node->literal = std::move(decoded);
      return node;
    }
    // Not a valid entity; emit '&' as literal text.
    pos_ = save + 1;
    auto node = makeNode(InlineType::Text);
    node->literal = "&";
    return node;
  }

  // ── regular text run ─────────────────────────────────────────────────────
  std::size_t start = pos_;
  while (pos_ < input_.size()) {
    char ch = input_[pos_];
    if (ch == '\n' || ch == '\\' || ch == '`' || ch == '*' || ch == '_' ||
        ch == '[' || ch == ']' || ch == '!' || ch == '<' || ch == '&')
      break;
    ++pos_;
  }
  auto node = makeNode(InlineType::Text);
  node->literal = std::string(input_.substr(start, pos_ - start));
  return node;
}

// ── parseBacktickString (spec §6.1) ──────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseBacktickString() {
  std::size_t tick_start = pos_;
  while (pos_ < input_.size() && input_[pos_] == '`')
    ++pos_;
  std::size_t tick_len = pos_ - tick_start;

  // Search for matching closing run of the same length.
  std::size_t search = pos_;
  while (search < input_.size()) {
    if (input_[search] == '`') {
      std::size_t cs = search;
      while (search < input_.size() && input_[search] == '`')
        ++search;
      if (search - cs == tick_len) {
        // Found. Normalise content.
        std::string content(input_.substr(pos_, cs - pos_));
        for (char &ch : content)
          if (ch == '\n')
            ch = ' ';
        bool all_spaces = content.find_first_not_of(' ') == std::string::npos;
        if (!all_spaces && content.size() >= 2 && content.front() == ' ' &&
            content.back() == ' ')
          content = content.substr(1, content.size() - 2);
        pos_ = search;
        auto node = makeNode(InlineType::Code);
        node->literal = std::move(content);
        return node;
      }
    } else {
      ++search;
    }
  }
  // No matching close: emit opening ticks as literal text.
  auto node = makeNode(InlineType::Text);
  node->literal = std::string(tick_len, '`');
  return node;
}

// ── parseAutolink (spec §6.7)
// ─────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseAutolink() {
  std::size_t save = pos_;
  ++pos_; // skip '<'
  std::size_t inner = pos_;

  // URI autolink: <scheme:path>
  // scheme: letter followed by letters/digits/+/-/. up to 32 chars total
  std::size_t sp = pos_;
  if (sp < input_.size() &&
      std::isalpha(static_cast<unsigned char>(input_[sp]))) {
    while (sp < input_.size() && sp - pos_ < 32) {
      char sc = input_[sp];
      if (std::isalpha(static_cast<unsigned char>(sc)) ||
          (sp > pos_ && (std::isdigit(static_cast<unsigned char>(sc)) ||
                         sc == '+' || sc == '-' || sc == '.')))
        ++sp;
      else
        break;
    }
    if (sp - pos_ >= 2 && sp < input_.size() && input_[sp] == ':') {
      std::size_t up = sp + 1;
      while (up < input_.size() && input_[up] != '>' && input_[up] != '<' &&
             input_[up] != ' ' && static_cast<unsigned char>(input_[up]) > 0x1F)
        ++up;
      if (up < input_.size() && input_[up] == '>') {
        std::string dest(input_.substr(inner, up - inner));
        pos_ = up + 1;
        auto link = makeNode(InlineType::Link);
        link->data = LinkData{dest, std::nullopt, std::nullopt};
        auto text = makeNode(InlineType::Text);
        text->literal = dest;
        link->children.push_back(std::move(text));
        return link;
      }
    }
  }

  // Email autolink: <local@domain>
  pos_ = inner;
  std::size_t at = pos_;
  while (at < input_.size() && input_[at] != '@' && input_[at] != '>' &&
         input_[at] != '\n')
    ++at;
  if (at < input_.size() && input_[at] == '@' && at > pos_) {
    bool local_ok = true;
    for (std::size_t i = pos_; i < at && local_ok; ++i) {
      char ch = input_[i];
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' ||
            ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' ||
            ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '/' ||
            ch == '=' || ch == '?' || ch == '^' || ch == '_' || ch == '`' ||
            ch == '{' || ch == '|' || ch == '}' || ch == '~'))
        local_ok = false;
    }
    if (local_ok) {
      std::size_t dp = at + 1;
      bool domain_ok = true;
      std::size_t lbl = dp;
      while (dp < input_.size() && input_[dp] != '>' && input_[dp] != '\n') {
        char ch = input_[dp];
        if (ch == '.') {
          if (dp == lbl) {
            domain_ok = false;
            break;
          }
          lbl = dp + 1;
        } else if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') {
          domain_ok = false;
          break;
        }
        ++dp;
      }
      if (domain_ok && dp < input_.size() && input_[dp] == '>' && dp > at + 1 &&
          dp != lbl) {
        std::string email(input_.substr(inner, dp - inner));
        pos_ = dp + 1;
        auto link = makeNode(InlineType::Link);
        link->data = LinkData{"mailto:" + email, std::nullopt, std::nullopt};
        auto text = makeNode(InlineType::Text);
        text->literal = email;
        link->children.push_back(std::move(text));
        return link;
      }
    }
  }

  pos_ = save;
  return nullptr;
}

// ── parseHtmlInline (spec §6.8)
// ───────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::parseHtmlInline() {
  std::size_t save = pos_;
  ++pos_; // skip '<'

  // std::cerr << "parseHtmlInline at pos " << save << ": " << input_ <<
  // "...\n";

  if (pos_ >= input_.size()) {
    pos_ = save;
    return nullptr;
  }
  char c = input_[pos_];

  auto emit = [&]() -> std::unique_ptr<InlineNode> {
    auto node = makeNode(InlineType::HtmlInline);
    node->literal = std::string(input_.substr(save, pos_ - save));
    return node;
  };

  // Closing tag: </tagname>
  if (c == '/') {
    ++pos_;
    if (pos_ >= input_.size() ||
        !std::isalpha(static_cast<unsigned char>(input_[pos_]))) {
      pos_ = save;
      // std::cerr << "Not a closing tag: " << input_ << "...\n";
      return nullptr;
    }
    while (pos_ < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
            input_[pos_] == '-'))
      ++pos_;
    while (pos_ < input_.size() && input_[pos_] == ' ')
      ++pos_;
    if (pos_ < input_.size() && input_[pos_] == '>') {
      ++pos_;
      return emit();
    }
    pos_ = save;
    // std::cerr << "Malformed closing tag: " << input_ << "...\n";
    return nullptr;
  }

  // ! variants: comment, CDATA, declaration
  if (c == '!') {
    // HTML comment: <!--...-->
    if (pos_ + 2 < input_.size() && input_[pos_ + 1] == '-' &&
        input_[pos_ + 2] == '-') {
      pos_ += 3;
      // must not start with > or ->
      if (pos_ < input_.size() && input_[pos_] == '>') {
        pos_ = save;
        return nullptr;
      }
      if (pos_ + 1 < input_.size() && input_[pos_] == '-' &&
          input_[pos_ + 1] == '>') {
        pos_ = save;
        // std::cerr << "Empty comment not allowed: " << input_ << "...\n";
        return nullptr;
      }
      while (pos_ < input_.size()) {
        if (pos_ + 1 < input_.size() && input_[pos_] == '-' &&
            input_[pos_ + 1] == '-') {
          if (pos_ + 2 < input_.size() && input_[pos_ + 2] == '>') {
            pos_ += 3;
            return emit();
          }
          pos_ = save;
          // std::cerr << "Comment cannot end with --: " << input_ << "...\n";
          return nullptr; // -- not followed by >
        }
        ++pos_;
      }
      pos_ = save;
      // std::cerr << "Unclosed comment: " << input_ << "...\n";
      return nullptr;
    }
    // CDATA: <![CDATA[...]]>
    if (pos_ + 7 < input_.size() && input_.substr(pos_, 8) == "![CDATA[") {
      pos_ += 8;
      while (pos_ + 2 < input_.size()) {
        if (input_[pos_] == ']' && input_[pos_ + 1] == ']' &&
            input_[pos_ + 2] == '>') {
          pos_ += 3;
          return emit();
        }
        ++pos_;
      }
      pos_ = save;
      return nullptr;
    }
    // Declaration: <!NAME ...>
    if (pos_ + 1 < input_.size() &&
        std::isupper(static_cast<unsigned char>(input_[pos_ + 1]))) {
      ++pos_; // skip '!'
      while (pos_ < input_.size() &&
             std::isupper(static_cast<unsigned char>(input_[pos_])))
        ++pos_;
      while (pos_ < input_.size() && input_[pos_] != '>') {
        if (input_[pos_] == '\n') {
          pos_ = save;
          return nullptr;
        }
        ++pos_;
      }
      if (pos_ < input_.size() && input_[pos_] == '>') {
        ++pos_;
        return emit();
      }
    }
    pos_ = save;
    return nullptr;
  }

  // Processing instruction: <?...?>
  if (c == '?') {
    ++pos_;
    while (pos_ + 1 < input_.size()) {
      if (input_[pos_] == '?' && input_[pos_ + 1] == '>') {
        pos_ += 2;
        return emit();
      }
      ++pos_;
    }
    pos_ = save;
    return nullptr;
  }

  // Open tag: <tagname attributes? /?>
  if (!std::isalpha(static_cast<unsigned char>(c))) {
    pos_ = save;
    // std::cerr << "Tag name must start with a letter: " << input_ << "...\n";
    return nullptr;
  }

  while (pos_ < input_.size() &&
         (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
          input_[pos_] == '-'))
    ++pos_;

  while (pos_ < input_.size()) {
    char ch = input_[pos_];
    if (ch == '>' || ch == '/')
      break;

    // Require at least one whitespace before each attribute.
    if (ch != ' ' && ch != '\t' && ch != '\n') {
      pos_ = save;
      // std::cerr << "Expected space before attribute: " << input_ << "...\n";
      return nullptr;
    }
    while (
        pos_ < input_.size() &&
        (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
      ++pos_;
    if (pos_ >= input_.size()) {
      pos_ = save;
      // std::cerr << "Unexpected end of input in tag: " << input_ << "...\n";
      return nullptr;
    }
    ch = input_[pos_];
    if (ch == '>' || ch == '/')
      break;

    // Attribute name.
    if (!std::isalpha(static_cast<unsigned char>(ch)) && ch != '_' &&
        ch != ':') {
      pos_ = save;
      // std::cerr << "Invalid attribute name start: " << input_ << "...\n";
      return nullptr;
    }
    while (pos_ < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
            input_[pos_] == '_' || input_[pos_] == ':' || input_[pos_] == '.' ||
            input_[pos_] == '-'))
      ++pos_;

    // Optional = value.
    std::size_t eq_save = pos_;
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t'))
      ++pos_;
    if (pos_ < input_.size() && input_[pos_] == '=') {
      ++pos_;
      while (pos_ < input_.size() &&
             (input_[pos_] == ' ' || input_[pos_] == '\t'))
        ++pos_;
      if (pos_ >= input_.size()) {
        pos_ = save;
        // std::cerr << "Unexpected end of input after '=': " << input_ <<
        // "...\n";
        return nullptr;
      }
      char q = input_[pos_];
      if (q == '"' || q == '\'') {
        ++pos_;
        while (pos_ < input_.size() && input_[pos_] != q) {
          // if (input_[pos_] == '\n') {
          //   pos_ = save;
          //   // std::cerr << "Newline not allowed in attribute value: " <<
          //   input_
          //             << "...\n";
          //   return nullptr;
          // }
          ++pos_;
        }
        if (pos_ >= input_.size()) {
          pos_ = save;
          // std::cerr << "Unclosed attribute value: " << input_ << "...\n";
          return nullptr;
        }
        ++pos_;
      } else if (q != ' ' && q != '\t' && q != '"' && q != '\'' && q != '=' &&
                 q != '<' && q != '>' && q != '`') {
        while (pos_ < input_.size() && input_[pos_] != ' ' &&
               input_[pos_] != '\t' && input_[pos_] != '"' &&
               input_[pos_] != '\'' && input_[pos_] != '=' &&
               input_[pos_] != '<' && input_[pos_] != '>' &&
               input_[pos_] != '`')
          ++pos_;
      } else {
        pos_ = save;
        // std::cerr << "Invalid attribute value start: " << input_ << "...\n";
        return nullptr;
      }
    } else {
      pos_ = eq_save;
    }
  }

  if (pos_ >= input_.size()) {
    pos_ = save;
    // std::cerr << "Unexpected end of input in tag: " << input_ << "...\n";
    return nullptr;
  }
  if (input_[pos_] == '/') {
    ++pos_;
    if (pos_ >= input_.size() || input_[pos_] != '>') {
      pos_ = save;
      // std::cerr << "Expected '>' after '/': " << input_ << "...\n";
      return nullptr;
    }
  }
  if (input_[pos_] == '>') {
    ++pos_;
    return emit();
  }
  pos_ = save;
  // std::cerr << "Expected '>' at end of tag: " << input_ << "...\n";
  return nullptr;
}

// ── handleBracketOpener
// ───────────────────────────────────────────────────────

void InlineParser::handleBracketOpener(bool is_image) {
  brackets_.push_back({is_image, nullptr, delimiters_.size()});
}

// ── scanLinkDestination
// ───────────────────────────────────────────────────────

std::optional<std::string> InlineParser::scanLinkDestination() {
  if (pos_ >= input_.size())
    return std::nullopt;
  std::size_t save = pos_;

  if (input_[pos_] == '<') {
    ++pos_;
    std::string dest;
    while (pos_ < input_.size() && input_[pos_] != '\n') {
      char c = input_[pos_];
      if (c == '>') {
        ++pos_;
        return dest;
      }
      if (c == '<') {
        pos_ = save;
        return std::nullopt;
      }
      if (c == '\\' && pos_ + 1 < input_.size() &&
          isAsciiPunct(input_[pos_ + 1])) {
        dest += input_[pos_ + 1];
        pos_ += 2;
      } else if (c == '&') {
        std::string decoded = entities::decode(input_, pos_);
        if (!decoded.empty())
          dest += decoded;
        else {
          dest += c;
          ++pos_;
        }
      } else {
        dest += c;
        ++pos_;
      }
    }
    pos_ = save;
    return std::nullopt;
  }

  // Bare form: balanced parens, no ASCII control chars.
  std::string dest;
  int depth = 0;
  while (pos_ < input_.size()) {
    char c = input_[pos_];
    if (static_cast<unsigned char>(c) <= 0x1F || c == ' ')
      break;
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      if (depth == 0)
        break;
      --depth;
    } else if (c == '\\' && pos_ + 1 < input_.size() &&
               isAsciiPunct(input_[pos_ + 1])) {
      dest += input_[pos_ + 1];
      pos_ += 2;
      continue;
    } else if (c == '&') {
      std::string decoded = entities::decode(input_, pos_);
      if (!decoded.empty()) {
        dest += decoded;
        continue;
      }
    }
    dest += c;
    ++pos_;
  }
  if (depth != 0) {
    pos_ = save;
    return std::nullopt;
  }
  return dest;
}

// ── scanLinkTitle
// ─────────────────────────────────────────────────────────────

std::optional<std::string> InlineParser::scanLinkTitle() {
  if (pos_ >= input_.size())
    return std::nullopt;
  std::size_t save = pos_;
  char open = input_[pos_];
  char close;
  if (open == '"')
    close = '"';
  else if (open == '\'')
    close = '\'';
  else if (open == '(')
    close = ')';
  else
    return std::nullopt;
  ++pos_;

  std::string title;
  while (pos_ < input_.size()) {
    char c = input_[pos_];
    if (c == close) {
      ++pos_;
      return title;
    }
    if (open == '(' && c == '(') {
      pos_ = save;
      return std::nullopt;
    }
    if (c == '\\' && pos_ + 1 < input_.size() &&
        isAsciiPunct(input_[pos_ + 1])) {
      title += input_[pos_ + 1];
      pos_ += 2;
    } else if (c == '&') {
      std::string decoded = entities::decode(input_, pos_);
      if (!decoded.empty())
        title += decoded;
      else {
        title += c;
        ++pos_;
      }
    } else {
      title += c;
      ++pos_;
    }
  }
  pos_ = save;
  return std::nullopt;
}

// ── handleBracketCloser
// ───────────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::handleBracketCloser() {
  auto literal_bracket = [&]() -> std::unique_ptr<InlineNode> {
    auto node = makeNode(InlineType::Text);
    node->literal = "]";
    return node;
  };

  if (brackets_.empty())
    return literal_bracket();

  BracketEntry bracket = brackets_.back();

  // Deactivated openers (set to nullptr when a containing link was resolved).
  if (bracket.node == nullptr) {
    brackets_.pop_back();
    return literal_bracket();
  }

  // Helper: collect literal text of nodes after the bracket opener node.
  auto get_bracket_text = [&]() -> std::string {
    auto op_it = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto &p) {
      return p.get() == bracket.node;
    });
    std::string text;
    if (op_it == nodes_.end())
      return text;
    for (auto it = op_it + 1; it != nodes_.end(); ++it)
      text += (*it)->literal;
    return text;
  };

  std::size_t save = pos_;
  std::optional<std::string> dest, title, label;
  bool resolved = false;

  // 1. Inline link: ](destination title?)
  if (!resolved && pos_ < input_.size() && input_[pos_] == '(') {
    std::size_t il_save = pos_;
    ++pos_;
    while (
        pos_ < input_.size() &&
        (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
      ++pos_;
    auto d = scanLinkDestination();
    if (d) {
      dest = d;
      while (
          pos_ < input_.size() &&
          (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n'))
        ++pos_;
      if (pos_ < input_.size() &&
          (input_[pos_] == '"' || input_[pos_] == '\'' ||
           input_[pos_] == '(')) {
        auto t = scanLinkTitle();
        if (t) {
          title = t;
          while (pos_ < input_.size() &&
                 (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                  input_[pos_] == '\n'))
            ++pos_;
        }
      }
      if (pos_ < input_.size() && input_[pos_] == ')') {
        ++pos_;
        resolved = true;
      }
    }
    if (!resolved) {
      pos_ = il_save;
      dest = std::nullopt;
      title = std::nullopt;
    }
  }

  // 2. Full reference: ][label]
  if (!resolved && pos_ < input_.size() && input_[pos_] == '[') {
    std::size_t fr_save = pos_;
    ++pos_;
    std::size_t ls = pos_;
    while (pos_ < input_.size() && input_[pos_] != ']' &&
           input_[pos_] != '\n' && pos_ - ls < 999)
      ++pos_;
    if (pos_ < input_.size() && input_[pos_] == ']' && pos_ > ls) {
      std::string raw(input_.substr(ls, pos_ - ls));
      ++pos_;
      auto it = ref_map_->find(normaliseLabel(raw));
      if (it != ref_map_->end()) {
        dest = it->second.destination;
        title = it->second.title;
        label = raw;
        resolved = true;
      }
    }
    if (!resolved)
      pos_ = fr_save;
  }

  // 3. Collapsed reference: ][]
  if (!resolved && pos_ + 1 < input_.size() && input_[pos_] == '[' &&
      input_[pos_ + 1] == ']') {
    std::string bt = get_bracket_text();
    auto it = ref_map_->find(normaliseLabel(bt));
    if (it != ref_map_->end()) {
      pos_ += 2;
      dest = it->second.destination;
      title = it->second.title;
      label = bt;
      resolved = true;
    }
  }

  // 4. Shortcut reference: label = bracket text
  if (!resolved) {
    std::string bt = get_bracket_text();
    auto it = ref_map_->find(normaliseLabel(bt));
    if (it != ref_map_->end()) {
      dest = it->second.destination;
      title = it->second.title;
      label = bt;
      resolved = true;
    }
  }

  if (resolved) {
    processEmphasis(bracket.delim_top);

    auto link =
        makeNode(bracket.is_image ? InlineType::Image : InlineType::Link);
    link->data = LinkData{dest.value(), title, label};

    auto op_it = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto &p) {
      return p.get() == bracket.node;
    });

    if (op_it != nodes_.end()) {
      for (auto it = op_it + 1; it != nodes_.end(); ++it)
        link->children.push_back(std::move(*it));
      nodes_.erase(op_it + 1, nodes_.end());
      nodes_.erase(op_it); // remove the "[" / "![" text node
    }

    // Links cannot be nested: deactivate any preceding link (not image)
    // openers.
    if (!bracket.is_image) {
      for (auto &b : brackets_)
        if (!b.is_image)
          b.node = nullptr;
    }

    brackets_.pop_back();
    return link;
  }

  // Failed: deactivate delimiters inside this bracket range.
  pos_ = save;
  brackets_.pop_back();
  for (std::size_t i = bracket.delim_top; i < delimiters_.size(); ++i) {
    delimiters_[i].can_open = false;
    delimiters_[i].can_close = false;
  }
  return literal_bracket();
}

// ── handleEmphasis (spec §6.4)
// ────────────────────────────────────────────────

void InlineParser::handleEmphasis(char delim_char, std::size_t run_len) {
  // pos_ is already past the run; run starts at pos_ - run_len.
  char prev_c = (pos_ > run_len) ? input_[pos_ - run_len - 1] : '\0';
  char next_c = (pos_ < input_.size()) ? input_[pos_] : '\0';

  bool prev_ws = (prev_c == '\0') || isUnicodeWhitespace(prev_c);
  bool next_ws = (next_c == '\0') || isUnicodeWhitespace(next_c);
  bool prev_punct = isUnicodePunct(prev_c);
  bool next_punct = isUnicodePunct(next_c);

  bool left_flanking = !next_ws && (!next_punct || prev_ws || prev_punct);
  bool right_flanking = !prev_ws && (!prev_punct || next_ws || next_punct);

  bool can_open, can_close;
  if (delim_char == '*') {
    can_open = left_flanking;
    can_close = right_flanking;
  } else { // '_'
    can_open = left_flanking && (!right_flanking || prev_punct);
    can_close = right_flanking && (!left_flanking || next_punct);
  }

  if (!can_open && !can_close)
    return; // literal text, no delimiter pushed

  Delimiter d;
  d.ch = delim_char;
  d.num = static_cast<int>(run_len);
  d.can_open = can_open;
  d.can_close = can_close;
  d.node = nullptr; // set by parseInline after this call returns
  delimiters_.push_back(d);
}

// ── processEmphasis (CommonMark appendix algorithm)
// ───────────────────────────

void InlineParser::processEmphasis(std::optional<std::size_t> stack_bottom) {
  std::size_t bot = stack_bottom.value_or(0);

  // Per-character lower bounds for opener search.
  std::size_t ob[2] = {bot, bot}; // [0]='*', [1]='_'
  auto oi = [](char c) -> int { return c == '*' ? 0 : 1; };

  std::size_t ci = bot; // closer index

  while (ci < delimiters_.size()) {
    Delimiter &closer = delimiters_[ci];
    if (!closer.can_close) {
      ++ci;
      continue;
    }

    char c = closer.ch;

    // Search backward for a matching opener.
    bool found = false;
    std::size_t opIdx = ci;
    while (opIdx > ob[oi(c)]) {
      --opIdx;
      Delimiter &opener = delimiters_[opIdx];
      if (!opener.can_open || opener.ch != c)
        continue;
      // "Sum rule" (§6.4): if either delimiter can both open and close,
      // (opener.num + closer.num) % 3 must not be 0 unless both are mult of 3.
      if ((opener.can_open && opener.can_close) ||
          (delimiters_[ci].can_open && delimiters_[ci].can_close)) {
        if ((opener.num + delimiters_[ci].num) % 3 == 0 && opener.num % 3 != 0)
          continue;
      }
      found = true;
      break;
    }

    if (!found) {
      ob[oi(c)] = ci;
      if (!delimiters_[ci].can_open)
        delimiters_.erase(delimiters_.begin() + ci);
      else
        ++ci;
      continue;
    }

    // Matched opener at opIdx, closer at ci.
    int use = (delimiters_[opIdx].num >= 2 && delimiters_[ci].num >= 2) ? 2 : 1;
    auto type = (use == 2) ? InlineType::Strong : InlineType::Emph;

    InlineNode *op_ptr = delimiters_[opIdx].node;
    InlineNode *cl_ptr = delimiters_[ci].node;

    auto op_it =
        std::find_if(nodes_.begin(), nodes_.end(),
                     [op_ptr](const auto &p) { return p.get() == op_ptr; });
    auto cl_it =
        std::find_if(nodes_.begin(), nodes_.end(),
                     [cl_ptr](const auto &p) { return p.get() == cl_ptr; });

    auto emph = makeNode(type);
    for (auto it = op_it + 1; it != cl_it; ++it)
      emph->children.push_back(std::move(*it));

    // Replace nodes (op_it+1 .. cl_it) with the emph node.
    auto ins = nodes_.erase(op_it + 1, cl_it);
    nodes_.insert(ins, std::move(emph));

    // Shrink delimiter counts; update literal strings.
    delimiters_[opIdx].num -= use;
    delimiters_[opIdx].node->literal =
        std::string(static_cast<std::size_t>(delimiters_[opIdx].num), c);

    delimiters_[ci].num -= use;
    delimiters_[ci].node->literal =
        std::string(static_cast<std::size_t>(delimiters_[ci].num), c);

    // Erase delimiter entries strictly between opener and closer.
    if (opIdx + 1 < ci) {
      delimiters_.erase(delimiters_.begin() + opIdx + 1,
                        delimiters_.begin() + ci);
      ci = opIdx + 1;
    }

    // Remove empty opener.
    if (delimiters_[opIdx].num == 0) {
      delimiters_.erase(delimiters_.begin() + opIdx);
      if (ci > opIdx)
        --ci;
    }

    // Remove empty closer (don't advance; next iteration retries this slot).
    if (ci < delimiters_.size() && delimiters_[ci].num == 0)
      delimiters_.erase(delimiters_.begin() + ci);
    // If closer still has chars, stay at ci and match again.
  }

  // Remove all delimiter entries at or above bot.
  if (bot < delimiters_.size())
    delimiters_.erase(delimiters_.begin() + bot, delimiters_.end());
}

// ── normaliseLabel (spec §4.7, ASCII-only case folding)
// ───────────────────────

std::string InlineParser::normaliseLabel(std::string_view label) {
  std::string out;
  out.reserve(label.size());
  bool space = true; // trim leading whitespace
  for (char c : label) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!space)
        out += ' ';
      space = true;
    } else {
      out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      space = false;
    }
  }
  if (!out.empty() && out.back() == ' ')
    out.pop_back(); // trim trailing
  return out;
}

// ── makeNode
// ──────────────────────────────────────────────────────────────────

std::unique_ptr<InlineNode> InlineParser::makeNode(InlineType type) {
  auto node = std::make_unique<InlineNode>();
  node->type = type;
  return node;
}
