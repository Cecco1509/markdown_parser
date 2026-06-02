# 9. Open design decisions

← [8. Data flow through phases](08_data_flow.md) | [Index](index.md)

---

These items were identified as ambiguous or unresolved during specification review.
They **must** be decided before the corresponding component is implemented.

---

## 9.1 Memory ownership — BlockNode and InlineNode (SOLVED)

**Problem:** `openBlock` (see [§5.4](05_spine_handler.md#54-tree-mutation-primitives)) allocates each `BlockNode` with `raw new`. The tree is
nominally owned via parent→child linked-list chains, but `std::unique_ptr<BlockNode>
document_` only frees the Document root. Every other node leaks. The same applies
to `InlineNode` objects created by `InlineParser::makeNode()` (see [§6.2](06_inline_parser.md#62-inlineparser-methods)).

**Options (choose one):**

A. **Iterative destructor on `BlockNode`:** `~BlockNode()` walks `first_child → next`
   iteratively (not recursively) and `delete`s each child, then recurses into the
   now-empty `first_child = nullptr` state. This avoids stack overflow for deep trees.
   `InlineNode` gets a similar iterative destructor chaining through `next` and `children`.

B. **Arena / `std::pmr::monotonic_buffer_resource`:** `SpineHandler` owns a
   `std::pmr::monotonic_buffer_resource`; all `BlockNode` and `InlineNode` objects are
   allocated from it. The entire arena is freed at `SpineHandler` destruction. No
   destructors needed on individual nodes. Recommended for high-throughput use.

See also [§8.3](08_data_flow.md#83-ownership-model) for the ownership model overview.

---

## 9.2 `maybeScanLinkRefDefs` — when to scan

**Problem:** The spec places `maybeScanLinkRefDefs()` in `step3AppendText` (see [§5.3](05_spine_handler.md#53-per-line-loop--three-steps)), which is
skipped for blank lines (`if (line.is_blank) return`). A paragraph closed by a blank
line therefore never has its link reference definitions extracted during step 3.

**Resolution:** Link reference definition scanning must happen inside `closeBlock()`
for Paragraph nodes (or a dedicated `finalizeBlock()` hook called from `closeBlock`),
not in `step3AppendText`. The step 3 placement described in [§5.3](05_spine_handler.md#53-per-line-loop--three-steps) should be removed.
The correct call site is:

```cpp
void SpineHandler::closeBlock(BlockNode* node) {
    if (node->type == NodeType::Paragraph)
        scanLinkRefDefs(node);   // extracts into ref_map_, trims string_content
    node->end_line = line_number_;
    node->is_open  = false;
    spine_.erase(std::find(spine_.begin(), spine_.end(), node));
}
```

`ref_map_` is referenced in [§5.1](05_spine_handler.md#51-state). `LinkDef` is defined in [§2.2](02_data_types.md#22-blockdata-union).

---

## 9.3 `appendText` `from_byte` when a tab is split

**Problem:** [§5.3](05_spine_handler.md#53-per-line-loop--three-steps) calls `appendText(line.content, line.next_non_space)`. The `appendText`
implementation does `++from_byte` to skip the split tab byte. But `next_non_space` is the
byte of the first **non-whitespace** character (already past all indent bytes). If the split
tab is in the indent region, `++from_byte` would skip a content byte, not the tab.

The [§7.3](07_tab_algorithm.md#73-worked-example--spec-22-example-5) worked example is internally consistent because `from_byte` passed to `appendText`
is the position of the split tab (byte 1), not `next_non_space` (byte 2). The pseudocode
in [§5.3](05_spine_handler.md#53-per-line-loop--three-steps) is wrong about which value to pass.

**Resolution:** `step3AppendText` (and more broadly, any caller of `appendText`) must pass
the **current byte cursor position** maintained by the step-1/step-2 walk — the position
immediately after the last byte consumed by `consumeColumns` — not `line.next_non_space`.
`SpineHandler` should maintain a `current_byte_` field (analogous to `current_col_`) that
is updated by `consumeColumns` alongside `current_col_`, so the correct offset is always
available.

See [`consumeColumns()`](05_spine_handler.md#55-tab-accounting) and [`appendText()`](05_spine_handler.md#54-tree-mutation-primitives) for the implementation context.

---

## 9.4 `processEmphasis` / bracket deactivation interaction

**Problem:** When a `[` opener turns out to be invalid (no matching link found),
`BracketEntry::delim_top` (see [§2.5](02_data_types.md#25-inlinenode-delimiter-bracketentry)) is used to deactivate delimiters. The spec (§6.2) mentions
`delim_top` but does not specify:

- Whether `delim_top` is a **count** (stack size at `[` time) or an **index** (first
  delimiter added after `[`).
- Which branch of `handleBracketCloser` triggers deactivation vs. the "emit `]` as Text"
  path.

**Resolution (per CommonMark appendix algorithm):**
- `delim_top` is the **size** of `delimiters_` at the moment `[` is pushed. It is used as
  the exclusive lower bound: deactivate all entries in `delimiters_[delim_top .. top]`.
- Deactivation means setting `can_open = false` and `can_close = false` on those entries.
- Deactivation happens when `handleBracketCloser` concludes that the bracket opener is
  not a valid link/image (i.e., no inline/reference/collapsed/shortcut form is found, and
  the entry is popped from the bracket stack as failed).
- `processEmphasis(stack_bottom)` is called with `stack_bottom = bracket.delim_top` when
  a **valid** link/image is resolved, to process emphasis within the link's content before
  the link node is constructed.

Relevant methods: [`handleBracketOpener`, `handleBracketCloser`, `processEmphasis`](06_inline_parser.md#62-inlineparser-methods).

---

## 9.5 Unicode case folding for link reference labels

**Problem:** `normaliseLabel` (see [§6.2](06_inline_parser.md#62-inlineparser-methods)) must apply Unicode case folding (spec §4.7). C++20's
standard library provides no Unicode case-folding function.

**Options (choose one):**

A. **ASCII-only `tolower`:** Non-conformant for labels containing non-ASCII letters (e.g.
   `[Ñoño]` vs `[ñoño]`). Acceptable if the test suite does not exercise non-ASCII labels.

B. **Embed a minimal case-folding table:** Include the Unicode simple case-fold mapping
   (a ~1400-entry lookup table derived from `CaseFolding.txt`) as a generated header.
   This is what `cmark` does.

C. **Use ICU or a small Unicode library:** Adds an external dependency.

**Recommendation:** Option B — embed the simple case-fold table. It covers all CommonMark
spec test cases and avoids external dependencies.

---

← [8. Data flow through phases](08_data_flow.md) | [Index](index.md)
