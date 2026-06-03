#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/PreScanner.hpp"
#include "markdown_parser/block_rules.hpp"
#include <algorithm>
#include <iostream>
#include <optional>

SpineHandler::SpineHandler(PreScanner &scanner, InlineParser &inline_parser)
    : scanner_(scanner), inline_parser_(inline_parser) {
  openBlock(NodeType::Document, std::monostate{});
}

void SpineHandler::processLine(std::string_view raw) {
  partial_tab_remaining_ = 0;
  current_col_ = 0;
  current_byte_ = 0;
  swallow_current_line_ = false;
  ++line_number_;

  ScannedLine line = scanner_.scan(raw);

  // std::cout << "step 1" << std::endl;
  SpineMatchResult match = step1WalkSpine(line);
  // std::cout << "step 2" << std::endl;
  step2NewBlocks(line, match);
  // std::cout << "step 3" << std::endl;
  step3AppendText(line, match);
  // std::cout << "check html block end" << std::endl;
  checkHtmlBlockEnd(line);

  // Track blank lines for loose-list detection.
  if (!spine_.empty())
    tip()->last_line_blank = line.is_blank;
}

void SpineHandler::finalize() {
  while (!spine_.empty())
    closeBlock();
  parseInlineContent(document_.get());
}

std::unique_ptr<BlockNode> SpineHandler::releaseDocument() {
  return std::move(document_);
}

// ── Step 1
// ────────────────────────────────────────────────────────────────────

SpineMatchResult SpineHandler::step1WalkSpine(const ScannedLine &line) {
  SpineMatchResult result;
  result.deepest_matched = 0;
  result.first_unmatched = spine_.size();

  for (std::size_t i = 1; i < spine_.size(); ++i) {
    auto cr = block_rules::continuationMatches(*spine_[i], line, current_col_);
    if (cr.matched) {
      if (cr.cols_to_consume > 0)
        consumeColumns(line.content, current_byte_, cr.cols_to_consume);
      result.deepest_matched = i;
      // Leaf blocks cannot contain nested blocks; stop here so new openers
      // in step2 can close this leaf via first_unmatched.
      const NodeType t = spine_[i]->type;
      const bool is_leaf = t != NodeType::Document &&
                           t != NodeType::BlockQuote && t != NodeType::List &&
                           t != NodeType::Item;
      if (is_leaf) {
        result.first_unmatched = i + 1;
        break;
      }
    } else {
      result.first_unmatched = i;
      if (cr.swallow_line)
        result.swallow_line = true;
      break;
    }
  }
  return result;
}

// ── Step 2
// ────────────────────────────────────────────────────────────────────

