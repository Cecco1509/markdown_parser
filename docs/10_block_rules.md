# 10. block_rules — continuation, open, and close predicates

← [9. Open design decisions](09_open_decisions.md) | [Index](index.md)

---

`block_rules` is a pure, stateless module that encodes the three rule sets from
[§3](03_continuation_rules.md). It has no side effects and no access to
`SpineHandler`'s internal state. All functions operate on `const BlockNode&`
references and `const ScannedLine&` values; `BlockNode` is only mutated by
`onClose`, which is called by `SpineHandler::closeBlock()` after ownership has
been transferred out of the spine.

```
include/markdown_parser/block_rules.hpp
src/block_rules.cpp
```

---

## 10.1 ContinuationResult

```cpp
struct ContinuationResult {
    bool        matched;
    std::size_t cols_to_consume = 0;
    bool        swallow_line    = false;
};
```

Returned by `continuationMatches`. When `matched` is true, `cols_to_consume`
tells `SpineHandler::step1WalkSpine` how many virtual columns to advance via
`consumeColumns` (e.g. the `> ` prefix for BlockQuote, or `padding` columns for
Item). When `matched` is false and `swallow_line` is true, the line is the
fenced-code closing fence: the block fails continuation but the line must not be
appended to any block's `string_content` — `SpineMatchResult::swallow_line` is
set and `step3AppendText` returns early.

---

## 10.2 continuationMatches

```cpp
ContinuationResult continuationMatches(const BlockNode& node, const ScannedLine& line);
```

Dispatches on `node.type`. Summary:

| Block type | Predicate | `cols_to_consume` |
|---|---|---|
| Document | always true | 0 |
| BlockQuote | `virtual_indent ≤ 3` and content starts with `>` | indent cols + 1 (`>`) + 1 optional space |
| List | always true (delegates to active Item) | 0 |
| Item | blank **or** `virtual_indent ≥ padding` | `padding` (0 for blank) |
| CodeBlock (fenced) | not a valid closing fence | 0 |
| CodeBlock (indented) | blank **or** `virtual_indent ≥ 4` | 4 (0 for blank) |
| Heading (ATX) | never | — |
| HtmlBlock type 6 | not blank | 0 |
| HtmlBlock type 7 | never | — |
| HtmlBlock types 1–5 | always (end checked post-append) | 0 |
| Paragraph | not blank | 0 |
| ThematicBreak | never | — |

The `swallow_line = true` path is only reached for a fenced-code closing fence
(fails the block, but the line must be consumed silently).

---

## 10.3 htmlBlockEndMet and isSetextUnderline

```cpp
bool htmlBlockEndMet(const BlockNode& node, std::string_view line_content);
```

Called by `SpineHandler::checkHtmlBlockEnd` **after** `step3AppendText` appends
the line. Returns true when `line_content` contains the type-specific end
pattern. Types 1–5 only (types 6/7 are handled by `continuationMatches`).

| Type | End condition |
|---|---|
| 1 | line contains `</script>`, `</pre>`, `</style>`, or `</textarea>` (case-insensitive) |
| 2 | line contains `-->` |
| 3 | line contains `?>` |
| 4 | line contains `>` |
| 5 | line contains `]]>` |

```cpp
bool isSetextUnderline(const ScannedLine& line);
```

True when the line is a valid setext underline: `virtual_indent ≤ 3`, first
non-space char is `=` or `-`, all remaining chars are the same marker char then
optional spaces. Used by two `SpineHandler` sites:

- `incorporatesLazyContinuation` — suppresses lazy continuation when the line is
  a setext underline (spec §5.1).
- `tryPromoteSetextHeading` — triggers the Paragraph → Heading promotion.

---

## 10.4 OpenResult

```cpp
struct OpenResult {
    NodeType              type;
    BlockData             data;
    std::optional<ListData> list_data;       // set when type == Item
    std::string           extracted_content; // pre-parsed text (ATX heading)
    bool                  swallow_line  = false;
    std::size_t           cols_consumed = 0;
};
```

