# 2. Data types and node structures

← [1. Project structure](01_project_structure.md) | [Index](index.md) | Next: [3. Continuation, open, and close rules](03_continuation_rules.md) →

---

## 2.1 Enumerations

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

These enumerations are used throughout [`BlockNode`](#24-blocknode) and [`InlineNode`](#25-inlinenode-delimiter-bracketentry).

---

## 2.2 BlockData union

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

`LinkDef` entries are populated into `SpineHandler::ref_map_` — see [§5.1 SpineHandler state](05_spine_handler.md#51-state) and [§9.2 maybeScanLinkRefDefs](09_open_decisions.md#92-maybescanlinkRefdefs--when-to-scan).

---

## 2.3 InlineData union

```cpp
struct LinkData {
    std::string                  destination;
    std::optional<std::string>   title;
    std::optional<std::string>   label;  // set for reference-style links/images
};

using InlineData = std::variant<std::monostate, LinkData>;
```

`LinkData` is used by `Link` and `Image` inline nodes — see [§2.5](#25-inlinenode-delimiter-bracketentry) and [§6.2 InlineParser methods](06_inline_parser.md#62-inlineparser-methods).

---

## 2.4 BlockNode

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

`BlockNode` objects are allocated by [`SpineHandler::openBlock()`](05_spine_handler.md#54-tree-mutation-primitives) and live in the tree rooted at `SpineHandler::document_`. Ownership and destruction are discussed in [§9.1](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode).

---

## 2.5 InlineNode, Delimiter, BracketEntry

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

`Delimiter` and `BracketEntry` are used internally by [`InlineParser`](06_inline_parser.md). The `delim_top` field's semantics are clarified in [§9.4](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction).

---

← [1. Project structure](01_project_structure.md) | [Index](index.md) | Next: [3. Continuation, open, and close rules](03_continuation_rules.md) →
