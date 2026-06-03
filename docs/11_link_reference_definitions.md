# 11. Link reference definitions

← [10. block_rules](10_block_rules.md) | [Index](index.md)

---

Link reference definitions (spec §4.7) are **not block nodes**. They are meta-data
extracted from `Paragraph::string_content` during phase 1 finalization and stored in
`SpineHandler::ref_map_`. Phase 2 (`InlineParser`) reads that map when resolving
`[label]`, `[text][label]`, and `![alt][label]` forms.

---

## 11.1 Spec rules summary

### Syntax

```
[label]: destination
[label]: destination "title"
[label]: destination 'title'
[label]: destination (title)
```

| Part | Rules |
|---|---|
| **Indentation** | 0–3 leading spaces allowed; 4+ → indented code block, not a definition |
| **Label** | Non-empty; case-insensitive (Unicode simple case fold + whitespace collapse, see [§6.2 `normaliseLabel`](06_inline_parser.md#62-inlineparser-methods)); may span a line break inside the brackets |
| **Colon** | Required immediately after `]`, no space before it |
| **Destination** | Required. Bare form (no spaces, no unescaped `<`/`>`) or angle-bracket form `<…>` (allows spaces, forbids `<`/`>` unescaped, forbids newlines). Empty destination only valid as `<>` |
| **Title** | Optional. `"…"`, `'…'`, or `(…)`. May span multiple lines; **must not** contain a blank line. Must be separated from destination by at least one space/tab |
| **After title** | Only whitespace allowed on the remainder of that line; anything else → not a definition |
| **First definition wins** | If `[foo]` is defined twice, the first entry in `ref_map_` is kept |

### Where they can appear

| Location | Result |
|---|---|
| Before or after the links that use them | Valid — forward and backward refs both work |
| After a heading or thematic break (no blank line) | Valid |
| Consecutive definitions with no blank lines between them | Valid |
| Inside a blockquote or list item | Valid — but scope is the **whole document**, not the container |
| Inside an indented or fenced code block | Not a definition; treated as literal code content |
| With 4+ spaces of indentation | Treated as indented code block instead |
| After paragraph text (no blank line before) | **Cannot interrupt a paragraph** — kept as paragraph text |

### The "cannot interrupt a paragraph" rule in detail

When a paragraph accumulates lines and is then closed, the sub-scanner
(`scanLinkRefDefs`) strips valid definitions only from the **leading** portion of
`string_content` — before any real paragraph text. The moment a line fails to parse
as a definition, all remaining lines stay as paragraph content.

Example (spec ex. 213):
```markdown
aaa
[foo]: /url
bar
```
Result: one paragraph containing `aaa\n[foo]: /url\nbar` — no definition extracted,
because the definition line comes after paragraph text.

---

## 11.2 Data representation

`LinkDef` is defined in [§2.2](02_data_types.md#22-blockdata-union):

```cpp
struct LinkDef {
    std::string destination;
    std::optional<std::string> title;
};
```

`SpineHandler` owns the map (see [§5.1](05_spine_handler.md#51-state)):

```cpp
std::unordered_map<std::string, LinkDef> ref_map_;
// key = normaliseLabel(raw_label)
```

Labels are normalised with `InlineParser::normaliseLabel` (Unicode simple case fold +
collapse whitespace runs to one space + trim) before insertion and before lookup so
that `[Foo]`, `[FOO]`, and `[foo]` all resolve to the same entry.

---

## 11.3 Implementation — `maybeScanLinkRefDefs` / `tryScanOneLinkRefDef`

Called from `SpineHandler::closeBlock` for every `Paragraph` node (see [§9.2](09_open_decisions.md#92-maybescanlinkRefdefs--when-to-scan)).
`maybeScanLinkRefDefs` drives the outer loop; `tryScanOneLinkRefDef` parses one
definition per call and inserts into `ref_map_`.

```cpp
// src/SpineHandler.cpp

void SpineHandler::maybeScanLinkRefDefs(BlockNode* node) {
    std::string_view content = node->string_content;
    std::size_t pos = 0;
    while (pos < content.size()) {
        if (!tryScanOneLinkRefDef(content, pos)) break;
    }
    node->string_content = std::string(content.substr(pos));
}
```

Key invariants:
- `pos` advances only when a **complete** valid definition is consumed; any
  sub-step failure leaves `pos` unchanged and exits the loop.
- If `string_content` is now blank `closeBlock` does not push the node to its
  parent (all content was definitions, no paragraph appears in output).

### `tryScanOneLinkRefDef` — step by step

```
tryScanOneLinkRefDef(content, pos) → bool:

  p = pos

  ① Indentation: skip 0–3 spaces.
     if content[p] != '[' → return false

  ② Label: advance p until ']'.
     Newlines inside the label are allowed (spec ex. 208 proves a label can
     span multiple lines — the "at most one line ending" rule from §4.8.3
     applies only to inline link labels, not reference definition labels).
     Fail if ']' is never found.
     Fail if normaliseLabel(raw_label) is empty (all-whitespace content).

  ③ Colon: content[p] must be ':' immediately after ']'. Fail otherwise.

  ④ Skip to destination: skip spaces/tabs, then at most one '\n'.
     If a blank line follows (two consecutive '\n') → fail (no destination).
     After the optional newline, skip leading spaces/tabs on the new line.
     If p is at EOI or '\n' → fail (empty destination).

  ⑤ Destination:
     Angle-bracket form '<…>': reject unescaped '<', '>', newlines; apply
     backslash-unescape. Fail if '>' is never found.
     Bare form: stop at space/tab/newline/ASCII-control; track parenthesis
     depth; fail if unbalanced or empty. Apply backslash-unescape.

  pos_after_dest = p   ← REWIND POINT (right after dest chars, before
                          any trailing whitespace or newline — see issue 1)

  ⑥ Try title (optional):
     a. Skip spaces/tabs; if next char is '\n', set crossed_newline=true,
        advance past it, skip leading spaces/tabs on the new line.
     b. If next char is '"', '\'' or '(':
          scan title content until the matching close delimiter.
          Fail if a blank line (two consecutive '\n') appears inside.
          Apply backslash-unescape.
        If title scan succeeds:
          Skip spaces/tabs after the closing delimiter (issue 2: stop at
          '\n', do NOT consume it here).
          If non-whitespace found before '\n'/EOI:
            if crossed_newline → rewind p to pos_after_dest, title = nullopt
                                  (issue 1+4: definition is valid without
                                   the title; the title line stays as para)
            else               → return false (garbage on the same line as
                                  the destination invalidates the definition)
          else → title accepted, p set to the '\n'/EOI position.
        If title scan fails:
          if crossed_newline → rewind p to pos_after_dest (issue 1)
          else               → return false
     c. If no title delimiter found at all → p stays at pos_after_dest,
        title = nullopt.

  ⑦ Trailing whitespace check (issue 2):
     Skip spaces/tabs only — do NOT consume '\n'.
     If a non-whitespace character is found before '\n'/EOI → return false.

  ⑧ Consume the line terminator as a separate step (issue 2):
     if content[p] == '\n' → ++p.

  ⑨ Store definition (first-definition-wins):
     if ref_map_.find(norm_key) == end → ref_map_[norm_key] = {dest, title}

  pos = p
  return true
```

### Failure modes illustrated

| Input (paragraph string_content) | Outcome |
|---|---|
| `[foo]: /url "title"\n` | definition extracted, paragraph removed |
| `[foo]: /url\n"title"\n` | definition extracted (with title on next line) |
| `[foo]: /url\n"title" ok\n` | definition extracted **without title** (title line → para text) |
| `[foo]: /url "title" ok\n` | definition fails, paragraph unchanged |
| `[\nfoo\n]: /url\nbar\n` | definition extracted (multi-newline label), `bar` stays |
| `[foo]:\n\n` | fails — blank line after `:`, no destination |
| `[foo]:\n/url\n` | definition extracted (destination on next line) |

---

## 11.4 Integration points

| Site | Action |
|---|---|
| `SpineHandler::closeBlock(Paragraph)` | Calls `scanLinkRefDefs`; mutates `string_content`; removes node if blank |
| `SpineHandler::ref_map_` | Accumulates all `LinkDef` entries across the whole document |
| `InlineParser::parse()` | Receives `ref_map_` as a const reference; never writes to it |
| `InlineParser::handleBracketCloser()` | Resolves full-ref, collapsed-ref, and shortcut-ref forms via `normaliseLabel` + `ref_map_` lookup |
| `InlineParser::normaliseLabel()` | Shared between insertion (phase 1) and lookup (phase 2) to guarantee consistent keys |

---

← [10. block_rules](10_block_rules.md) | [Index](index.md)
