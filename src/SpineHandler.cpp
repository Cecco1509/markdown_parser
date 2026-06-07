#include "markdown_parser/SpineHandler.hpp"
#include "markdown_parser/InlineParser.hpp"
#include "markdown_parser/block_rules.hpp"
#include "markdown_parser/commonmark_constants.hpp"
#include <algorithm>
#include <iostream>
#include <optional>

SpineHandler::SpineHandler(InlineParser &inline_parser, bool debug)
    : inline_parser_(inline_parser), debug_(debug) {
  openBlock(NodeType::Document, std::monostate{});
}

void SpineHandler::printSpineStatus() const {
  std::cerr << "SPINE  [" << spine_.size() << "]  ";
  for (std::size_t i = 0; i < spine_.size(); ++i) {
    const BlockNode *n = spine_[i].get();
    if (i)
      std::cerr << " > ";
    std::cerr << nodeTypeToString(n->type);
    if (!n->string_content.empty()) {
      std::string preview = n->string_content;
      if (preview.size() > 20)
        preview = preview.substr(0, 20) + "...";
      for (char &c : preview)
        if (c == '\n')
          c = '\xb6';
      std::cerr << "(\"" << preview << "\")";
    }
  }
  std::cerr << "\n";
}

void SpineHandler::processLine(std::string_view raw) {
  ++line_number_;
  if (debug_) {
    std::cerr << "\n──── LINE " << line_number_ << " \"" << raw << "\"\n";
    printSpineStatus();
    std::cerr << "  [enter]";
    std::cin.get();
  }

  ScannedLine line = ScannedLine::from(raw);
  auto [match, cur] = step1WalkSpine(line);
  auto [cur2, swallow] = step2NewBlocks(cur, match);
  step3AppendText(cur2, match, swallow);
  checkHtmlBlockEnd(line);

  // Track blank lines for loose-list detection.
  if (!spine_.empty())
    tip()->last_line_blank = line.is_blank();
}

void SpineHandler::finalize() {
  if (debug_)
    std::cerr << "FINALIZE  spine=" << spine_.size() << "\n";
  while (!spine_.empty())
    closeBlock();
  parseInlineContent(document_.get());
}

std::unique_ptr<BlockNode> SpineHandler::releaseDocument() {
  return std::move(document_);
}

// ── Step 1
// ────────────────────────────────────────────────────────────────────

