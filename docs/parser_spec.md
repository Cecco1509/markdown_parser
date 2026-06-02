# CommonMark parser — design specification

> Language: C++20  
> Reference: CommonMark Spec v0.31.2  
> External libs: `nlohmann/json` (spec test loading), `googletest` (unit tests)

---

## Table of contents

1. [Project structure](#1-project-structure)
2. [Data types and node structures](#2-data-types-and-node-structures)
   - 2.1 [Enumerations](#21-enumerations)
   - 2.2 [BlockData union](#22-blockdata-union)
   - 2.3 [InlineData union](#23-inlinedata-union)
   - 2.4 [BlockNode](#24-blocknode)
   - 2.5 [InlineNode, Delimiter, BracketEntry](#25-inlinenode-delimiter-bracketentry)
3. [Continuation, open, and close rules](#3-continuation-open-and-close-rules)
   - 3.1 [Continuation rules per block type](#31-continuation-rules-per-block-type)
   - 3.2 [Open block rules — step 2 triggers](#32-open-block-rules--step-2-triggers)
   - 3.3 [Close block rules](#33-close-block-rules)
4. [PreScanner](#4-prescanner)
   - 4.1 [ScannedLine](#41-scannedline)
   - 4.2 [PreScanner methods](#42-prescanner-methods)
5. [SpineHandler — phase 1](#5-spinehandler--phase-1)
   - 5.1 [State](#51-state)
   - 5.2 [SpineMatchResult](#52-spinematchresult)
   - 5.3 [Per-line loop — three steps](#53-per-line-loop--three-steps)
   - 5.4 [Tree mutation primitives](#54-tree-mutation-primitives)
   - 5.5 [Tab accounting](#55-tab-accounting)
   - 5.6 [finalize and the phase boundary](#56-finalize-and-the-phase-boundary)
6. [InlineParser — phase 2](#6-inlineparser--phase-2)
   - 6.1 [State](#61-state)
   - 6.2 [InlineParser methods](#62-inlineparser-methods)
7. [Tab algorithm](#7-tab-algorithm)
   - 7.1 [Virtual column arithmetic](#71-virtual-column-arithmetic)
   - 7.2 [Partial tab splitting](#72-partial-tab-splitting)
   - 7.3 [Worked example — spec §2.2 example 5](#73-worked-example--spec-22-example-5)
8. [Data flow through phases](#8-data-flow-through-phases)
   - 8.1 [End-to-end pipeline](#81-end-to-end-pipeline)
   - 8.2 [Lifecycle summary](#82-lifecycle-summary)
   - 8.3 [Ownership model](#83-ownership-model)

---

## 1. Project structure

```
markdown_parser/
├── CMakeLists.txt                  # top-level build config
├── README.md
├── .gitignore
│
├── include/
│   └── markdown_parser/
│       ├── Types.hpp               # NodeType, InlineType, BlockData, InlineData
│       ├── ScannedLine.hpp         # ScannedLine struct
│       ├── BlockNode.hpp           # BlockNode struct
│       ├── InlineNode.hpp          # InlineNode, Delimiter, BracketEntry
│       ├── PreScanner.hpp          # PreScanner class
│       ├── SpineHandler.hpp        # SpineHandler class
│       └── InlineParser.hpp        # InlineParser class
│
├── src/
│   ├── main.cpp
│   ├── PreScanner.cpp
│   ├── SpineHandler.cpp
│   └── InlineParser.cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_prescanner.cpp
│   ├── test_spine.cpp
│   ├── test_inline.cpp
│   └── test_spec.cpp               # CommonMark spec.json conformance suite
│
├── third_party/
│   ├── nlohmann/                   # JSON — spec test loading
│   └── googletest/                 # unit test framework
│
├── build/                          # git-ignored generated artifacts
└── docs/
```

### CMake targets

```cmake
cmake_minimum_required(VERSION 3.20)
project(markdown_parser CXX)
set(CMAKE_CXX_STANDARD 20)

add_library(md_parser
    src/PreScanner.cpp
    src/SpineHandler.cpp
    src/InlineParser.cpp
)
target_include_directories(md_parser PUBLIC include)
target_include_directories(md_parser PRIVATE third_party)

add_executable(md_parser_bin src/main.cpp)
target_link_libraries(md_parser_bin PRIVATE md_parser)

add_subdirectory(third_party/googletest)
add_subdirectory(tests)
```

### Header dependency order

`Types.hpp` has no internal deps. `ScannedLine.hpp` includes only `Types.hpp`.
`BlockNode.hpp` and `InlineNode.hpp` include `Types.hpp`. Component headers
(`PreScanner`, `SpineHandler`, `InlineParser`) include the node headers they
operate on. No circular dependencies.

---

## 2. Data types and node structures

### 2.1 Enumerations

```cpp
// include/markdown_parser/Types.hpp

enum class NodeType : uint8_t {
    Document,       // root container — always the spine[0]
    BlockQuote,     // container: > prefix
    List,           // container: groups Items of the same marker type
    Item,           // container: single list item
    CodeBlock,      // leaf: fenced (``` or ~~~) or indented (4-space)
    HtmlBlock,      // leaf: raw HTML, types 1–7
    Paragraph,      // leaf: fallback non-blank text
    Heading,        // leaf: ATX (# …) or Setext (underline promoted from Paragraph)
    ThematicBreak   // leaf: ---, ***, ___ — no string_content, no inline children
};

enum class InlineType : uint8_t {
    Text,           // plain text run
    SoftBreak,      // newline inside a paragraph — renders as space or \n
    LineBreak,      // hard line break — two trailing spaces or backslash + newline
    Code,           // backtick span
    HtmlInline,     // raw inline HTML tag
    Emph,           // * or _ emphasis — has inline children
    Strong,         // ** or __ strong — has inline children
    Link,           // [text](dest "title") or [text][label] — has inline children
    Image           // ![alt](dest "title") or ![alt][label] — has inline children
};

enum class ListType  : uint8_t { Bullet, Ordered };
enum class ListDelim : uint8_t { Period, Paren };   // ordered: '.' or ')'
```

### 2.2 BlockData union

Per-type payload stored inside `BlockNode::data` as a `std::variant`.
`std::monostate` is used for types that carry no extra fields
(Document, BlockQuote, Paragraph, ThematicBreak).

```cpp
struct HeadingData {
    int  level;     // 1–6
    bool setext;    // true if promoted from a Paragraph by a setext underline
};

struct CodeBlockData {
    bool        fenced;         // false = indented code block
    char        fence_char;     // '`' or '~'; unused for indented blocks
    int         fence_len;      // number of fence chars in the opening line (≥ 3)
    int         fence_indent;   // leading spaces on the opening fence line (0–3)
    std::string info_string;    // text after the opening fence, trimmed; e.g. "cpp"
};

struct ListData {
    ListType  list_type;        // Bullet or Ordered
    char      bullet_char;      // '-', '*', or '+' for bullet lists
    int       start;            // start number for ordered lists
    ListDelim delimiter;        // Period or Paren for ordered lists
    bool      tight;            // true if no blank lines between items
    int       padding;          // total columns consumed by marker + spacing
};

struct ItemData {
    ListData*  list_data;       // non-owning pointer to the parent List's data
    int        marker_offset;   // column where the marker begins
    int        padding;         // columns to consume for continuation (marker width + spacing)
};

struct HtmlBlockData {
    int html_type;              // 1–7 per spec §4.6
};

// Link reference definitions are not block nodes; they are extracted from
// Paragraph string_content during finalization and stored in SpineHandler::ref_map_.
struct LinkDef {
    std::string destination;
    std::optional<std::string> title;
};

using BlockData = std::variant<
    std::monostate,     // Document, BlockQuote, Paragraph, ThematicBreak
    HeadingData,
    CodeBlockData,
    ListData,
    ItemData,
    HtmlBlockData
>;
```

### 2.3 InlineData union

```cpp
struct LinkData {
    std::string                  destination;
    std::optional<std::string>   title;
    std::optional<std::string>   label;  // set for reference-style links/images
};

using InlineData = std::variant<std::monostate, LinkData>;
```

### 2.4 BlockNode

```cpp
// include/markdown_parser/BlockNode.hpp

struct BlockNode {
    // --- type and variant payload ---
    NodeType   type;
    BlockData  data;            // std::monostate for container/empty nodes

    // --- tree links (doubly-linked child list under each parent) ---
    BlockNode* parent      = nullptr;   // null only for Document
    BlockNode* first_child = nullptr;
    BlockNode* last_child  = nullptr;   // maintained for O(1) append
    BlockNode* next        = nullptr;   // next sibling
    BlockNode* prev        = nullptr;   // previous sibling

    // --- source location ---
    int start_line = 0;     // 1-based line where this block opened
    int end_line   = 0;     // 1-based line where this block was finalized
    int start_col  = 0;     // 0-based column of the opening marker

    // --- state ---
    bool is_open         = true;    // true while on the spine; false after closeBlock()
    bool last_line_blank = false;   // true if the last line processed for this block
                                    // was blank; used to compute list tightness

    // --- content ---
    // Accumulated raw text for leaf blocks (Paragraph, Heading, CodeBlock, HtmlBlock).
    // Empty for container blocks. Tab bytes are stored as-is; partial-tab spaces
    // are prepended by appendText() when a tab is split across a container boundary.
    std::string  string_content;

    // Head of the inline node linked list, populated by InlineParser::parse()
    // during phase 2. Null for container blocks and ThematicBreak.
    InlineNode*  inline_children = nullptr;
};
```

### 2.5 InlineNode, Delimiter, BracketEntry

```cpp
// include/markdown_parser/InlineNode.hpp

struct InlineNode {
    InlineType   type;
    std::string  literal;       // Text content for Text, Code, HtmlInline nodes
    InlineNode*  prev     = nullptr;
    InlineNode*  next     = nullptr;
    InlineNode*  children = nullptr;    // head of child list for Emph, Strong, Link, Image
    InlineData   data;          // LinkData for Link/Image; monostate otherwise
};

// Internal to InlineParser. One entry per delimiter run (* or _) on the
// delimiter stack, used by the process_emphasis algorithm (spec §6.4).
struct Delimiter {
    char        ch;             // '*' or '_'
    int         num;            // number of delimiter characters remaining
    bool        can_open;       // this run is left-flanking
    bool        can_close;      // this run is right-flanking
    InlineNode* node = nullptr; // the Text node holding the delimiter characters
};

// Internal to InlineParser. One entry per '[' or '![' opener on the bracket stack.
struct BracketEntry {
    bool        is_image;
    InlineNode* node      = nullptr;
    std::size_t delim_top = 0;  // delimiter stack depth at the moment '[' was seen;
                                // used to deactivate delimiters inside an invalid link
};
```

---

## 3. Continuation, open, and close rules

All predicates operate on raw bytes from `ScannedLine::content` and the
`virtual_indent` field. There is no pre-assigned `LineClass`; each predicate
makes its own determination. The same raw line may satisfy different predicates
depending on which block type's predicate is evaluated and in what order.

### 3.1 Continuation rules per block type

These predicates are tested in **step 1**. A block that fails its predicate is
recorded as unmatched — it is not closed yet (see §3.3).

| Block type | Continuation condition |
|---|---|
| **Document** | Always matches. |
| **BlockQuote** | Line has a `>` marker at 0–3 spaces of indent. The `>` and one optional space are consumed via `consumeColumns`. Lazy continuation applies: if the tip is a Paragraph, the line may continue without the `>` marker. |
| **List** | Delegates to its current Item. |
| **Item** | Line is blank, **or** `virtual_indent ≥ item.padding`. The padding columns are consumed. A blank line does not continue an item if two consecutive blanks have been seen. |
| **Indented code block** | Line is blank, **or** `virtual_indent ≥ 4` relative to the block's base indent. Trailing blank lines are removed at finalization. |
| **Fenced code block** | Always continues **unless** the line is a valid closing fence: same fence character, length ≥ opening fence length, indent ≤ 3, nothing but optional trailing spaces after the fence run. |
| **ATX heading** | Never continues — single-line block. Always fails the predicate; block is closed at the end of the line that opened it. |
| **Setext heading** | A Paragraph is promoted to a Setext heading when its next continuation line is a setext underline (`===` or `---`). The underline line closes and transforms the paragraph rather than continuing it. |
| **HTML block** | Types 1–5: continue until the type-specific end condition. Type 6: continue until a blank line. Type 7: single block, never continues. |
| **Paragraph** | Line is not blank. Blank lines close the paragraph. Non-blank lines that fail to open a new block are absorbed as lazy continuation even when ancestor container conditions fail. |

### 3.2 Open block rules — step 2 triggers

Checked against `content[next_non_space..]` and `virtual_indent` after step 1
has consumed container prefixes. First match wins.

1. **BlockQuote** — `>` at 0–3 spaces indent.
2. **ATX heading** — 1–6 `#` chars followed by space or end-of-line, indent 0–3.
3. **Fenced code block** — 3+ `` ` `` or `~`, indent 0–3; optional info string follows.
4. **HTML block** — matches one of the 7 start patterns (checked type 1 → 7).
5. **ThematicBreak** — 3+ `*`, `-`, or `_` with optional spaces, indent 0–3.
6. **List item / List** — bullet (`-`, `*`, `+`) or ordered marker (`1.`, `1)`, …)
   followed by at least one space. Opens a new List if none is open or if marker
   type differs, then always opens an Item.
7. **Indented code block** — `virtual_indent ≥ 4`, tip is not a Paragraph, not
   inside a list-item blank-line continuation.
8. **Paragraph** — fallback for any non-blank line matching nothing above.
   Appended to an open Paragraph (lazy continuation) or opens a new one.

### 3.3 Close block rules

Blocks are never closed in step 1. Closing is deferred to step 2 so that the
lazy continuation decision can be made with full information.

| Trigger | What closes |
|---|---|
| New block found in step 2 | All unmatched blocks recorded in step 1, from `first_unmatched` upward (tip-first). |
| No new block, not lazy | Same: all unmatched blocks. Text falls into the deepest matched block. |
| No new block, lazy | Unmatched ancestor blocks are intentionally **kept open**. Paragraph absorbs the line. |
| Blank line in Paragraph | The Paragraph is closed. |
| Block interruption | ATX heading, ThematicBreak, fenced code, HTML block types 1–6 can interrupt a Paragraph mid-content. |
| Setext underline | Closes and transforms the open Paragraph into a Heading before any new block opens. |
| End of input | `finalize()` closes all open blocks tip-first. |

---

## 4. PreScanner

The PreScanner's only job is to strip the line ending, compute indent metrics,
and detect blank lines. It does **not** expand tabs and does **not** classify the
line. Classification is the responsibility of each block's own continuation
predicate and the opener checks in step 2.

### 4.1 ScannedLine

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

### 4.2 PreScanner methods

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

---

## 5. SpineHandler — phase 1

The SpineHandler owns the open block stack (the "spine") and implements the
three-step `processLine` algorithm. It is the sole owner of the AST root.

### 5.1 State

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
    // spine_.back() is the current tip. All entries are non-owning raw
    // pointers into the tree owned by document_.
    std::vector<BlockNode*> spine_;

    // Owns the Document root and transitively every BlockNode in the tree
    // via the parent → child linked-list chains.
    std::unique_ptr<BlockNode> document_;

    // Link reference definitions extracted from Paragraph string_content
    // during finalization. Keyed by normalised label (case-folded,
    // collapsed whitespace). Fully populated before phase 2 begins.
    std::unordered_map<std::string, LinkDef> ref_map_;

    int line_number_      = 0;  // current 1-based line counter
    int last_line_length_ = 0;  // column length of the previous line;
                                // used for setext heading detection

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

### 5.2 SpineMatchResult

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

### 5.3 Per-line loop — three steps

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
        if (continuationMatches(spine_[i], line)) {
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

### 5.4 Tree mutation primitives

```cpp
// ── openBlock ─────────────────────────────────────────────────────────────
//
// Tree attachment happens HERE at open time.
// The node is permanently wired into the tree as a child of tip() before it
// is pushed onto the spine. closeBlock() does not move nodes in the tree.

BlockNode* SpineHandler::openBlock(NodeType type, BlockData data)
{
    // Allocate the new node. The tree owns all nodes via the parent→child
    // linked-list chain rooted at document_. The spine holds non-owning
    // raw pointers.
    BlockNode* node    = new BlockNode();
    node->type         = type;
    node->data         = std::move(data);
    node->start_line   = line_number_;
    node->is_open      = true;

    BlockNode* parent  = tip();

    // Wire the new node into the parent's child list.
    // This attachment is permanent — closeBlock() will not undo it.
    node->parent = parent;
    if (parent->last_child) {
        parent->last_child->next = node;
        node->prev               = parent->last_child;
    } else {
        parent->first_child = node;
    }
    parent->last_child = node;

    // Push onto the spine. The spine entry is non-owning.
    spine_.push_back(node);
    return node;
}

// ── closeBlock ────────────────────────────────────────────────────────────
//
// Finalise a block. Does NOT move the node in the tree — the node stays as a
// child of its parent exactly where it was placed by openBlock().
// Does NOT trigger inline parsing — that is phase 2, initiated by finalize().

void SpineHandler::closeBlock(BlockNode* node)
{
    // Record the closing line.
    node->end_line = line_number_;

    // Mark the block as no longer open.
    node->is_open = false;

    // Pop from the spine. The node remains in the tree; only the spine
    // reference is removed. Tree integrity is unchanged.
    spine_.erase(std::find(spine_.begin(), spine_.end(), node));

    // Inline parsing (InlineParser::parse) is intentionally NOT called here.
    // It runs in phase 2 via finalize() → parseInlineContent(), after all
    // lines have been processed and ref_map_ is fully populated.
}

// ── closeUnmatched ────────────────────────────────────────────────────────
//
// Close spine[from_index..] tip-first. Child blocks must be closed before
// their parents to ensure end_line is recorded correctly on each.

void SpineHandler::closeUnmatched(std::size_t from_index)
{
    while (spine_.size() > from_index) {
        closeBlock(spine_.back());
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
    return spine_.back();
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

### 5.5 Tab accounting

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

### 5.6 finalize and the phase boundary

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
    // Close all blocks except Document (index 0), tip-first.
    while (spine_.size() > 1) {
        closeBlock(spine_.back());
    }
    // Close Document itself.
    closeBlock(document_.get());

    // Phase 2: walk the tree and inline-parse every leaf block.
    parseInlineContent(document_.get());
}

// Recursive helper for finalize(). Visits the tree depth-first.
// Container blocks are recursed into; leaf blocks are inline-parsed.
// ThematicBreak, and any container type, are not passed to InlineParser
// because they carry no string_content.

void SpineHandler::parseInlineContent(BlockNode* node)
{
    const bool is_leaf =
        node->type == NodeType::Paragraph ||
        node->type == NodeType::Heading   ||
        node->type == NodeType::CodeBlock ||
        node->type == NodeType::HtmlBlock;

    if (is_leaf) {
        // string_content is complete and ref_map_ is fully populated.
        inline_parser_.parse(node, ref_map_);
    } else {
        // Recurse into container children.
        for (BlockNode* child = node->first_child;
             child != nullptr;
             child = child->next)
        {
            parseInlineContent(child);
        }
    }
}
```

---

## 6. InlineParser — phase 2

The InlineParser runs on a single leaf block's `string_content` at a time,
building a linked list of `InlineNode` children. It has no interaction with the
spine or the block tree structure. All state is reset between `parse()` calls.

### 6.1 State

```cpp
// include/markdown_parser/InlineParser.hpp

class InlineParser {
public:
    // Entry point. Parses block->string_content and attaches the resulting
    // inline node list to block->inline_children. Requires a fully-populated
    // ref_map for reference-style link resolution.
    // Must not be called before SpineHandler::finalize() returns.
    void parse(BlockNode*                                         block,
               const std::unordered_map<std::string, LinkDef>&   ref_map);

private:
    // ── per-parse state (reset at the top of each parse() call) ──────────

    std::string_view         input_;        // block->string_content view
    std::size_t              pos_ = 0;      // current byte offset into input_

    // Delimiter stack for * and _ runs. Entries accumulate as the scanner
    // encounters delimiter characters; the processEmphasis() post-pass
    // matches opener/closer pairs and emits Emph/Strong nodes.
    std::vector<Delimiter>   delimiters_;

    // Bracket stack for [ and ![ openers. LIFO; entries are popped and
    // resolved (or discarded) when a matching ] is encountered.
    std::vector<BracketEntry> brackets_;

    // Non-owning reference to the document's link reference map.
    const std::unordered_map<std::string, LinkDef>* ref_map_ = nullptr;
};
```

### 6.2 InlineParser methods

```cpp
// ── parse (entry point) ───────────────────────────────────────────────────

void InlineParser::parse(
    BlockNode*                                        block,
    const std::unordered_map<std::string, LinkDef>&   ref_map)
{
    // Reset all per-parse state.
    input_      = block->string_content;
    pos_        = 0;
    ref_map_    = &ref_map;
    delimiters_.clear();
    brackets_.clear();

    // Scan the string character by character, building inline nodes.
    // Each parseInline() call advances pos_ by at least one byte.
    InlineNode* head = nullptr;
    InlineNode* tail = nullptr;
    while (pos_ < input_.size()) {
        InlineNode* node = parseInline();
        if (!node) continue;
        if (!head) head = node;
        if (tail)  tail->next = node;
        node->prev = tail;
        tail = node;
    }

    // Process emphasis: match opener/closer pairs on the delimiter stack.
    // Emits Emph and Strong nodes wrapping their children.
    processEmphasis(std::nullopt);

    block->inline_children = head;
}

// ── dispatch ──────────────────────────────────────────────────────────────

// Dispatch on the current character. Returns the constructed InlineNode, or
// nullptr if the character is absorbed without producing a node (e.g. a
// delimiter that was pushed onto the delimiter stack).

InlineNode* InlineParser::parseInline();

// ── leaf scanners ─────────────────────────────────────────────────────────

// Scan a backtick string: find matching closing backtick run of the same
// length. Normalise interior whitespace (collapse runs to single space,
// strip leading/trailing). Returns a Code node, or nullptr if no closing
// run is found.
InlineNode* InlineParser::parseBacktickString();

// Match <URI> or <email> autolink forms. Returns a Link node on success,
// nullptr if the < does not start a valid autolink.
InlineNode* InlineParser::parseAutolink();

// Match open tag, closing tag, HTML comment, processing instruction,
// declaration, or CDATA section. Returns an HtmlInline node, or nullptr.
InlineNode* InlineParser::parseHtmlInline();

// ── link and image ────────────────────────────────────────────────────────

// Push a BracketEntry onto the bracket stack when '[' or '![' is seen.
// Records whether this is an image opener and the current delimiter stack depth
// (so delimiters inside an invalid link can be deactivated later).
void InlineParser::handleBracketOpener(bool is_image);

// Called when ']' is seen. Pop the bracket stack and attempt to resolve:
//   1. Inline link:     ] followed by ( destination "title" )
//   2. Full ref link:   ] followed by [label]
//   3. Collapsed ref:   ] followed by []
//   4. Shortcut ref:    ] with the bracket text used as the label
// On failure (no matching opener or no valid link), emit ']' as a Text node
// and leave the bracket stack intact.
InlineNode* InlineParser::handleBracketCloser();

// Scan a link destination in one of two forms:
//   - Angle-bracket form: < ... > (no spaces, no unescaped </>)
//   - Bare form: balanced parentheses, no ASCII control characters
// Returns the destination string on success, std::nullopt on failure.
std::optional<std::string> InlineParser::scanLinkDestination();

// Scan a link title in one of three delimiter forms:
//   - "title" (double-quoted)
//   - 'title' (single-quoted)
//   - (title) (parenthesised)
// Returns the title string on success, std::nullopt on failure.
std::optional<std::string> InlineParser::scanLinkTitle();

// ── emphasis ──────────────────────────────────────────────────────────────

// Called when a run of * or _ is scanned. Determines left-flanking /
// right-flanking status per spec §6.4 rules, then either pushes a Delimiter
// entry (can_open), attempts to close an existing opener (can_close), or
// emits the run as literal Text if it can neither open nor close.
void InlineParser::handleEmphasis(char delim_char, std::size_t run_len);

// Post-processing pass over the delimiter stack. Matches the innermost
// opener/closer pairs first, wraps their content in Emph or Strong nodes,
// and removes the matched entries. stack_bottom limits the search range;
// std::nullopt means process the entire stack.
// Called once at end of parse() with std::nullopt, and again with a saved
// depth when a valid link/image is resolved (to process emphasis inside it).
void InlineParser::processEmphasis(std::optional<std::size_t> stack_bottom);

// ── utilities ─────────────────────────────────────────────────────────────

// Collapse Unicode whitespace runs to a single space and fold to Unicode
// case-fold for link reference label matching (spec §4.7).
static std::string InlineParser::normaliseLabel(std::string_view label);

// Allocate a new InlineNode of the given type with default-initialised fields.
InlineNode* InlineParser::makeNode(InlineType type);
```

---

## 7. Tab algorithm

Tabs in CommonMark are never expanded to space characters in the source text.
However, for the purpose of block-structure decisions (indentation thresholds,
continuation indents, list padding), a tab behaves as if it advances the column
to the next multiple of 4. This virtual expansion governs structural logic only;
the raw bytes stored in `string_content` may contain literal tab characters,
except where a tab is split across a container boundary (see §7.2).

### 7.1 Virtual column arithmetic

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

### 7.2 Partial tab splitting

When a container continuation predicate consumes N virtual columns and a tab
straddles the N-column boundary, `consumeColumns()` handles the split:

- The columns consumed up to the boundary satisfy the predicate.
- `partial_tab_remaining_` is set to the number of virtual columns remaining
  in that tab (i.e. `tab_width − columns_used_from_it`).
- The byte offset is **not** advanced past the tab byte.

When `appendText()` is subsequently called to write content to `string_content`:

- If `partial_tab_remaining_ > 0`, emit that many space characters into
  `string_content` (the leftover virtual space becomes real spaces in content).
- Advance `from_byte` by 1 to skip the raw tab byte that was split.
- Clear `partial_tab_remaining_`.
- Append the remaining raw bytes normally.

This is the only place virtual columns are materialised as space characters.

### 7.3 Worked example — spec §2.2 example 5

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

---

## 8. Data flow through phases

### 8.1 End-to-end pipeline

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
  │                               openBlock() wires tree pointers (attachment)
  │                               then pushes onto spine_
  │    lazy continuation?     → keep unmatched open, no close, no open
  │    neither?               → closeUnmatched()
  │
  └─ step 3: step3AppendText()
       setext underline?      → tryPromoteSetextHeading(), return
       link ref def?          → maybeScanLinkRefDefs(), extract into ref_map_
       normal?                → appendText() → tip()->string_content
                                  if partial_tab_remaining_ > 0:
                                    emit spaces, skip tab byte, clear flag

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
           build InlineNode linked list
           resolve backtick spans, autolinks, raw HTML
           handle bracket stack → Link / Image nodes
           handle delimiter stack → Emph / Strong nodes (processEmphasis)
           attach result to block->inline_children
```

### 8.2 Lifecycle summary

```
openBlock()
  ├─ allocate BlockNode
  ├─ wire tree pointers: parent, prev, next, first_child, last_child  ← TREE ATTACH
  └─ push onto spine_  (non-owning raw pointer)

processLine() [per line]
  └─ appendText() accumulates string_content on tip()
       partial_tab_remaining_ → spaces prepended if tab was split

closeBlock()
  ├─ record end_line
  ├─ set is_open = false
  └─ pop from spine_                ← SPINE ONLY; tree position unchanged
     (InlineParser NOT called here)

finalize()
  ├─ closeBlock() × N              drain remaining spine tip-first
  └─ parseInlineContent()          phase 2 — ref_map_ complete
       └─ InlineParser::parse()    per leaf block
```

### 8.3 Ownership model

- `SpineHandler::document_` — `std::unique_ptr<BlockNode>`. Owns the Document
  root and transitively every `BlockNode` in the tree through the
  parent → child linked-list chains.
- `SpineHandler::spine_` — `std::vector<BlockNode*>`. Non-owning raw pointers
  into the tree. Removing a pointer from the spine (via `closeBlock`) does not
  affect tree ownership or node lifetime.
- `BlockNode::inline_children` — raw pointer to the head of the inline node
  list. `InlineNode` lifetime is managed separately (arena or `unique_ptr` chain
  on the `BlockNode`). Design decision to be finalised before writing the
  `BlockNode` destructor.
- `ScannedLine::content` — `std::string_view`. Non-owning slice into the raw
  input buffer passed to `processLine()`. Valid only for the duration of the
  `processLine()` call.
- `InlineParser::ref_map_` — raw const pointer. Non-owning reference to
  `SpineHandler::ref_map_`. Valid for the duration of `finalize()`.

> **Note on `BlockNode` destruction:** because child nodes are heap-allocated
> individually and linked via raw pointers, `BlockNode`'s destructor must walk
> `first_child` and delete the chain, or all nodes must be allocated from a flat
> arena owned by `SpineHandler`. Deep document trees risk stack overflow on
> recursive deletion; an arena or iterative destructor is recommended.