`list_data` carries the `ListData` for the enclosing List when the opener is a
list item. `SpineHandler::tryOpenNewBlock` uses it to decide whether to open a
new `List` container or reuse the current one (same `list_type`, `delimiter`,
and `bullet_char`).

`extracted_content` is the pre-parsed heading text for ATX headings (after
stripping the `#` run, leading/trailing whitespace, and optional trailing `#`
sequence). `SpineHandler::tryOpenNewBlock` assigns it to `new_node->string_content`
immediately after `openBlock`.

`swallow_line = true` for openers that consume the entire line as a marker with
no content to append (ATX heading, ThematicBreak, fenced-code opener).
`SpineHandler` sets `swallow_current_line_ = true` so `step3AppendText` returns
early.

`cols_consumed` is passed to `consumeColumns` after the block is pushed onto
the spine (e.g. `padding` columns for a list item, virtual-indent columns for a
BlockQuote prefix).

---

## 10.5 tryOpen

```cpp
std::optional<OpenResult> tryOpen(const ScannedLine& line,
                                  bool tip_is_paragraph,
                                  bool inside_list_blank = false);
```

Tries openers in priority order (§3.2). Returns `std::nullopt` for blank lines
or when no opener fires. Does **not** return a Paragraph fallback — that is
handled implicitly in `SpineHandler::step3AppendText` (see [§5.3](05_spine_handler.md#53-per-line-loop--three-steps)).

Priority order and paragraph-interruption guards:

| Priority | Opener | Paragraph-interruption guard |
|---|---|---|
| 1 | BlockQuote | none |
| 2 | ATX heading | none |
| 3 | Fenced code block | none |
| 4 | HTML block types 1–7 | type 7 cannot interrupt a paragraph |
| 5 | ThematicBreak | none |
| 6 | List item / List | ordered marker start ≠ 1 blocked; empty item blocked |
| 7 | Indented code block | blocked when `tip_is_paragraph` or `inside_list_blank` |

`tip_is_paragraph` is checked against `tip()->type == NodeType::Paragraph` in
`SpineHandler::tryOpenNewBlock` before each call to `tryOpen`.

`inside_list_blank` is true when any `Item` ancestor has `last_line_blank =
true`, which suppresses the indented code block opener (spec §3.2 point 7).

---

## 10.6 tryOpen — container loop

`tryOpen` returns one opener per call. For container openers (BlockQuote, List,
Item), `SpineHandler::tryOpenNewBlock` calls `tryOpen` in a loop, rescanning the
remaining line after each successful container open:

```
tryOpen(cur, ...) → BlockQuote
  consumeColumns(padding)
  cur = scanner_.scanWithOffset(line.content.substr(current_byte_), current_col_)
tryOpen(cur, ...) → Item
  open List if needed, open Item
  consumeColumns(padding)
  cur = scanner_.scanWithOffset(...)
tryOpen(cur, ...) → ATX heading   ← leaf: break loop, set swallow_current_line_
```

`PreScanner::scanWithOffset` recomputes `indent`, `virtual_indent`, and
`next_non_space` from `current_col_` as the new base column, so tab expansion
continues correctly across nested prefixes. See [§7](07_tab_algorithm.md).

---

## 10.7 onClose

```cpp
void onClose(BlockNode& node);
```

Called by `SpineHandler::closeBlock()` before `end_line` is recorded. Handles
type-specific finalization:

| Block type | Action |
|---|---|
| Indented CodeBlock | strip trailing blank lines from `string_content` |
| Setext Heading | strip leading/trailing `\n`/`\r` from `string_content` |
| All others | no-op |

Note: fenced code blocks are **not** stripped — their closing fence already
delimits content precisely. ATX headings never call `appendText` (`swallow_line
= true`), so their `extracted_content` is assigned directly on open.

---

← [9. Open design decisions](09_open_decisions.md) | [Index](index.md)