bool SpineHandler::tryOpenNewBlock(const ScannedLine &line,
                                   const SpineMatchResult &match) {
  bool any_opened = false;
  ScannedLine cur =
      scanner_.scanWithOffset(line.content.substr(current_byte_), current_col_);

  int cycle = 0;
  while (true) {
    // std::cerr << "[tryOpenNewBlock] cycle=" << cycle++
    //           << " current_byte_=" << current_byte_
    //           << " current_col_=" << current_col_ << " remaining=\""
    //           << line.content.substr(current_byte_) << "\"\n";
    // std::cerr << "  Press ENTER to continue...";
    //    std::cin.get();

    const bool tip_para = (tip()->type == NodeType::Paragraph);

    // Detect list-item blank-line continuation to suppress indented code.
    bool inside_list_blank = false;
    for (auto it = spine_.rbegin(); it != spine_.rend(); ++it) {
      const NodeType t = (*it)->type;
      if (t == NodeType::Item) {
        inside_list_blank = (*it)->last_line_blank;
        break;
      }
      if (t != NodeType::List)
        break;
    }

    auto result = block_rules::tryOpen(cur, tip_para, inside_list_blank);
    // std::cerr << "  tryOpen result: " << (result ? "found" : "null")
    //           << (result ? " cols=" + std::to_string(result->cols_consumed)
    //                      : "")
    //           << "\n";
    if (!result) {
      // When an indented code block is suppressed inside a list item after a
      // blank line, consume the 4-col code-block indent so the paragraph text
      // starts at the right position (CommonMark §5.3).
      if (inside_list_blank && cur.virtual_indent == 4)
        consumeColumns(line.content, current_byte_, 4);
      break;
    }

    if (!any_opened) {
      closeUnmatched(match.first_unmatched);
      // A new block opener always closes an open paragraph (paragraphs are
      // leaves and cannot contain block-level children).
      if (tip()->type == NodeType::Paragraph)
        closeBlock();
      any_opened = true;
    }

    // For Item openers: open or reuse a containing List.
    if (result->type == NodeType::Item && result->list_data.has_value()) {
      const auto &new_ld = *result->list_data;
      bool need_new_list = (tip()->type != NodeType::List);
      if (!need_new_list) {
        const auto &cur_ld = std::get<ListData>(tip()->data);
        need_new_list = cur_ld.list_type != new_ld.list_type ||
                        cur_ld.delimiter != new_ld.delimiter ||
                        (new_ld.list_type == ListType::Bullet &&
                         cur_ld.bullet_char != new_ld.bullet_char);
      }
      if (need_new_list)
        openBlock(NodeType::List, new_ld);
    }

    BlockNode *new_node = openBlock(result->type, result->data);
    if (!result->extracted_content.empty())
      new_node->string_content = result->extracted_content;
    if (result->cols_consumed > 0)
      consumeColumns(line.content, current_byte_, result->cols_consumed);
    std::cerr << "  [after consume] byte=" << current_byte_
              << " col=" << current_col_
              << " cols_consumed=" << result->cols_consumed << "\n";
    if (result->swallow_line) {
      swallow_current_line_ = true;
      break;
    }

    // Only container blocks continue the loop, and only if we advanced.
    const bool is_container = result->type == NodeType::BlockQuote ||
                              result->type == NodeType::List ||
                              result->type == NodeType::Item;
    if (!is_container || result->cols_consumed == 0)
      break;

    cur = scanner_.scanWithOffset(line.content.substr(current_byte_),
                                  current_col_);
  }

  return any_opened;
}

void SpineHandler::step2NewBlocks(const ScannedLine &line,
                                  const SpineMatchResult &match) {
  const bool new_block_found = tryOpenNewBlock(line, match);

  if (new_block_found) {
    // closeUnmatched already called inside tryOpenNewBlock
  } else if (incorporatesLazyContinuation(line, match)) {
    // lazy continuation: leave unmatched blocks open

    // Debug cout
    std::cout
        << "incorporates lazy continuation, leaving spine unmatched from index "
        << match.first_unmatched << " onward\n"
        << "  line=\"" << line.content << "\" is_blank=" << line.is_blank
        << " spine_size=" << spine_.size() << "\n"
        << "  tip type=" << nodeTypeToString(tip()->type) << "\n"
        << "  match.first_unmatched (type)="
        << (nodeTypeToString(spine_[match.first_unmatched]->type)) << "\n";

  } else {
    closeUnmatched(match.first_unmatched);
  }
}

// ── Step 3
// ────────────────────────────────────────────────────────────────────

void SpineHandler::step3AppendText(const ScannedLine &line,
                                   const SpineMatchResult &match) {
  if (match.swallow_line || swallow_current_line_)
    return;
  if (line.is_blank)
    return;
  if (tryPromoteSetextHeading(line))
    return;

  // If the tip is a container, open a Paragraph to receive the text.
  {
    const NodeType tt = tip()->type;
    if (tt == NodeType::Document || tt == NodeType::BlockQuote ||
        tt == NodeType::List || tt == NodeType::Item)
      openBlock(NodeType::Paragraph, std::monostate{});
  }

  appendText(line.content, current_byte_);
}

// ── Tree mutation primitives
// ──────────────────────────────────────────────────

