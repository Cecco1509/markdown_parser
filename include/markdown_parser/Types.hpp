#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace markdown_parser {

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

enum class ListType : uint8_t { Bullet, Ordered };
enum class ListDelim : uint8_t { Period, Paren };

// ── BlockData
// ─────────────────────────────────────────────────────────────────

struct HeadingData {
  int level;
  bool setext;
};

struct CodeBlockData {
  bool fenced;
  char fence_char;
  int fence_len;
  int fence_indent;
  std::string info_string;
};

struct ListData {
  ListType list_type;
  char bullet_char;
  int start;
  ListDelim delimiter;
  bool tight;
  int padding;
};

struct ItemData {
  int marker_offset;
  int padding;
};

enum class HtmlBlockType : int {
  ScriptStylePre  = 1, // <script>, <style>, <pre>
  Comment         = 2, // <!-- ... -->
  ProcessingInstr = 3, // <? ... ?>
  Declaration     = 4, // <!LETTER ... >
  CData           = 5, // <![CDATA[ ... ]]>
  KnownTag        = 6, // block-level tag
  Complete        = 7, // complete open/close tag on one line
};

struct HtmlBlockData {
  HtmlBlockType html_type;
};

struct LinkDef {
  std::string destination;
  std::optional<std::string> title;
};

using BlockData = std::variant<std::monostate, HeadingData, CodeBlockData,
                               ListData, ItemData, HtmlBlockData>;

// ── InlineData
// ────────────────────────────────────────────────────────────────

struct LinkData {
  std::string destination;
  std::optional<std::string> title;
  std::optional<std::string> label;
};

using InlineData = std::variant<std::monostate, LinkData>;

// ── Utilities
// ─────────────────────────────────────────────────────────────────

inline std::string_view nodeTypeToString(NodeType t) {
  switch (t) {
  case NodeType::Document:
    return "Document";
  case NodeType::BlockQuote:
    return "BlockQuote";
  case NodeType::List:
    return "List";
  case NodeType::Item:
    return "Item";
  case NodeType::CodeBlock:
    return "CodeBlock";
  case NodeType::HtmlBlock:
    return "HtmlBlock";
  case NodeType::Paragraph:
    return "Paragraph";
  case NodeType::Heading:
    return "Heading";
  case NodeType::ThematicBreak:
    return "ThematicBreak";
  }
  return "Unknown";
}

inline std::string_view inlineTypeToString(InlineType t) {
  switch (t) {
  case InlineType::Text:
    return "Text";
  case InlineType::SoftBreak:
    return "SoftBreak";
  case InlineType::LineBreak:
    return "LineBreak";
  case InlineType::Code:
    return "Code";
  case InlineType::HtmlInline:
    return "HtmlInline";
  case InlineType::Emph:
    return "Emph";
  case InlineType::Strong:
    return "Strong";
  case InlineType::Link:
    return "Link";
  case InlineType::Image:
    return "Image";
  }
  return "Unknown";
}

} // namespace markdown_parser
