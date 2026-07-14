#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// ── FlowDb ────────────────────────────────────────────────────────────────
// The parser's output: a semantic model of a mermaid `flowchart` / `graph`
// diagram, targeting the mermaid 11.0.0 feature set.
//
// This module is intentionally standalone (namespace `mermaid`, not nested in
// markdown_parser): flowchart parse -> layout -> SVG has no dependency on
// markdown. Markdown only *invokes* it, through the code-fence handler.
//
// FlowDb is also independent of the JSON emitted by mermaid-utils/extract.mjs.
// The test layer bridges the two with a normalizing comparator rather than a
// shared schema, so our AST is free to model the diagram in whatever shape is
// most convenient for layout + rendering.
// ---------------------------------------------------------------------------

namespace mermaid {

enum class Direction { TB, BT, LR, RL };

enum class ShapeKind {
  Rect,         // A[text]
  RoundEdges,   // A(text)
  Stadium,      // A([text])
  Subroutine,   // A[[text]]
  Cylinder,     // A[(text)]
  Circle,       // A((text))
  Asymmetric,   // A>text]
  Rhombus,      // A{text}
  Hexagon,      // A{{text}}
  LeanRight,    // A[/text/]
  LeanLeft,     // A[\text\]
  Trapezoid,    // A[/text\]
  TrapezoidAlt, // A[\text/]
  DoubleCircle  // A(((text)))
};

enum class Stroke { Normal, Thick, Dotted, Invisible };

enum class ArrowHead { None, Arrow, Circle, Cross };

struct Vertex {
  std::string id;
  std::string label;
  ShapeKind shape = ShapeKind::Rect;
  // A bare node (no brackets) is mermaid shape "rect"; an explicit [text] is
  // "square". Both are ShapeKind::Rect here, so this flag preserves whether a
  // shape was ever written explicitly.
  bool explicit_shape = false;
  std::vector<std::string> classes; // :::name and `class` assignments
  std::vector<std::string> styles;  // inline `style` declarations
};

struct Edge {
  std::string start;
  std::string end;
  std::string label;
  Stroke stroke = Stroke::Normal;
  ArrowHead head_end = ArrowHead::Arrow;
  bool head_start = false; // true for bidirectional (<-->, <--o, <--x)
  int length = 1;          // rank span from extra dashes/dots/equals
};

struct Subgraph {
  std::string id;
  std::string label;
  std::vector<std::string> nodes;
  std::optional<Direction> dir; // per-subgraph `direction` override
};

struct ClassDef {
  std::string id;
  std::vector<std::string> styles;
};

struct FlowDb {
  Direction direction = Direction::TB;
  std::string title;
  std::vector<Vertex> vertices; // insertion order; last-label-wins merged
  std::vector<Edge> edges;
  std::vector<Subgraph> subgraphs;
  std::map<std::string, ClassDef> classes;
};

} // namespace mermaid