BlockNode *SpineHandler::openBlock(NodeType type, BlockData data) {
  auto node_ptr = std::make_unique<BlockNode>();
  BlockNode *node = node_ptr.get();
  node->type = type;
  node->data = std::move(data);
  node->start_line = line_number_;
  node->is_open = true;
  spine_.push_back(std::move(node_ptr));
  return node;
}

void SpineHandler::closeBlock() {
  auto node = std::move(spine_.back());
  spine_.pop_back();

  if (node->type == NodeType::Paragraph) {
    maybeScanLinkRefDefs(node.get());
    // Paragraph carried only link reference definitions: discard it.
    const auto &sc = node->string_content;
    const bool blank = std::all_of(sc.begin(), sc.end(), [](char c) {
      return c == ' ' || c == '\t' || c == '\n';
    });
    if (blank)
      return;
  }

  // If a list item had a blank line inside or after it, the containing list
  // is loose (CommonMark §5.3).
  if (node->type == NodeType::Item && node->last_line_blank) {
    if (!spine_.empty() && spine_.back()->type == NodeType::List)
      std::get<ListData>(spine_.back()->data).tight = false;
  }

  block_rules::onClose(*node);

  node->end_line = line_number_;
  node->is_open = false;

  if (!spine_.empty()) {
    spine_.back()->children.push_back(std::move(node));
  } else {
    document_ = std::move(node);
  }
}

void SpineHandler::closeUnmatched(std::size_t from_index) {
  while (spine_.size() > from_index)
    closeBlock();
}

void SpineHandler::appendText(std::string_view line, std::size_t from_byte) {
  BlockNode *t = tip();
  if (partial_tab_remaining_ > 0) {
    t->string_content.append(partial_tab_remaining_, ' ');
    partial_tab_remaining_ = 0;
    ++from_byte;
  }
  t->string_content += line.substr(from_byte);
  t->string_content += '\n';
}

BlockNode *SpineHandler::tip() const noexcept { return spine_.back().get(); }

// ── Helpers
// ───────────────────────────────────────────────────────────────────

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine &line, const SpineMatchResult &match) const noexcept {

  // Debug cout
  std::cout << "incorporatesLazyContinuation? line=\"" << line.content
            << "\" is_blank=" << line.is_blank
            << " match.first_unmatched=" << match.first_unmatched
            << " spine_size=" << spine_.size() << "\n";
  if (line.is_blank)
    return false;
  if (match.first_unmatched >= spine_.size())
    return false;
  if (tip()->type != NodeType::Paragraph) {
    std::cout << "tip is not paragraph, it's " << nodeTypeToString(tip()->type)
              << "\n";
    return false;
  }
  // A setext underline is never a valid lazy continuation (spec §5.1).
  if (block_rules::isSetextUnderline(line))
    return false;
  return true;
}

bool SpineHandler::tryPromoteSetextHeading(const ScannedLine &line) {
  if (!block_rules::isSetextUnderline(line))
    return false;
  BlockNode *t = tip();
  if (t->type != NodeType::Paragraph)
    return false;
  const char c = line.content[line.next_non_space];
  t->type = NodeType::Heading;
  t->data = HeadingData{(c == '=') ? 1 : 2, /*setext=*/true};
  closeBlock();
  return true;
}

void SpineHandler::checkHtmlBlockEnd(const ScannedLine &line) {
  if (spine_.empty())
    return;
  BlockNode *t = tip();
  if (t->type != NodeType::HtmlBlock)
    return;
  const int html_type = std::get<HtmlBlockData>(t->data).html_type;
  if (html_type >= 1 && html_type <= 5 &&
      block_rules::htmlBlockEndMet(*t, line.content))
    closeBlock();
}

