# 5. SpineHandler — phase 1

← [4. PreScanner](04_prescanner.md) | [Index](index.md) | Next: [6. InlineParser — phase 2](06_inline_parser.md) →

---

The SpineHandler owns the open block stack (the "spine") and implements the
three-step `processLine` algorithm. It is the sole owner of the AST root.
All rule logic (continuation predicates, block openers, finalization hooks) lives
in the stateless [`block_rules`](10_block_rules.md) module; SpineHandler is a
pure orchestrator.

For the block types and data structures it operates on, see [§2](02_data_types.md).  
For the continuation and open/close rules delegated to `block_rules`, see [§3](03_continuation_rules.md) and [§10](10_block_rules.md).  
For tab accounting details, see [§7](07_tab_algorithm.md).

---

## 5.1 State

```cpp
// include/markdown_parser/SpineHandler.hpp

class SpineHandler {
public:
    explicit SpineHandler(PreScanner& scanner, InlineParser& inline_parser);

    // Process one raw input line. Resets per-line cursor state, then runs
    // step1WalkSpine → step2NewBlocks → step3AppendText.
    void processLine(std::string_view raw);

    // Close all remaining open blocks and trigger phase 2 inline parsing.
    // Must be called exactly once after all lines have been processed.
    void finalize();

    // Transfer ownership of the completed AST root to the caller.
    // Only valid after finalize() has returned.
    std::unique_ptr<BlockNode> releaseDocument();

private:
    // ── persistent state ──────────────────────────────────────────────────

    // The open block stack. spine_[0] is always the Document root.
    // spine_.back() is the current tip.
    // Each entry owns its BlockNode. When a block is closed it is moved
    // (std::move) out of the spine and appended to its parent's children
    // vector. The Document node is the last to leave the spine, captured
    // by document_ inside finalize().
    std::vector<std::unique_ptr<BlockNode>> spine_;

    // Populated by finalize() when the Document root is closed and moved
    // out of spine_. Empty during the block phase.
    std::unique_ptr<BlockNode> document_;

    // Link reference definitions extracted from Paragraph string_content
    // during finalization. Keyed by normalised label (case-folded,
    // collapsed whitespace). Fully populated before phase 2 begins.
    std::unordered_map<std::string, LinkDef> ref_map_;

    int line_number_      = 0;  // current 1-based line counter
    int last_line_length_ = 0;  // column length of the previous line

    // ── per-line byte cursor ──────────────────────────────────────────────
    // Tracks the real byte offset through the raw line in parallel with
    // current_col_ (which tracks virtual columns). Both are reset to 0 at
    // the top of every processLine() call. consumeColumns() advances
    // current_byte_ alongside current_col_ so that step3AppendText() always
    // has the exact byte position immediately after the last prefix byte
    // consumed — even when a tab was split mid-indent. Passing current_byte_
    // to appendText() instead of line.next_non_space avoids the bug where
    // ++from_byte would skip a content byte rather than the split tab byte.
    // See §9.3 for the full analysis.
    std::size_t current_byte_ = 0;

    // ── per-line cursor state ─────────────────────────────────────────────
    // All three fields are reset to 0/false at the top of every processLine().

    // Virtual columns remaining from a tab that was partially consumed by a
    // container continuation predicate. Non-zero only between the moment a
    // tab is split by consumeColumns() and the moment its remainder is
    // consumed by the next predicate or materialised by appendText().
    // Written only by consumeColumns(); read and cleared only by appendText().
    std::size_t partial_tab_remaining_ = 0;

    // Real column position of the scan cursor as step 1 walks the spine and
    // consumes container prefixes. Passed as base_col to scanWithOffset() so
    // that inner-block predicates see the correct column position.
    std::size_t current_col_ = 0;

    // Set by step2 when an opener's OpenResult::swallow_line is true (ATX
    // heading, ThematicBreak, fenced-code opener). Checked by step3AppendText
    // to prevent the marker line from being appended as content.
    bool swallow_current_line_ = false;

    // ── component references ──────────────────────────────────────────────
    PreScanner&   scanner_;
    InlineParser& inline_parser_;
};
```

