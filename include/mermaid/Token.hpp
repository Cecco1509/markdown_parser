#pragma once

#include "mermaid/FlowDb.hpp"

#include <cstdint>
#include <string>
#include <variant>

// ── Token ─────────────────────────────────────────────────────────────────
// The flat token stream the lexer produces. Each terminal is a distinct
// alternative of `TokenValue`, so a payload field only exists on the token kind
// that actually carries it (the compiler enforces this — there is no `dir`
// field to misread off a link, etc.). Payload-less terminals (punctuation and
// reserved keywords) share the `PunctTok` alternative and are distinguished by
// the `Punct` enum.
//
// The lexer is context-free apart from a small set of scan-until-delimiter
// subroutines for delimited raw-text runs (quoted strings, bracket labels, pipe
// labels) — the same mechanical pattern every compiler uses for string
// literals. It performs no semantic reasoning (no symbol table, no meaning);
// that is the parser's job. Reserved keywords are classified unconditionally,
// so there is no context-sensitive keyword/identifier mode.
// ---------------------------------------------------------------------------

namespace mermaid {

// Payload-less terminals. The specific one is carried by this enum.
enum class Punct : uint8_t {
  End,       // end-of-input sentinel ($)
  Newline,   // statement separator (also ';')
  Ampersand, // &  (node-group cross product)
  Pipe,      // |  (edge label delimiter, -->|text|)
  Comma,     // ,  (class / linkStyle id lists)
  TripleColon, // :::  (inline class attach)
  // reserved keywords (classified unconditionally)
  KwGraph,     // graph | flowchart
  KwSubgraph,  // subgraph
  KwEnd,       // end
  KwDirection, // direction
  KwStyle,     // style
  KwClassDef,  // classDef
  KwClass,     // class
  KwLinkStyle, // linkStyle
  KwClick,     // click
  KwDefault    // default
};

struct PunctTok {
  Punct punct;
};

struct DirTok {
  Direction dir;
};

struct NumTok {
  std::string text; // 0-based index (linkStyle)
};

struct IdTok {
  std::string text; // bare node identifier
};

struct ShapeTok {
  std::string label;                  // text inside the delimiters
  ShapeKind shape = ShapeKind::Rect;  // which bracket family
};

struct StrTok {
  std::string text; // contents of a "quoted string" / pipe label (unquoted)
};

struct StyleBodyTok {
  std::string text; // trailing "fill:#f00,..." captured to end of line
};

struct LinkTok {
  Stroke stroke = Stroke::Normal;
  ArrowHead head_end = ArrowHead::Arrow;
  bool head_start = false; // bidirectional (<-->, <--o, <--x)
  int length = 1;          // extra dashes/dots/equals
  std::string label;       // inline `-- text -->` label
  bool has_label = false;
};

using TokenValue = std::variant<PunctTok, DirTok, NumTok, IdTok, ShapeTok,
                                StrTok, StyleBodyTok, LinkTok>;

struct Token {
  TokenValue value;
  int line = 0; // 1-based source position, for diagnostics
  int col = 0;

  // Typed accessor: returns nullptr if the token is not of type T.
  template <class T> const T *as() const { return std::get_if<T>(&value); }

  // Convenience: is this a specific payload-less terminal?
  bool is(Punct p) const {
    const auto *pt = std::get_if<PunctTok>(&value);
    return pt != nullptr && pt->punct == p;
  }
};

// Human-readable rendering of a token, for tests and diagnostics.
std::string to_string(const Token &t);

} // namespace mermaid
