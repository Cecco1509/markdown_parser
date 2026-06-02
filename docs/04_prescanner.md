# 4. PreScanner

← [3. Continuation, open, and close rules](03_continuation_rules.md) | [Index](index.md) | Next: [5. SpineHandler — phase 1](05_spine_handler.md) →

---

The PreScanner's only job is to strip the line ending, compute indent metrics,
and detect blank lines. It does **not** expand tabs and does **not** classify the
line. Classification is the responsibility of each block's own continuation
predicate (see [§3.1](03_continuation_rules.md#31-continuation-rules-per-block-type)) and the opener checks in step 2 (see [§3.2](03_continuation_rules.md#32-open-block-rules--step-2-triggers)).

Tab virtual-column arithmetic is described in [§7](07_tab_algorithm.md).

---

## 4.1 ScannedLine

```cpp
// include/markdown_parser/ScannedLine.hpp

struct ScannedLine {
    // Non-owning view into the input buffer. Line-ending stripped. Tabs are
    // never replaced — the raw tab bytes are preserved exactly as received.
    // The caller (SpineHandler::processLine) owns the line buffer for the
    // duration of the processLine() call.
    std::string_view content;

    // Raw leading-whitespace byte count: each space or tab counts as 1.
    // Informational only — never used for structural decisions.
    std::size_t indent;

    // Column of the first non-whitespace character, computed with tab-stop
    // arithmetic (stop = 4) starting from base_col. This is the value all
    // structural predicates compare against. See §7 for the arithmetic.
    std::size_t virtual_indent;

    // Byte offset of the first non-whitespace character in content. Used to
    // slice off leading indent when testing for block openers.
    std::size_t next_non_space;

    // True iff content contains only spaces, tabs, or is empty after
    // line-ending removal.
    bool is_blank;
};
```

`ScannedLine` is produced by [`PreScanner::scan()`](#42-prescanner-methods) and consumed by [`SpineHandler::processLine()`](05_spine_handler.md#53-per-line-loop--three-steps). The `content` view is valid only for the duration of the `processLine()` call — see [§8.3 ownership model](08_data_flow.md#83-ownership-model).

The `next_non_space` field interacts with tab splitting; see [§9.3](09_open_decisions.md#93-appendtext-from_byte-when-a-tab-is-split) for a known issue with passing it to `appendText()`.

---

## 4.2 PreScanner methods

```cpp
// include/markdown_parser/PreScanner.hpp

class PreScanner {
public:
    // Strip line ending (\n, \r\n, or \r). Walk leading whitespace to compute
    // indent, virtual_indent (starting from base_col = 0), next_non_space,
    // and is_blank. Returns a ScannedLine whose content is a view into raw.
    ScannedLine scan(std::string_view raw) const;

    // Identical to scan(), but virtual_indent is computed starting from
    // base_col rather than 0. Called by SpineHandler after it has consumed
    // a container prefix (a '>' marker or list item padding) so that inner
    // block predicates see the correct column position relative to the real
    // column, not the start of the raw line.
    ScannedLine scanWithOffset(std::string_view raw,
                               std::size_t      base_col) const;

private:
    // Core implementation shared by both public methods.
    // Returns {raw_indent, virtual_indent, next_non_space_byte_offset}.
    // Pure function — no side effects, no state read.
    static std::tuple<std::size_t, std::size_t, std::size_t>
    computeVirtualIndent(std::string_view content,
                         std::size_t      base_col) noexcept;

    // Remove trailing \n, \r\n, or \r. Returns a slice of the original buffer.
    static std::string_view stripLineEnding(std::string_view raw) noexcept;

    // True iff every byte in content is b' ' or b'\t', or content is empty.
    static bool isBlank(std::string_view content) noexcept;
};
```

`scanWithOffset()` is called by `SpineHandler` after consuming container prefixes via [`consumeColumns()`](05_spine_handler.md#55-tab-accounting), so that inner block predicates see the correct `virtual_indent` relative to the real column position.

---

← [3. Continuation, open, and close rules](03_continuation_rules.md) | [Index](index.md) | Next: [5. SpineHandler — phase 1](05_spine_handler.md) →