`ref_map_` stores [`LinkDef`](02_data_types.md#22-blockdata-union) entries keyed by normalised label. It is fully populated before phase 2 begins. Label normalisation is described in [§6.2](06_inline_parser.md#62-inlineparser-methods) (`normaliseLabel`) and [§9.5](09_open_decisions.md#95-unicode-case-folding-for-link-reference-labels).

Ownership semantics of `document_` and `spine_` are described in [§8.3](08_data_flow.md#83-ownership-model).

---

## 5.2 SpineMatchResult

```cpp
// Returned by step1WalkSpine(). Carries both indices so step 2 can act
// without re-walking the spine.
struct SpineMatchResult {
    // Spine index of the last block whose continuation predicate passed.
    // Always >= 0 because Document (index 0) always matches.
    std::size_t deepest_matched;

    // Spine index of the first block whose continuation predicate failed.
    // Equal to spine_.size() if every block matched (no unmatched blocks).
    std::size_t first_unmatched;

    // Set when the failing block was a fenced code block and the line is the
    // closing fence. step3AppendText returns early without appending the fence
    // line to any block's string_content.
    bool swallow_line = false;
};
```

---

## 5.3 Per-line loop — three steps

```cpp
void SpineHandler::processLine(std::string_view raw)
{
    // Reset ephemeral per-line cursor state.
    partial_tab_remaining_ = 0;
    current_col_           = 0;
    current_byte_          = 0;
    swallow_current_line_  = false;
    ++line_number_;

    ScannedLine line = scanner_.scan(raw);

    SpineMatchResult match = step1WalkSpine(line);
    step2NewBlocks(line, match);
    step3AppendText(line, match);
    checkHtmlBlockEnd(line);          // post-append end-condition check for HTML types 1–5

    // Track blank lines for loose-list detection.
    if (!spine_.empty())
        tip()->last_line_blank = line.is_blank;
}

// ── Step 1 ────────────────────────────────────────────────────────────────
// Walk the spine top-down. Delegate each block's continuation predicate to
// block_rules::continuationMatches. Advance column cursor for matched blocks.
// INVARIANT: no closeBlock() call anywhere in this function.

SpineMatchResult SpineHandler::step1WalkSpine(const ScannedLine& line)
{
    SpineMatchResult result;
    result.deepest_matched = 0;
    result.first_unmatched = spine_.size(); // optimistic: all match

    for (std::size_t i = 1; i < spine_.size(); ++i) {
        auto cr = block_rules::continuationMatches(*spine_[i], line);
        if (cr.matched) {
            if (cr.cols_to_consume > 0)
                consumeColumns(line.content, current_byte_, cr.cols_to_consume);
            result.deepest_matched = i;
        } else {
            result.first_unmatched = i;
            if (cr.swallow_line) result.swallow_line = true;  // fenced-code closing fence
            break;
        }
    }
    return result;
}

// ── Step 2 ────────────────────────────────────────────────────────────────
// tryOpenNewBlock runs a container loop: it calls block_rules::tryOpen
// repeatedly, rescanning the remaining line after each container opener.
// The loop breaks on a leaf opener (ATX, fenced code, thematic break, HTML,
// indented code) or when no opener fires.
//
// block_rules::tryOpen does NOT return a Paragraph fallback. The implicit
// Paragraph is opened in step 3 when the tip is still a container block after
// all openers have been attempted.

bool SpineHandler::tryOpenNewBlock(const ScannedLine& line, const SpineMatchResult& match)
{
    bool any_opened = false;
    // Start from the current cursor position (already advanced by step 1).
    ScannedLine cur = scanner_.scanWithOffset(
        line.content.substr(current_byte_), current_col_);

    while (true) {
        const bool tip_para = (tip()->type == NodeType::Paragraph);

        // Detect list-item blank-line continuation (suppresses indented code, §3.2 #7).
        bool inside_list_blank = false;
        for (auto it = spine_.rbegin(); it != spine_.rend(); ++it) {
            const NodeType t = (*it)->type;
            if (t == NodeType::Item) { inside_list_blank = (*it)->last_line_blank; break; }
            if (t != NodeType::List) break;
        }

        auto result = block_rules::tryOpen(cur, tip_para, inside_list_blank);
        if (!result) break;

        if (!any_opened) {
            closeUnmatched(match.first_unmatched);  // deferred close from step 1
            any_opened = true;
        }

        // For Item openers: open a new List container if the current tip is not
        // a compatible List (same list_type, delimiter, bullet_char).
        if (result->type == NodeType::Item && result->list_data.has_value()) {
            const auto& new_ld = *result->list_data;
            bool need_new_list = (tip()->type != NodeType::List);
            if (!need_new_list) {
                const auto& cur_ld = std::get<ListData>(tip()->data);
                need_new_list = cur_ld.list_type  != new_ld.list_type
                             || cur_ld.delimiter   != new_ld.delimiter
                             || (new_ld.list_type == ListType::Bullet
                                 && cur_ld.bullet_char != new_ld.bullet_char);
            }
            if (need_new_list) openBlock(NodeType::List, new_ld);
        }

        BlockNode* new_node = openBlock(result->type, result->data);
        if (!result->extracted_content.empty())
            new_node->string_content = result->extracted_content;  // ATX heading text
        if (result->cols_consumed > 0)
            consumeColumns(line.content, current_byte_, result->cols_consumed);
        if (result->swallow_line) {
            swallow_current_line_ = true;   // prevent step3 from appending
            break;
        }

        // Only container openers re-enter the loop.
        const bool is_container = result->type == NodeType::BlockQuote
                                || result->type == NodeType::List
                                || result->type == NodeType::Item;
        if (!is_container) break;

        // Rescan remaining content with updated column base for the next opener.
        cur = scanner_.scanWithOffset(line.content.substr(current_byte_), current_col_);
    }

    return any_opened;
}

void SpineHandler::step2NewBlocks(const ScannedLine& line, const SpineMatchResult& match)
{
    const bool new_block_found = tryOpenNewBlock(line, match);
    // closeUnmatched already called inside tryOpenNewBlock when any_opened.
    if (!new_block_found && !incorporatesLazyContinuation(line, match))
        closeUnmatched(match.first_unmatched);
}

// ── Step 3 ────────────────────────────────────────────────────────────────

void SpineHandler::step3AppendText(const ScannedLine& line, const SpineMatchResult& match)
{
    // Swallow conditions: fenced-code closing fence (step 1) or a single-line
    // opener such as ATX heading or ThematicBreak (step 2).
    if (match.swallow_line || swallow_current_line_) return;
    if (line.is_blank) return;

    // Setext underline: promote the tip Paragraph to a Heading and close it.
    // Uses block_rules::isSetextUnderline internally.
    if (tryPromoteSetextHeading(line)) return;

    // Implicit Paragraph: if the tip is still a container block after step 2,
    // open a Paragraph to receive the text. This replaces the Paragraph
    // "fallback" that would otherwise live inside block_rules::tryOpen.
    {
        const NodeType tt = tip()->type;
        if (tt == NodeType::Document || tt == NodeType::BlockQuote
                || tt == NodeType::List || tt == NodeType::Item)
            openBlock(NodeType::Paragraph, std::monostate{});
    }

    // Append content. current_byte_ is correct even when a tab was split; see §9.3.
    appendText(line.content, current_byte_);
}
```

The continuation predicates are described in [§3.1](03_continuation_rules.md#31-continuation-rules-per-block-type) and implemented in [§10.2](10_block_rules.md#102-continuationmatches). The open-block rules are described in [§3.2](03_continuation_rules.md#32-open-block-rules--step-2-triggers) and implemented in [§10.5](10_block_rules.md#105-tryopen).

---

## 5.4 Tree mutation primitives

```cpp
// ── openBlock ─────────────────────────────────────────────────────────────
//
// Allocate a new BlockNode and push it onto the spine. The spine takes
// ownership via unique_ptr. Tree attachment (into the parent's children
// vector) is deferred to closeBlock(), not done here.

BlockNode* SpineHandler::openBlock(NodeType type, BlockData data)
{
    auto node_ptr    = std::make_unique<BlockNode>();
    BlockNode* node  = node_ptr.get();
    node->type       = type;
    node->data       = std::move(data);
    node->start_line = line_number_;
    node->is_open    = true;

    spine_.push_back(std::move(node_ptr));
    return node;
}

// ── closeBlock ────────────────────────────────────────────────────────────
//
// Finalise the tip block (spine_.back()):
//   1. Move ownership out of the spine.
//   2. Per-type finalization hooks (before recording end_line):
//      a. Paragraph: scan string_content for link reference definitions
//         (maybeScanLinkRefDefs), extract into ref_map_, trim matched content.
//         Placed here — not in step3AppendText — so that paragraphs closed by
//         a blank line (which skips step 3) are also scanned. See §9.2.
//      b. Indented CodeBlock: strip trailing blank lines from string_content
//         (spec §4.4). A "blank line" in string_content is a '\n' preceded
//         only by spaces. Strip from the end until a non-blank line is reached.
//         Fenced code blocks are NOT stripped — their closing fence already
//         delimits the content.
//   3. Record end_line and mark is_open = false.
//   4a. If a parent exists (spine non-empty): append to parent's children.
//   4b. If closing the Document root (spine now empty): capture in document_.
//
// Inline parsing is NOT triggered here — that is phase 2, via finalize().

void SpineHandler::closeBlock()
{
    auto node = std::move(spine_.back());
    spine_.pop_back();

    // Paragraph: extract link reference definitions before any other cleanup,
    // so that paragraphs closed by a blank line (which skips step 3) are also
    // scanned. See §9.2.
    if (node->type == NodeType::Paragraph)
        maybeScanLinkRefDefs(node.get());

    // Type-specific finalization delegated to the block_rules module (§10.7):
    //   IndentedCodeBlock → strip trailing blank lines
    //   SetextHeading     → strip leading/trailing blank lines
    block_rules::onClose(*node);

    node->end_line = line_number_;
    node->is_open  = false;

    if (!spine_.empty()) {
        spine_.back()->children.push_back(std::move(node));
    } else {
        document_ = std::move(node);   // Document root captured for releaseDocument()
    }
}

// ── closeUnmatched ────────────────────────────────────────────────────────
//
// Close spine[from_index..] tip-first. Child blocks must be closed before
// their parents to ensure end_line is recorded correctly on each.

void SpineHandler::closeUnmatched(std::size_t from_index)
{
    while (spine_.size() > from_index) {
        closeBlock();
    }
}

// ── appendText ────────────────────────────────────────────────────────────
//
// Append content to the tip's string_content.
// If partial_tab_remaining_ > 0, a tab was split by the most recent
// consumeColumns() call. The remaining virtual columns are materialised as
// space characters here — this is the only place virtual spaces are written
// into string_content. The raw tab byte is skipped (from_byte advances by 1).
//
// Line separator convention: for leaf blocks that accumulate multiple lines
// (Paragraph, Heading), appendText() appends a '\n' after each line's content.
// The InlineParser's parseInline() dispatches on '\n' to emit SoftBreak nodes
// (or LineBreak if the preceding content ends with two spaces or a backslash).
// Container blocks and CodeBlock/HtmlBlock are not inline-parsed, so the '\n'
// is either structural (containers never call appendText) or literal (code/HTML
// blocks preserve newlines verbatim as part of their raw string_content).

void SpineHandler::appendText(std::string_view line, std::size_t from_byte)
{
    BlockNode* t = tip();
    if (partial_tab_remaining_ > 0) {
        // Emit the leftover virtual space from the split tab.
        t->string_content.append(partial_tab_remaining_, ' ');
        partial_tab_remaining_ = 0;
        ++from_byte;    // skip the raw tab byte whose columns are now spent
    }
    t->string_content += line.substr(from_byte);
    t->string_content += '\n';  // line separator; parsed as SoftBreak/LineBreak by InlineParser
}

// ── tip ───────────────────────────────────────────────────────────────────

BlockNode* SpineHandler::tip() const noexcept
{
    return spine_.back().get();
}

// ── incorporatesLazyContinuation ─────────────────────────────────────────
//
// Returns true when the lazy continuation rule applies:
//   - at least one ancestor block failed continuation (first_unmatched < spine_.size())
//   - the current tip is an open Paragraph
//   - the line is not blank
//   - the line is NOT a setext underline (spec §5.1: a setext underline never
//     acts as a lazy continuation; the missing '>' closes the BlockQuote and
//     the '---' becomes a thematic break instead of promoting the paragraph)

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine&     line,
    const SpineMatchResult& match) const noexcept
{
    return !line.is_blank
        && match.first_unmatched < spine_.size()
        && tip()->type == NodeType::Paragraph
        && !block_rules::isSetextUnderline(line);
}
```

Ownership model: `spine_` holds `unique_ptr<BlockNode>` for every open block. `closeBlock()` transfers ownership to the parent's `children` vector (or to `document_` for the root). After `finalize()` the entire tree is owned transitively by `document_`. See [§8.3](08_data_flow.md#83-ownership-model).

---

## 5.5 Tab accounting

```cpp
// ── consumeColumns ────────────────────────────────────────────────────────
//
// The single authoritative site for partial-tab decisions.
// Advances byte_offset through line by exactly n_cols virtual columns,
// updating current_col_ and current_byte_ along the way.
//
// If a tab straddles the n_cols boundary:
//   - byte_offset is NOT advanced past the tab byte
//   - partial_tab_remaining_ is set to the unused portion of that tab
//   - current_col_ is advanced by the cols consumed
//
// The next call to consumeColumns() or appendText() will drain
// partial_tab_remaining_ before touching any new bytes.
//
// current_byte_ is updated to byte_offset at every step so that
// step3AppendText() can read the correct content-start position even when
// a tab was split mid-indent. See §9.3.
//
// Returns the new byte_offset.

std::size_t SpineHandler::consumeColumns(
    std::string_view line,
    std::size_t      byte_offset,
    std::size_t      n_cols)
{
    std::size_t cols_needed = n_cols;

    // Phase A: drain any leftover virtual space from a prior split on this line.
    if (partial_tab_remaining_ > 0) {
        const std::size_t take = std::min(partial_tab_remaining_, cols_needed);
        partial_tab_remaining_ -= take;
        cols_needed            -= take;
        current_col_           += take;
        // byte_offset is not advanced: we are still on the same tab byte.
    }

    // Phase B: walk forward consuming spaces and whole or partial tabs.
    while (cols_needed > 0 && byte_offset < line.size()) {
        const unsigned char byte = static_cast<unsigned char>(line[byte_offset]);
        if (byte == ' ') {
            ++byte_offset;
            --cols_needed;
            ++current_col_;
        } else if (byte == '\t') {
            const std::size_t tab_w =
                (current_col_ / 4 + 1) * 4 - current_col_;
            if (tab_w <= cols_needed) {
                // Whole tab fits within the remaining budget.
                ++byte_offset;
                cols_needed  -= tab_w;
                current_col_ += tab_w;
            } else {
                // Tab straddles the boundary — partial consumption.
                // Record the unused portion; do not advance byte_offset
                // so that appendText() can find the tab byte and skip it.
                partial_tab_remaining_ = tab_w - cols_needed;
                current_col_ += cols_needed;
                cols_needed   = 0;
            }
        } else {
            break; // non-whitespace: budget exhausted or no indent
        }
    }
    current_byte_ = byte_offset;  // keep byte cursor in sync with col cursor (§9.3)
    return byte_offset;
}
```

The virtual column arithmetic used here is specified in [§7.1](07_tab_algorithm.md#71-virtual-column-arithmetic). A complete worked example of tab splitting is in [§7.3](07_tab_algorithm.md#73-worked-example--spec-22-example-5).

---

## 5.6 finalize and the phase boundary

```cpp
// ── finalize ──────────────────────────────────────────────────────────────
//
// Called once after all input lines have been processed via processLine().
//
// Phase 1 completion: drain all remaining open blocks tip-first. The Document
// root is closed last.
//
// Phase 2 initiation: walk the completed tree and call InlineParser::parse()
// on each leaf block. ref_map_ is now fully populated — all link reference
// definitions in the document have been extracted during the phase 1 loop.
// Inline parsing of one block does not affect any other block, so this walk
// is parallelisable in principle.

void SpineHandler::finalize()
{
    // Close all blocks tip-first, including Document.
    // Each closeBlock() moves the node from spine_ into its parent's children
    // (or into document_ for the Document root).
    while (!spine_.empty()) {
        closeBlock();
    }

    // Phase 2: walk the completed tree and inline-parse every leaf block.
    parseInlineContent(document_.get());
}

// Recursive helper for finalize(). Visits the tree depth-first.
// Container blocks are recursed into; leaf blocks are inline-parsed.
// ThematicBreak, and any container type, are not passed to InlineParser
// because they carry no string_content.

void SpineHandler::parseInlineContent(BlockNode* node)
{
    // Only Paragraph and Heading have inline structure.
    // CodeBlock and HtmlBlock are raw literal content — they must NOT be
    // passed to the inline parser (spec §4.5, §4.6).
    const bool needs_inline =
        node->type == NodeType::Paragraph ||
        node->type == NodeType::Heading;

    const bool is_container =
        node->type == NodeType::Document   ||
        node->type == NodeType::BlockQuote ||
        node->type == NodeType::List       ||
        node->type == NodeType::Item;

    if (needs_inline) {
        // string_content is complete and ref_map_ is fully populated.
        inline_parser_.parse(node, ref_map_);
    } else if (is_container) {
        for (const auto& child : node->children) {
            parseInlineContent(child.get());
        }
    }
    // CodeBlock and HtmlBlock: leave inline_children = nullptr; string_content
    // is used verbatim as the rendered output.
}
```

`finalize()` marks the phase boundary. After it returns, `ref_map_` is fully populated and [`InlineParser::parse()`](06_inline_parser.md#62-inlineparser-methods) has been called on every eligible leaf. The complete data flow is described in [§8](08_data_flow.md).

---

← [4. PreScanner](04_prescanner.md) | [Index](index.md) | Next: [6. InlineParser — phase 2](06_inline_parser.md) →
