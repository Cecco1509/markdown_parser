#pragma once

#include "mermaid/FlowDb.hpp"
#include "mermaid/TextMeasure.hpp"

#include <string>
#include <vector>

// The layout stage: FlowDb -> positioned graph (Layout). A minimal Sugiyama
// pipeline (cycle-break, longest-path ranking, barycenter ordering, coordinate
// assignment, polyline routing). See docs/mermaid/rendering.md.

namespace mermaid {

struct Point { double x = 0, y = 0; };
struct Size { double w = 0, h = 0; };

struct LaidNode {
  std::string id;
  std::string label;
  ShapeKind shape = ShapeKind::Rect;
  Point center;
  Size size;
  std::vector<std::string> classes; // carried for the renderer (styling later)
};

struct LaidEdge {
  std::string start, end, label;
  std::vector<Point> points; // polyline waypoints, incl. clipped endpoints
  Stroke stroke = Stroke::Normal;
  ArrowHead head_end = ArrowHead::Arrow;
  bool head_start = false;
  Point label_pos;
};

struct LaidSubgraph { // unused in v1 (subgraphs are collapsed); here for later
  std::string id, label;
  Point origin;
  Size size;
  int depth = 0;
};

struct Layout {
  Size diagram; // overall bounds -> SVG viewBox
  std::vector<LaidNode> nodes;
  std::vector<LaidEdge> edges;
  std::vector<LaidSubgraph> subgraphs; // empty in v1
};

struct LayoutOptions {
  FontSpec font;
  double node_sep = 40; // gap between nodes within a rank
  double rank_sep = 50; // gap between ranks
  double margin = 16;   // diagram padding
  bool collapse_subgraphs = true; // v1: collapse subgraphs into single nodes
};

// v1 simplification: replace each subgraph with a single node (label = title/id),
// redirect edges through the outermost owner, drop resulting self-loops, dedup.
FlowDb collapse_subgraphs(const FlowDb &db);

Layout layout(const FlowDb &db, TextMeasurer &measurer, const LayoutOptions &opts = {});

} // namespace mermaid
