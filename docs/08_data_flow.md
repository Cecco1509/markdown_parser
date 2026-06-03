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
  │
  ├─ step 1: step1WalkSpine()
  │    Walk spine top-down. Test continuation predicates via consumeColumns().
  │    Update current_col_ and partial_tab_remaining_ as prefixes are consumed.
  │    Produce SpineMatchResult { deepest_matched, first_unmatched }.
  │    NO blocks closed here.
  │
  ├─ step 2: step2NewBlocks()
  │    new block found?       → closeUnmatched() + openBlock()
  │                               openBlock() pushes onto spine_
  │    lazy continuation?     → keep unmatched open, no close, no open
  │    neither?               → closeUnmatched()
  │
  └─ step 3: step3AppendText()
       setext underline?      → tryPromoteSetextHeading(), return
       link ref def?          → maybeScanLinkRefDefs(), extract into ref_map_
       normal?                → appendText() → tip()->string_content
                                  if partial_tab_remaining_ > 0:
                                    emit spaces, skip tab byte, clear flag
                                  append '\n' as line separator

  [repeat for every line]

  ▼ SpineHandler::finalize()
  │
  ├─ phase 1 completion
  │    closeBlock() × N  (drain spine tip-first, set is_open=false, record end_line)
  │    ref_map_ is now fully populated
  │
  └─ phase 2: parseInlineContent()
       depth-first tree walk
       for each leaf block (Paragraph, Heading, CodeBlock, HtmlBlock):
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
- [`SpineHandler`](05_spine_handler.md) — drives the per-line loop and phase boundary
- [`InlineParser`](06_inline_parser.md) — phase 2 leaf-block processing
- [Tab algorithm](07_tab_algorithm.md) — governs `consumeColumns()` and `appendText()`

---

## 8.2 Lifecycle summary

```
openBlock()
  ├─ allocate BlockNode (make_unique)
  ├─ push onto spine_               ← SPINE OWNS via unique_ptr
  └─ tree attachment DEFERRED to closeBlock()

processLine() [per line]
  └─ appendText() accumulates string_content on tip()
       partial_tab_remaining_ → spaces prepended if tab was split

closeBlock()
  ├─ move unique_ptr out of spine_.back(), pop spine_
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

> **Note on `BlockNode` destruction:** `vector<unique_ptr<BlockNode>> children`
> causes `~BlockNode()` to be called recursively — each destructor destroys its
> children, which destroys their children, and so on. For deeply nested documents
> (e.g. thousands of nested block quotes) this risks stack overflow. An iterative
> destructor or arena allocator is recommended; see [§9.1](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode).

The open design decision on memory ownership is [§9.1](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode).

---

← [7. Tab algorithm](07_tab_algorithm.md) | [Index](index.md) | Next: [9. Open design decisions](09_open_decisions.md) →
