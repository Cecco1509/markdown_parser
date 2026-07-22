> # ⚠️ SUPERSEDED — ORIGINAL DESIGN SPEC
>
> This file is the **pre-implementation design specification**, kept for
> historical reference and for the AI-usage report (it records what was
> *planned* before the code existed). It does **not** describe the code as
> built — names, file layout and data structures have since changed.
>
> **See the current documentation in [`docs/`](../index.md).**

---

# 8. Data flow through phases

← [7. Tab algorithm](07_tab_algorithm.md) | [Index](index.md) | Next: [9. Open design decisions](09_open_decisions.md) →

---

## 8.1 End-to-end pipeline

```
raw input line (std::string_view)
  │
  ▼ PreScanner::scan()
ScannedLine  { content, indent, virtual_indent, next_non_space, is_blank }
  │           (no classification — raw bytes only)
  │
  ▼ SpineHandler::processLine()
  │   reset: partial_tab_remaining_, current_col_, current_byte_,
  │           swallow_current_line_
  │
  ├─ step 1: step1WalkSpine()
  │    Walk spine[1..] top-down.
  │    For each block call block_rules::continuationMatches(node, line):
  │      matched  → consumeColumns(cols_to_consume); deepest_matched = i
  │      !matched → first_unmatched = i
  │                 swallow_line = cr.swallow_line  (fenced-code closing fence)
  │                 break
  │    Produce SpineMatchResult { deepest_matched, first_unmatched, swallow_line }.
  │    NO blocks closed here.
  │
  ├─ step 2: step2NewBlocks()  [tryOpenNewBlock — container loop]
  │    cur = scanner_.scanWithOffset(line.content[current_byte_..], current_col_)
  │    loop:
  │      block_rules::tryOpen(cur, tip_is_paragraph, inside_list_blank)
  │        → nullopt                  break (no opener)
  │        → OpenResult (container)   if first iter: closeUnmatched(first_unmatched)
  │                                   open List if needed (for Item)
  │                                   openBlock(type, data)
  │                                   set node->string_content = extracted_content
  │                                   consumeColumns(cols_consumed)
  │                                   cur = scanner_.scanWithOffset(remaining, current_col_)
  │                                   continue loop
  │        → OpenResult (leaf)        same open steps
  │                                   if swallow_line: swallow_current_line_ = true
  │                                   break
  │    if no opener found and not lazy continuation: closeUnmatched(first_unmatched)
  │
  ├─ step 3: step3AppendText()
  │    swallow_current_line_ || match.swallow_line?  → return (line consumed silently)
  │    is_blank?                                     → return
  │    tryPromoteSetextHeading(line)?
  │      isSetextUnderline → promote tip Paragraph to Heading, closeBlock(), return
  │    tip is container (Document/BlockQuote/List/Item)?
  │      → openBlock(Paragraph)       implicit Paragraph for non-blank, non-opener lines
  │    appendText(line.content, current_byte_) → tip()->string_content += content + '\n'
  │      partial_tab_remaining_ > 0: prepend spaces, skip tab byte, clear flag
  │
  └─ checkHtmlBlockEnd(line)          [post-step-3]
       tip is HtmlBlock types 1–5?
         block_rules::htmlBlockEndMet(node, line.content)?
           → closeBlock()

  tip()->last_line_blank = line.is_blank

  [repeat for every line]

  ▼ SpineHandler::finalize()
  │
  ├─ phase 1 completion
  │    closeBlock() × N  (drain spine tip-first, set is_open=false, record end_line)
  │    each closeBlock() calls:
  │      maybeScanLinkRefDefs()  for Paragraph
  │      block_rules::onClose()  for type-specific finalization
  │    ref_map_ is now fully populated
  │
  └─ phase 2: parseInlineContent()
       depth-first tree walk
       for each leaf block (Paragraph, Heading):
         InlineParser::parse(block, ref_map_)
           scan string_content character by character
           build InlineNode children vector
           resolve backtick spans, autolinks, raw HTML
           handle bracket stack → Link / Image nodes
           handle delimiter stack → Emph / Strong nodes (processEmphasis)
           push results into block->inline_children
```

