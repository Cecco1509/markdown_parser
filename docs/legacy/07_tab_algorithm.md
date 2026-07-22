> # ⚠️ SUPERSEDED — ORIGINAL DESIGN SPEC
>
> This file is the **pre-implementation design specification**, kept for
> historical reference and for the AI-usage report (it records what was
> *planned* before the code existed). It does **not** describe the code as
> built — names, file layout and data structures have since changed.
>
> **See the current documentation in [`docs/`](../index.md).**

---

# 7. Tab algorithm

← [6. InlineParser — phase 2](06_inline_parser.md) | [Index](index.md) | Next: [8. Data flow through phases](08_data_flow.md) →

---

Tabs in CommonMark are never expanded to space characters in the source text.
However, for the purpose of block-structure decisions (indentation thresholds,
continuation indents, list padding), a tab behaves as if it advances the column
to the next multiple of 4. This virtual expansion governs structural logic only;
the raw bytes stored in `string_content` may contain literal tab characters,
except where a tab is split across a container boundary (see [§7.2](#72-partial-tab-splitting)).

This arithmetic is implemented in [`PreScanner::computeVirtualIndent()`](04_prescanner.md#42-prescanner-methods) and [`SpineHandler::consumeColumns()`](05_spine_handler.md#55-tab-accounting).

---

## 7.1 Virtual column arithmetic

Walking leading whitespace left to right from a starting column `base_col`:

```
col = base_col
for each byte in content:
    if byte == ' ':   col += 1
    if byte == '\t':  col = (col / 4 + 1) * 4
    else:             break
virtual_indent = col
```

The raw bytes of `content` are never modified. Only the virtual column counter
uses this arithmetic.

---

## 7.2 Partial tab splitting

When a container continuation predicate consumes N virtual columns and a tab
straddles the N-column boundary, [`consumeColumns()`](05_spine_handler.md#55-tab-accounting) handles the split:

- The columns consumed up to the boundary satisfy the predicate.
- `partial_tab_remaining_` is set to the number of virtual columns remaining
  in that tab (i.e. `tab_width − columns_used_from_it`).
- The byte offset is **not** advanced past the tab byte.

When [`appendText()`](05_spine_handler.md#54-tree-mutation-primitives) is subsequently called to write content to `string_content`:

- If `partial_tab_remaining_ > 0`, emit that many space characters into
  `string_content` (the leftover virtual space becomes real spaces in content).
- Advance `from_byte` by 1 to skip the raw tab byte that was split.
- Clear `partial_tab_remaining_`.
- Append the remaining raw bytes normally.

This is the only place virtual columns are materialised as space characters.

> **Note:** the `from_byte` argument passed to `appendText()` must be the current byte cursor position, not `line.next_non_space`. See [§9.3](09_open_decisions.md#93-appendtext-from_byte-when-a-tab-is-split) for details.

---

## 7.3 Worked example — spec §2.2 example 5

Input: `- foo\n\n\t\tbar\n`  
Expected: `<ul><li><p>foo</p><pre><code>  bar\n</code></pre></li></ul>`

**Line 3: `\t\tbar`**

Pre-scan: `virtual_indent = 8` (tab → col 4, tab → col 8), `next_non_space = 2`.  
Reset: `partial_tab_remaining_ = 0`, `current_col_ = 0`.

| byte | col before | col after | action |
|---|---|---|---|
| `[0] \t` | 0 | 2 | Item needs `padding=2`. Tab width = 4. 4 > 2. **Split:** `partial_tab_remaining_ = 2`. `byte_offset` stays 0. |
| `[0] \t` (cont.) | 2 | 4 | Code block needs 4 cols. Phase A drains partial (2 cols). `byte_offset → 1`. |
| `[1] \t` | 4 | 6 | Code block needs 2 more cols. Tab width = 4. 4 > 2. **Split:** `partial_tab_remaining_ = 2`. `byte_offset` stays 1. |
| `[1] \t` (content) | — | — | `appendText`: emit `"  "` (2 spaces). Skip byte 1. Append `"bar"`. `string_content = "  bar"`. |

The two leading spaces in the output are produced entirely from the second tab
being split — 2 of its 4 virtual columns satisfied the code block's indent
requirement; the remaining 2 became content spaces.

This example exercises both phases of [`consumeColumns()`](05_spine_handler.md#55-tab-accounting): Phase A (draining a prior split) and Phase B (new partial split).

---

← [6. InlineParser — phase 2](06_inline_parser.md) | [Index](index.md) | Next: [8. Data flow through phases](08_data_flow.md) →
