# 5. SpineHandler — phase 1

← [4. PreScanner](04_prescanner.md) | [Index](index.md) | Next: [6. InlineParser — phase 2](06_inline_parser.md) →

---

The SpineHandler owns the open block stack (the "spine") and implements the
three-step `processLine` algorithm. It is the sole owner of the AST root.

For the block types and data structures it operates on, see [§2](02_data_types.md).  
For the continuation and open/close rules it enforces, see [§3](03_continuation_rules.md).  
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
    int last_line_length_ = 0;  // column length of the previous line;
                                // used for setext heading detection

    // List tightness tracking (spec §5.3):
    // A list becomes loose if any of its constituent items are separated by a
    // blank line, or if any item directly contains two block-level elements
    // separated by a blank line.
    //
    // Implementation: whenever a blank line is processed, walk the spine and
    // set last_line_blank = true on the current tip AND on any Item ancestor.
    // When an Item is closed (closeBlock), if last_line_blank is true on that
    // Item, set ListData::tight = false on the parent List. Similarly, when a
    // new Item opens inside a List, if the previous Item's last_line_blank is
    // true, set tight = false. A List starts with tight = true.

    // ── per-line cursor state ─────────────────────────────────────────────
    // Both fields are reset to 0 at the top of every processLine() call.

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
    ++line_number_;

    ScannedLine line = scanner_.scan(raw);

    // ── Step 1 ────────────────────────────────────────────────────────────
    // Walk the spine top-down. Test each open block's continuation predicate.
    // Record which blocks matched and which did not.
    // INVARIANT: no closeBlock() call happens inside step 1.
    // Closing is deferred to step 2 so the lazy continuation check can be
    // made with full information about what the line contains.
    SpineMatchResult match = step1WalkSpine(line);

    // ── Step 2 ────────────────────────────────────────────────────────────
    // Look for new block openers in the remainder of the line. Decide whether
    // to close unmatched blocks, open new ones, or apply lazy continuation.
    step2NewBlocks(line, match);

    // ── Step 3 ────────────────────────────────────────────────────────────
    // Append remaining text to the deepest open block (the tip after step 2).
    // Handles setext promotion and link reference definition scanning.
    step3AppendText(line, match);
}

// ── Step 1 ────────────────────────────────────────────────────────────────

SpineMatchResult SpineHandler::step1WalkSpine(const ScannedLine& line)
{
    // spine_[0] is Document — always matches; start from index 1.
    SpineMatchResult result;
    result.deepest_matched = 0;
    result.first_unmatched = spine_.size(); // optimistic: all match

    for (std::size_t i = 1; i < spine_.size(); ++i) {
        if (continuationMatches(spine_[i].get(), line)) {
            // Block passes its continuation predicate. consumeColumns() has
            // advanced current_col_ and possibly set partial_tab_remaining_.
            result.deepest_matched = i;
        } else {
            result.first_unmatched = i;
            break;
        }
    }
    return result;
    // No closeBlock() anywhere in this function.
}

// ── Step 2 ────────────────────────────────────────────────────────────────

void SpineHandler::step2NewBlocks(
    const ScannedLine&     line,
    const SpineMatchResult& match)
{
    const bool new_block_found = tryOpenNewBlock(line, match);

    if (new_block_found) {
        // A new block is opening. Now it is safe to close unmatched blocks
        // because we know lazy continuation does not apply.
        closeUnmatched(match.first_unmatched);
        // openBlock() was already called inside tryOpenNewBlock().

    } else if (incorporatesLazyContinuation(line, match)) {
        // Tip is an open Paragraph, the line is not blank, and no new block
        // opened. The unmatched ancestor blocks are intentionally kept open —
        // this is the lazy continuation rule (spec §5.1).
        // Text will be appended to the paragraph tip in step 3.

    } else {
        // No new block, no lazy continuation.
        // Close the unmatched blocks and let text fall into the deepest
        // matched block in step 3.
        closeUnmatched(match.first_unmatched);
    }
}

// ── Step 3 ────────────────────────────────────────────────────────────────

void SpineHandler::step3AppendText(
    const ScannedLine&     line,
    const SpineMatchResult& match)
{
    if (line.is_blank) return;

    // Special case A: setext underline.
    // If the current tip is a Paragraph and this line is a valid setext
    // underline (a run of '=' or '-' with optional trailing spaces, indent
    // 0–3), retroactively promote the Paragraph to a Heading. The underline
    // line is consumed here and nothing is appended to string_content.
    if (tryPromoteSetextHeading(line)) return;

    // Special case B: link reference definition scan.
    // When a Paragraph is being closed (its next line opens a new block or
    // is blank), scan its accumulated string_content for link reference
    // definitions before committing the remaining text. Extracted definitions
    // are inserted into ref_map_. Any leftover content that is not a valid
    // definition stays as the paragraph body.
    maybeScanLinkRefDefs();

    // Normal case: append this line's content to the tip's string_content.
    appendText(line.content, line.next_non_space);
}
```

> **Note:** the placement of `maybeScanLinkRefDefs()` in step 3 is incorrect for paragraphs closed by blank lines. See [§9.2](09_open_decisions.md#92-maybescanlinkRefdefs--when-to-scan) for the resolution.

> **Note:** passing `line.next_non_space` to `appendText()` is incorrect when a tab is split in the indent region. See [§9.3](09_open_decisions.md#93-appendtext-from_byte-when-a-tab-is-split) for the resolution.

The continuation predicates tested in step 1 are described in [§3.1](03_continuation_rules.md#31-continuation-rules-per-block-type). The open-block rules in step 2 are described in [§3.2](03_continuation_rules.md#32-open-block-rules--step-2-triggers).

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
//   2. Record end_line and mark is_open = false.
//   3a. If a parent exists (spine non-empty): append to parent's children.
//   3b. If closing the Document root (spine now empty): capture in document_.
//
// Inline parsing is NOT triggered here — that is phase 2, via finalize().

void SpineHandler::closeBlock()
{
    auto node = std::move(spine_.back());
    spine_.pop_back();

    node->end_line = line_number_;
    node->is_open  = false;

    if (!spine_.empty()) {
        // Transfer ownership to the parent's children vector.
        spine_.back()->children.push_back(std::move(node));
    } else {
        // Closing the Document root — capture it for releaseDocument().
        document_ = std::move(node);
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

bool SpineHandler::incorporatesLazyContinuation(
    const ScannedLine&     line,
    const SpineMatchResult& match) const noexcept
{
    return !line.is_blank
        && match.first_unmatched < spine_.size()
        && tip()->type == NodeType::Paragraph;
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
// updating current_col_ along the way.
//
// If a tab straddles the n_cols boundary:
//   - byte_offset is NOT advanced past the tab byte
//   - partial_tab_remaining_ is set to the unused portion of that tab
//   - current_col_ is advanced by the cols consumed
//
// The next call to consumeColumns() or appendText() will drain
// partial_tab_remaining_ before touching any new bytes.
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