Key components in this pipeline:
- [`PreScanner`](04_prescanner.md) — produces `ScannedLine`
- [`block_rules`](10_block_rules.md) — stateless predicate/descriptor module for continuation, open, and close rules
- [`SpineHandler`](05_spine_handler.md) — orchestrates the per-line loop and the phase boundary
- [`InlineParser`](06_inline_parser.md) — phase 2 leaf-block processing
- [Tab algorithm](07_tab_algorithm.md) — governs `consumeColumns()` and `appendText()`

---

## 8.2 Lifecycle summary

```
openBlock()
  ├─ allocate BlockNode (make_unique)
  ├─ push onto spine_                  ← SPINE OWNS via unique_ptr
  ├─ assign extracted_content if set   (ATX heading text)
  └─ tree attachment DEFERRED to closeBlock()

processLine() [per line]
  └─ appendText() accumulates string_content on tip()
       partial_tab_remaining_ → spaces prepended if tab was split

closeBlock()
  ├─ move unique_ptr out of spine_.back(), pop spine_
  ├─ per-type finalization (before end_line):
  │    Paragraph  → maybeScanLinkRefDefs()   extracts into ref_map_, trims content
  │                 (here, not step 3 — covers blank-line closure too — §9.2)
  │    any type   → block_rules::onClose()
  │                   IndentedCodeBlock: stripTrailingBlankLines()
  │                   SetextHeading: strip leading/trailing blank lines
  ├─ record end_line, set is_open = false
  └─ if parent exists: parent->children.push_back(std::move(node))  ← TREE ATTACH
     else (Document root): document_ = std::move(node)
     (InlineParser NOT called here)

finalize()
  ├─ closeBlock() × N              drain remaining spine tip-first
  └─ parseInlineContent()          phase 2 — ref_map_ complete
       └─ InlineParser::parse()    per leaf block
```

The primitives `openBlock`, `closeBlock`, and `appendText` are implemented in [§5.4](05_spine_handler.md#54-tree-mutation-primitives). The phase boundary (`finalize`) is in [§5.6](05_spine_handler.md#56-finalize-and-the-phase-boundary).

---

## 8.3 Ownership model

- `SpineHandler::document_` — `std::unique_ptr<BlockNode>`. Owns the Document
  root and transitively every `BlockNode` in the tree through the
  `children: vector<unique_ptr<BlockNode>>` chains on each node.
- `SpineHandler::spine_` — `std::vector<std::unique_ptr<BlockNode>>`. Owns every
  currently-open block. `closeBlock()` transfers ownership out of `spine_` into
  the parent's `children` vector (or into `document_` for the root). After
  `finalize()` the spine is empty.
- `BlockNode::inline_children` — `std::vector<std::unique_ptr<InlineNode>>`.
  Owned inline nodes, populated by `InlineParser::parse()` during phase 2.
  Empty for container blocks and `ThematicBreak`.
- `ScannedLine::content` — `std::string_view`. Non-owning slice into the raw
  input buffer passed to `processLine()`. Valid only for the duration of the
  `processLine()` call.
- `InlineParser::ref_map_` — raw const pointer. Non-owning reference to
  `SpineHandler::ref_map_`. Valid for the duration of `finalize()`.
- `block_rules` functions — all accept `const BlockNode&` or `BlockNode&`.
  They do not own nodes and hold no references beyond the call.

> **Note on `BlockNode` destruction:** `vector<unique_ptr<BlockNode>> children`
> causes `~BlockNode()` to be called recursively — each destructor destroys its
> children, which destroys their children, and so on. For deeply nested documents
> (e.g. thousands of nested block quotes) this risks stack overflow. An iterative
> destructor or arena allocator is recommended; see [§9.1](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode).

The open design decision on memory ownership is [§9.1](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode).

---

← [7. Tab algorithm](07_tab_algorithm.md) | [Index](index.md) | Next: [9. Open design decisions](09_open_decisions.md) →
