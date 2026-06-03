#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>

enum class NodeType : uint8_t {
    Document,
    BlockQuote,
    List,
    Item,
    CodeBlock,
    HtmlBlock,
    Paragraph,
    Heading,
    ThematicBreak
};

enum class InlineType : uint8_t {
    Text,
    SoftBreak,
    LineBreak,
    Code,
    HtmlInline,
    Emph,
    Strong,
    Link,
    Image
};

enum class ListType  : uint8_t { Bullet, Ordered };
enum class ListDelim : uint8_t { Period, Paren };

// ── BlockData ─────────────────────────────────────────────────────────────────

struct HeadingData {
    int  level;
    bool setext;
};

struct CodeBlockData {
    bool        fenced;
    char        fence_char;
    int         fence_len;
    int         fence_indent;
    std::string info_string;
};

struct ListData {
    ListType  list_type;
    char      bullet_char;
    int       start;
    ListDelim delimiter;
    bool      tight;
    int       padding;
};

struct ItemData {
    int marker_offset;
    int padding;
};

struct HtmlBlockData {
    int html_type;
};

struct LinkDef {
    std::string destination;
    std::optional<std::string> title;
};

using BlockData = std::variant<
    std::monostate,
    HeadingData,
    CodeBlockData,
    ListData,
    ItemData,
    HtmlBlockData
>;

// ── InlineData ────────────────────────────────────────────────────────────────

struct LinkData {
    std::string                destination;
    std::optional<std::string> title;
    std::optional<std::string> label;
};

using InlineData = std::variant<std::monostate, LinkData>;