std::size_t SpineHandler::consumeColumns(std::string_view line,
                                         std::size_t byte_offset,
                                         std::size_t n_cols) {
  std::size_t cols_needed = n_cols;

  if (partial_tab_remaining_ > 0) {
    const std::size_t take = std::min(partial_tab_remaining_, cols_needed);
    partial_tab_remaining_ -= take;
    cols_needed -= take;
    current_col_ += take;
    if (partial_tab_remaining_ == 0)
      ++byte_offset; // tab fully consumed; advance past it
  }

  while (cols_needed > 0 && byte_offset < line.size()) {
    const unsigned char byte = static_cast<unsigned char>(line[byte_offset]);
    if (byte == ' ') {
      ++byte_offset;
      --cols_needed;
      ++current_col_;
    } else if (byte == '\t') {
      const std::size_t tab_w = (current_col_ / 4 + 1) * 4 - current_col_;
      if (tab_w <= cols_needed) {
        ++byte_offset;
        cols_needed -= tab_w;
        current_col_ += tab_w;
      } else {
        partial_tab_remaining_ = tab_w - cols_needed;
        current_col_ += cols_needed;
        cols_needed = 0;
      }
    } else {
      ++byte_offset;
      --cols_needed;
      ++current_col_;
    }
  }
  current_byte_ = byte_offset;
  return byte_offset;
}

void SpineHandler::parseInlineContent(BlockNode *node) {
  const bool needs_inline =
      node->type == NodeType::Paragraph || node->type == NodeType::Heading;

  const bool is_container =
      node->type == NodeType::Document || node->type == NodeType::BlockQuote ||
      node->type == NodeType::List || node->type == NodeType::Item;

  if (needs_inline) {
    inline_parser_.parse(node, ref_map_);
  } else if (is_container) {
    for (const auto &child : node->children)
      parseInlineContent(child.get());
  }
}