std::pair<SpineMatchResult, ScannedLine>
SpineHandler::step1WalkSpine(const ScannedLine &line) {
  if (debug_)
    std::cerr << "STEP1  \"" << line.content() << "\""
              << "  blank=" << line.is_blank() << " base=" << line.base_col()
              << "\n";
  SpineMatchResult result;
  result.deepest_matched = 0;
  result.first_unmatched = spine_.size();

  ScannedLine cur = line;

  for (std::size_t i = 1; i < spine_.size(); ++i) {
    auto cr = block_rules::continuationMatches(*spine_[i], cur, cur.base_col());
    if (cr.matched) {
      if (cr.cols_to_consume > 0)
        cur = cur.consume(cr.cols_to_consume);
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
  if (debug_)
    std::cerr << "step1  -> deep=" << result.deepest_matched
              << " unmatch=" << result.first_unmatched
              << " swallow=" << result.swallow_line << " base=" << cur.base_col()
              << "\n";
  return {result, cur};
}

// ── Step 2
// ────────────────────────────────────────────────────────────────────

SpineHandler::OpenBlockResult
SpineHandler::tryOpenNewBlock(const ScannedLine &line,
                              const SpineMatchResult &match) {
  if (debug_)
    std::cerr << "OPEN?  \"" << line.content() << "\""
              << "  base=" << line.base_col()
              << " unmatch=" << match.first_unmatched
              << " tip=" << nodeTypeToString(tip()->type) << "\n";
  bool any_opened = false;
  bool swallow = false;
  ScannedLine cur = line;

  while (true) {
    // tip_para: only restrict list-item interruption when the paragraph will
    // survive closeUnmatched (i.e. its container already matched).
    bool tip_para = false;
    // suppress_indented_code: also suppress indented-code-block opening when
    // the paragraph is exactly at the first_unmatched boundary — in that case
    // the line should lazily continue the paragraph instead.
    bool suppress_indented_code = false;
    if (tip()->type == NodeType::Paragraph && spine_.size() >= 2) {
      const std::size_t para_container_idx = spine_.size() - 2;
      tip_para = (para_container_idx < match.first_unmatched);
      suppress_indented_code = (para_container_idx <= match.first_unmatched);
    }

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
    // Suppress indented-code-block opening when a paragraph sits exactly at
    // the first_unmatched boundary — the line should lazily continue it.
    if (result && result->type == NodeType::CodeBlock &&
        !std::get<CodeBlockData>(result->data).fenced && suppress_indented_code)
      result = std::nullopt;
    if (!result) {
      // When an indented code block is suppressed inside a list item after a
      // blank line, consume the 4-col code-block indent so the paragraph text
      // starts at the right position (CommonMark §5.3).
      if (inside_list_blank && cur.indent() == commonmark::kCodeBlockIndent)
        cur = cur.consume(4);
      break;
    }

    if (!any_opened) {
      closeUnmatched(match.first_unmatched);
      // A new block opener always closes an open paragraph (paragraphs are
      // leaves and cannot contain block-level children).
      if (tip()->type == NodeType::Paragraph)
        closeBlock();
      // Lists can only directly contain Items; close a dangling List before
      // adding any other kind of block (e.g. an indented code block or an
      // HTML comment that follows the last list item).
      if (result->type != NodeType::Item && tip()->type == NodeType::List)
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
      if (need_new_list) {
        // Close the current incompatible list so the new one becomes a sibling,
        // not a nested child.
        if (tip()->type == NodeType::List)
          closeBlock();
        openBlock(NodeType::List, new_ld);
      } else {
        // Same list: if the previous item ended with a blank line the list is
        // loose (blank between two consecutive items).
        const auto &ch = tip()->children;
        if (!ch.empty() && ch.back()->last_line_blank)
          std::get<ListData>(tip()->data).tight = false;
      }
    }

    BlockNode *new_node = openBlock(result->type, result->data);
    if (!result->extracted_content.empty())
      new_node->string_content = result->extracted_content;
    if (result->cols_consumed > 0)
      cur = cur.consume(result->cols_consumed);
    if (debug_)
      std::cerr << "  OPEN   consumed=" << result->cols_consumed
                << " -> base=" << cur.base_col() << "\n";
    if (result->swallow_line) {
      swallow = true;
      break;
    }

    // Only container blocks continue the loop, and only if we advanced.
    const bool is_container = result->type == NodeType::BlockQuote ||
                              result->type == NodeType::List ||
                              result->type == NodeType::Item;
    if (!is_container || result->cols_consumed == 0)
      break;
  }

  if (debug_)
    std::cerr << "OPEN?  -> opened=" << any_opened << " swallow=" << swallow
              << "\n";
  return {cur, any_opened, swallow};
}

SpineHandler::Step2Result
SpineHandler::step2NewBlocks(const ScannedLine &cur,
                             const SpineMatchResult &match) {
  if (debug_)
    std::cerr << "STEP2  \"" << cur.content() << "\""
              << "  unmatch=" << match.first_unmatched
              << " swallow=" << match.swallow_line
              << " tip=" << nodeTypeToString(tip()->type) << "\n";
  // A swallowed line (e.g. a fenced-code closing fence) has already been
  // consumed in step 1.  Just close unmatched blocks and skip step 2 entirely
  // so the closing fence cannot be re-parsed as a new block opener.
  if (match.swallow_line) {
    closeUnmatched(match.first_unmatched);
    return {cur, /*swallow=*/true};
  }

  // If the tip is a paragraph whose container already matched (i.e., the
  // paragraph itself will survive closeUnmatched) and this line is a setext
  // underline, skip block opening — step3 promotes via tryPromoteSetextHeading.
  // The container-matched check prevents treating e.g. `---` after a list item
  // as a setext underline when the item's indent didn't match.
  if (tip()->type == NodeType::Paragraph &&
      block_rules::isSetextUnderline(cur) && spine_.size() >= 2 &&
      (spine_.size() - 2) < match.first_unmatched)
    return {cur, /*swallow=*/false};

  // A fully-matched non-paragraph leaf (CodeBlock, HtmlBlock, …) owns the
  // remaining content — step3 will append it. Opening new blocks here would
  // wrongly insert them as siblings of the leaf (e.g. `---` inside an indented
  // code block being parsed as a ThematicBreak).
  if (match.first_unmatched == spine_.size()) {
    const NodeType tt = tip()->type;
    const bool is_container = tt == NodeType::Document ||
                              tt == NodeType::BlockQuote ||
                              tt == NodeType::List || tt == NodeType::Item;
    if (!is_container && tt != NodeType::Paragraph)
      return {cur, /*swallow=*/false};
  }

  auto [remaining, any_opened, swallow] = tryOpenNewBlock(cur, match);

  if (any_opened) {
    // closeUnmatched already called inside tryOpenNewBlock
  } else if (incorporatesLazyContinuation(cur, match)) {
    // lazy continuation: leave unmatched blocks open
    if (debug_)
      std::cerr << "STEP2  lazy-cont: keep from=" << match.first_unmatched
                << " (" << nodeTypeToString(spine_[match.first_unmatched]->type)
                << ")\n";
  } else {
    closeUnmatched(match.first_unmatched);
    // A List whose item failed to continue has no way to receive bare text.
    // Close any such dangling List so its content lands in the right parent.
    while (!cur.is_blank() && !spine_.empty() && tip()->type == NodeType::List)
      closeBlock();
  }
  return {remaining, swallow};
}

// ── Step 3
// ────────────────────────────────────────────────────────────────────

void SpineHandler::step3AppendText(const ScannedLine &cur,
                                   const SpineMatchResult &match,
                                   bool swallow) {
  if (debug_)
    std::cerr << "STEP3  \"" << cur.content() << "\""
              << "  blank=" << cur.is_blank() << " swallow=" << swallow
              << " base=" << cur.base_col()
              << " tip=" << nodeTypeToString(tip()->type) << "\n";
  if (swallow)
    return;
  // Blank lines are content inside code blocks (indented or fenced).
  if (cur.is_blank()) {
    if (tip()->type == NodeType::CodeBlock)
      appendText(cur, 0);
    return;
  }
  // If all remaining content (including any prefix_spaces) is whitespace
  // (e.g. empty list item marker), treat this line as having no text.
  if (cur.prefix_spaces() == 0 && cur.is_blank())
    return;

  if (tryPromoteSetextHeading(cur, match))
    return;

  // If the tip is a container, open a Paragraph to receive the text.
  {
    const NodeType tt = tip()->type;
    if (tt == NodeType::Document || tt == NodeType::BlockQuote ||
        tt == NodeType::List || tt == NodeType::Item)
      openBlock(NodeType::Paragraph, std::monostate{});
  }

  std::size_t text_start = 0;
  if (tip()->type == NodeType::CodeBlock) {
    // For fenced code blocks, strip up to fence_indent spaces from each line
    // (CommonMark §4.5: content lines lose the same indentation as the fence).
    const auto &cbd = std::get<CodeBlockData>(tip()->data);
    if (cbd.fenced) {
      int to_strip = cbd.fence_indent;
      while (to_strip > 0 && text_start < cur.content().size() &&
             cur.content()[text_start] == ' ') {
        ++text_start;
        --to_strip;
      }
    }
  } else if (tip()->type == NodeType::Paragraph) {
    // Strip up to 3 leading spaces per CommonMark §4.4.
    // next_non_space is now relative to cur.content() so this is always correct.
    if (debug_) {
      std::cerr << "  STEP3  para text_start=" << cur.next_non_space() << "\n";
    }
    if (cur.next_non_space() <= 3)
      text_start = cur.next_non_space();
  }
  appendText(cur, text_start);
}

// ── Tree mutation primitives
// ──────────────────────────────────────────────────

BlockNode *SpineHandler::openBlock(NodeType type, BlockData data) {
  if (debug_)
    std::cerr << "OPEN   " << nodeTypeToString(type)
              << "  line=" << line_number_ << " spine=" << spine_.size()
              << "\n";
  // Detect internal blank lines inside list items (CommonMark §5.3).
  // If we're opening a new block child into an Item that already has children
  // and the Item had a blank since its last child, mark the list as loose.
  if (!spine_.empty() && tip()->type == NodeType::Item) {
    BlockNode *item = tip();
    if (!item->children.empty() && item->last_line_blank) {
      for (int k = static_cast<int>(spine_.size()) - 2; k >= 0; --k) {
        if (spine_[k]->type == NodeType::List) {
          std::get<ListData>(spine_[k]->data).tight = false;
          break;
        }
      }
    }
    // Reset so subsequent children don't re-trigger loose detection.
    item->last_line_blank = false;
  }
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
  if (debug_)
    std::cerr << "CLOSE  " << nodeTypeToString(spine_.back()->type)
              << "  spine=" << spine_.size() << "\n";
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

  // If a list item had a blank between two of its own block children, the
  // containing list is loose (CommonMark §5.3 "internal blank" rule).
  // A trailing blank (only one child) is NOT enough to mark loose here;
  // blank-between-items is detected separately when the next sibling opens.
  if (node->type == NodeType::Item && node->last_line_blank) {
    if (!spine_.empty() && spine_.back()->type == NodeType::List &&
        node->children.size() >= 2)
      std::get<ListData>(spine_.back()->data).tight = false;
  }

  // When a List closes, propagate its last item's trailing-blank to the
  // parent Item so that a blank between the List and a sibling block in the
  // outer item is detected as loose (CommonMark §5.3).
  if (node->type == NodeType::List && !node->children.empty()) {
    const auto &last_child = node->children.back();
    if (last_child->last_line_blank && !spine_.empty() &&
        spine_.back()->type == NodeType::Item)
      spine_.back()->last_line_blank = true;
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
  if (debug_)
    std::cerr << "UNMATCH from=" << from_index << " spine=" << spine_.size()
              << "\n";
  while (spine_.size() > from_index)
    closeBlock();
}

void SpineHandler::appendText(const ScannedLine &cur, std::size_t from_byte) {
  if (debug_)
    std::cerr << "APPEND \"" << cur.content().substr(from_byte) << "\""
              << "  pfx=" << cur.prefix_spaces()
              << " tip=" << nodeTypeToString(tip()->type) << "\n";
  BlockNode *t = tip();
  if (cur.prefix_spaces() > 0)
    t->string_content.append(cur.prefix_spaces(), ' ');
  t->string_content += cur.content().substr(from_byte);
  t->string_content += '\n';
}

BlockNode *SpineHandler::tip() const noexcept { return spine_.back().get(); }

// ── Helpers
// ───────────────────────────────────────────────────────────────────

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine &line, const SpineMatchResult &match) const noexcept {

  if (debug_)
    std::cerr << "LAZY?  \"" << line.content() << "\""
              << "  unmatch=" << match.first_unmatched
              << " tip=" << nodeTypeToString(tip()->type) << "\n";
  if (line.is_blank())
    return false;
  if (match.first_unmatched >= spine_.size())
    return false;
  if (tip()->type != NodeType::Paragraph) {
    if (debug_)
      std::cerr << "LAZY?  -> false (tip not paragraph)\n";
    return false;
  }
  // Lazy continuation applies for block quotes and list items. For list items
  // the spec allows paragraph continuation even when the item's indent isn't
  // met, as long as the line doesn't open a new block (checked by the caller:
  // we only reach here when tryOpenNewBlock returned false).
  return true;
}

bool SpineHandler::tryPromoteSetextHeading(const ScannedLine &line,
                                           const SpineMatchResult &match) {
  if (debug_)
    std::cerr << "SETEXT? \"" << line.content()
              << "\"  tip=" << nodeTypeToString(tip()->type) << "\n";
  if (!block_rules::isSetextUnderline(line))
    return false;
  BlockNode *t = tip();
  if (t->type != NodeType::Paragraph)
    return false;
  // Don't promote when the paragraph's container is unmatched — the line
  // arrived via lazy continuation and must be appended as plain text instead.
  if (spine_.size() >= 2 && (spine_.size() - 2) >= match.first_unmatched)
    return false;
  const char c = line.content()[line.next_non_space()];
  t->type = NodeType::Heading;
  t->data = HeadingData{(c == '=') ? 1 : 2, /*setext=*/true};
  closeBlock();
  return true;
}

void SpineHandler::checkHtmlBlockEnd(const ScannedLine &line) {
  if (debug_)
    std::cerr << "HTMLEND? \"" << line.content() << "\""
              << "  tip="
              << (spine_.empty() ? "empty" : nodeTypeToString(tip()->type))
              << "\n";
  if (spine_.empty())
    return;
  BlockNode *t = tip();
  if (t->type != NodeType::HtmlBlock)
    return;
  const HtmlBlockType html_type = std::get<HtmlBlockData>(t->data).html_type;
  if (html_type != HtmlBlockType::KnownTag && html_type != HtmlBlockType::Complete &&
      block_rules::htmlBlockEndMet(*t, line.content()))
    closeBlock();
}


void SpineHandler::parseInlineContent(BlockNode *node) {
  if (debug_)
    std::cerr << "INLINE " << nodeTypeToString(node->type)
              << "  children=" << node->children.size() << "\n";
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
  if (debug_)
    std::cerr << "REFDEF? pos=" << pos << " \"" << content.substr(pos)
              << "\"\n";
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
  if (debug_)
    std::cerr << "REFDEF  \"" << node->string_content << "\"\n";
  std::string_view content = node->string_content;
  std::size_t pos = 0;
  while (pos < content.size()) {
    if (!tryScanOneLinkRefDef(content, pos))
      break;
  }
  node->string_content = std::string(content.substr(pos));
}

void SpineHandler::stripTrailingBlankLines(std::string &) {}
