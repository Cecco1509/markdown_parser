# 3. Continuation, open, and close rules

← [2. Data types and node structures](02_data_types.md) | [Index](index.md) | Next: [4. PreScanner](04_prescanner.md) →

---

All predicates operate on raw bytes from `ScannedLine::content` (see [§4.1](04_prescanner.md#41-scannedline)) and the
`virtual_indent` field. There is no pre-assigned `LineClass`; each predicate
makes its own determination. The same raw line may satisfy different predicates
depending on which block type's predicate is evaluated and in what order.

These rules are applied inside [`SpineHandler::processLine()`](05_spine_handler.md#53-per-line-loop--three-steps) — step 1 tests continuation predicates, step 2 applies open/close rules.

---

## 3.1 Continuation rules per block type

These predicates are tested in **step 1**. A block that fails its predicate is
recorded as unmatched — it is not closed yet (see [§3.3](#33-close-block-rules)).

| Block type | Continuation condition |
|---|---|
| **Document** | Always matches. |
| **BlockQuote** | Line has a `>` marker at 0–3 spaces of indent. The `>` and one optional space are consumed via `consumeColumns`. Lazy continuation applies: if the tip is a Paragraph, the line may continue without the `>` marker. **Exception:** a setext underline (`===` / `---`) is never a valid lazy continuation line (spec §5.1); the missing `>` causes the block quote to close, and the `---` becomes a thematic break rather than promoting the paragraph. |
| **List** | Delegates to its current Item. |
| **Item** | Line is blank, **or** `virtual_indent ≥ item.padding`. The padding columns are consumed. Blank lines do not terminate item continuation — they may appear inside an item (triggering loose-list rendering via `last_line_blank`). |
| **Indented code block** | Line is blank, **or** `virtual_indent ≥ 4` relative to the block's base indent. Trailing blank lines are removed at finalization. |
| **Fenced code block** | Always continues **unless** the line is a valid closing fence: same fence character, length ≥ opening fence length, indent ≤ 3, nothing but optional trailing spaces after the fence run. |
| **ATX heading** | Never continues — single-line block. Always fails the predicate; block is closed at the end of the line that opened it. |
| **Setext heading** | A Paragraph is promoted to a Setext heading when its next continuation line is a setext underline (`===` or `---`, indent 0–3, optional trailing spaces). The underline line closes and transforms the paragraph rather than continuing it. Leading/trailing blank lines are stripped from the accumulated `string_content` before the heading is finalised. The setext underline itself is never added to `string_content`. |
| **HTML block** | Types 1–5: continue until the type-specific end condition. Type 6: continue until a blank line. Type 7: single block, never continues. |
| **Paragraph** | Line is not blank. Blank lines close the paragraph. Non-blank lines that fail to open a new block are absorbed as lazy continuation even when ancestor container conditions fail. |

Tab-based indent comparisons use virtual column arithmetic described in [§7.1](07_tab_algorithm.md#71-virtual-column-arithmetic).

---

## 3.2 Open block rules — step 2 triggers

Checked against `content[next_non_space..]` and `virtual_indent` after step 1
has consumed container prefixes. First match wins.

1. **BlockQuote** — `>` at 0–3 spaces indent.
2. **ATX heading** — 1–6 `#` chars followed by a space, tab, or end-of-line, indent 0–3. The `string_content` is the heading text after stripping the leading `#` run and its trailing space/tab. A trailing sequence of `#` characters (optionally preceded by a space or tab) is also stripped from the content (spec §4.2).
3. **Fenced code block** — 3+ `` ` `` or `~`, indent 0–3; optional info string follows.
4. **HTML block** — matches one of the 7 start patterns (checked type 1 → 7). **Type 7 cannot interrupt a paragraph** (spec §4.6): the type-7 pattern is not attempted when the current tip is an open Paragraph.
5. **ThematicBreak** — 3+ `*`, `-`, or `_` with optional spaces, indent 0–3.
6. **List item / List** — bullet (`-`, `*`, `+`) or ordered marker (`N.` or `N)` for N ≥ 0, up to 9 digits) followed by at least one space or tab. Opens a new List if none is open or if the marker type/delimiter differs, then always opens an Item. **Paragraph interruption constraint:** when an open Paragraph is the current tip, only a bullet marker or an ordered marker whose start number is exactly `1` may open a new list (spec §5.3). An ordered marker with start number ≠ 1 in this context is not recognised as a list opener.
7. **Indented code block** — `virtual_indent ≥ 4`, tip is not a Paragraph, not
   inside a list-item blank-line continuation.
8. **Paragraph** — fallback for any non-blank line matching nothing above.
   Appended to an open Paragraph (lazy continuation) or opens a new one.

These openers are attempted in [`SpineHandler::step2NewBlocks()`](05_spine_handler.md#53-per-line-loop--three-steps) via `tryOpenNewBlock()`.

---

## 3.3 Close block rules

Blocks are never closed in step 1. Closing is deferred to step 2 so that the
lazy continuation decision can be made with full information.

| Trigger | What closes |
|---|---|
| New block found in step 2 | All unmatched blocks recorded in step 1, from `first_unmatched` upward (tip-first). |
| No new block, not lazy | Same: all unmatched blocks. Text falls into the deepest matched block. |
| No new block, lazy | Unmatched ancestor blocks are intentionally **kept open**. Paragraph absorbs the line. |
| Blank line in Paragraph | The Paragraph is closed. |
| Block interruption | ATX heading, ThematicBreak, fenced code, HTML block types 1–6 can interrupt a Paragraph mid-content. HTML block type 7 **cannot** interrupt a paragraph. |
| Setext underline | Closes and transforms the open Paragraph into a Heading before any new block opens. |
| End of input | `finalize()` closes all open blocks tip-first. |

Closing is implemented by [`SpineHandler::closeBlock()` and `closeUnmatched()`](05_spine_handler.md#54-tree-mutation-primitives). The `finalize()` call is described in [§5.6](05_spine_handler.md#56-finalize-and-the-phase-boundary).

---

← [2. Data types and node structures](02_data_types.md) | [Index](index.md) | Next: [4. PreScanner](04_prescanner.md) →