// Returns true and advances pos past the consumed definition.
// Returns false (pos unchanged) if no valid definition starts at pos.
// Inserts into ref_map_ on success (first-definition-wins: skips duplicates).
bool SpineHandler::tryScanOneLinkRefDef(std::string_view content,
                                        std::size_t &pos) {
  const std::size_t len = content.size();
  std::size_t p = pos;

  // 0–3 spaces of leading indentation (4+ would have been a code block already)
  std::size_t indent = 0;
  while (indent < 3 && p < len && content[p] == ' ') {
    ++p;
    ++indent;
  }

  if (p >= len || content[p] != '[')
    return false;
  ++p;

  // Scan label: everything up to the first unescaped ']'.
  // Newlines inside the label are allowed (issue 3); fail only if ']' is never
  // found or the normalised content is empty (all-whitespace).
  std::size_t label_start = p;
  while (p < len && content[p] != ']')
    ++p;
  if (p >= len)
    return false;
  std::string raw_label(content.substr(label_start, p - label_start));
  ++p; // skip ']'

  std::string norm_key = InlineParser::normaliseLabel(raw_label);
  if (norm_key.empty())
    return false;

  if (p >= len || content[p] != ':')
    return false;
  ++p;

  // Skip spaces/tabs and at most one newline before destination.
  // Two consecutive newlines (blank line) → no destination → fail.
  while (p < len && (content[p] == ' ' || content[p] == '\t'))
    ++p;
  if (p < len && content[p] == '\n') {
    ++p;
    if (p < len && content[p] == '\n')
      return false; // blank line
    while (p < len && (content[p] == ' ' || content[p] == '\t'))
      ++p;
  }
  if (p >= len || content[p] == '\n')
    return false; // no destination

  // Scan destination.
  std::string destination;
  if (content[p] == '<') {
    ++p;
    bool closed = false;
    while (p < len) {
      const char c = content[p];
      if (c == '\n' || c == '<')
        break; // not allowed unescaped
      if (c == '>') {
        closed = true;
        ++p;
        break;
      }
      if (c == '\\' && p + 1 < len) {
        destination += content[p + 1];
        p += 2;
      } else {
        destination += c;
        ++p;
      }
    }
    if (!closed)
      return false;
  } else {
    int depth = 0;
    while (p < len) {
      const char c = content[p];
      if (c == ' ' || c == '\t' || c == '\n')
        break;
      if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f')
        break;
      if (c == '(') {
        ++depth;
        destination += c;
        ++p;
      } else if (c == ')') {
        if (depth == 0)
          break;
        --depth;
        destination += c;
        ++p;
      } else if (c == '\\' && p + 1 < len) {
        destination += content[p + 1];
        p += 2;
      } else {
        destination += c;
        ++p;
      }
    }
    if (depth != 0 || destination.empty())
      return false;
  }

  // pos_after_dest: byte right after the last destination character, before any
  // trailing whitespace or newline. This is the rewind point if title parsing
  // fails on a separate line (issue 1).
  const std::size_t pos_after_dest = p;

  // Try to scan an optional title.
  std::optional<std::string> title;
  {
    std::size_t tp = p;
    bool crossed_newline = false;

    while (tp < len && (content[tp] == ' ' || content[tp] == '\t'))
      ++tp;
    if (tp < len && content[tp] == '\n') {
      crossed_newline = true;
      ++tp;
      while (tp < len && (content[tp] == ' ' || content[tp] == '\t'))
        ++tp;
    }

    if (tp < len &&
        (content[tp] == '"' || content[tp] == '\'' || content[tp] == '(')) {
      const char open_d = content[tp];
      const char close_d = (open_d == '(') ? ')' : open_d;
      ++tp;
      std::string buf;
      bool title_ok = false;
      bool prev_nl = false;
      while (tp < len) {
        const char c = content[tp];
        if (c == '\n') {
          if (prev_nl) {
            title_ok = false;
            break;
          } // blank line inside title
          prev_nl = true;
          buf += c;
          ++tp;
          continue;
        }
        prev_nl = false;
        if (c == close_d) {
          title_ok = true;
          ++tp;
          break;
        }
        if (c == '\\' && tp + 1 < len) {
          buf += content[tp + 1];
          tp += 2;
        } else {
          buf += c;
          ++tp;
        }
      }

      if (title_ok) {
        // Check for garbage after the closing delimiter (issue 4).
        // "skip trailing spaces/tabs only — stop at '\n'" (issue 2).
        std::size_t ap = tp;
        while (ap < len && (content[ap] == ' ' || content[ap] == '\t'))
          ++ap;

        if (ap >= len || content[ap] == '\n') {
          // Clean: title accepted; point p at the '\n' (or EOI).
          title = std::move(buf);
          p = ap;
        } else if (crossed_newline) {
          // Garbage after a same-line title, but the title was on the next
          // line — rewind to pos_after_dest and accept the definition
          // without a title (issues 1 & 4). The title line stays as
          // paragraph text.
          p = pos_after_dest;
        } else {
          // Garbage on the same line as the destination → whole definition
          // fails (issue 4).
          return false;
        }
      } else {
        // Title parse failed (no closing delimiter, or blank line inside).
        if (crossed_newline) {
          p = pos_after_dest; // rewind (issue 1)
        } else {
          return false;
        }
      }
    }
    // If no title delimiter was found at all, p remains at pos_after_dest and
    // title stays nullopt.
  }

  // Issue 2: skip trailing spaces and tabs only — do NOT consume '\n' here.
  while (p < len && (content[p] == ' ' || content[p] == '\t'))
    ++p;

  // Any non-whitespace before the line terminator invalidates the definition.
  if (p < len && content[p] != '\n')
    return false;

  // Issue 2: consume the line terminator as a separate step.
  if (p < len)
    ++p;

  if (ref_map_.find(norm_key) == ref_map_.end())
    ref_map_[norm_key] = LinkDef{std::move(destination), std::move(title)};

  pos = p;
  return true;
}

void SpineHandler::maybeScanLinkRefDefs(BlockNode *node) {
  std::string_view content = node->string_content;
  std::size_t pos = 0;
  while (pos < content.size()) {
    if (!tryScanOneLinkRefDef(content, pos))
      break;
  }
  node->string_content = std::string(content.substr(pos));
}

void SpineHandler::stripTrailingBlankLines(std::string &) {}
