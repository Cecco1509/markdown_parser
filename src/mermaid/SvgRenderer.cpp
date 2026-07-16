#include "mermaid/SvgRenderer.hpp"

#include <cmath>
#include <sstream>
#include <string>

namespace mermaid {
namespace {

std::string xml_escape(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&': r += "&amp;"; break;
    case '<': r += "&lt;"; break;
    case '>': r += "&gt;"; break;
    case '"': r += "&quot;"; break;
    default: r += c;
    }
  }
  return r;
}

std::string num(double v) {
  std::ostringstream o;
  o.precision(2);
  o << std::fixed << v;
  std::string s = o.str();
  // trim trailing zeros / dot for compactness
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s;
}

// A shape centred at the local origin, width w, height h.
std::string shape_svg(ShapeKind shape, double w, double h) {
  const double hw = w / 2, hh = h / 2;
  auto poly = [](std::initializer_list<std::pair<double, double>> pts) {
    std::string s = "<polygon class=\"shape\" points=\"";
    for (auto [x, y] : pts) s += num(x) + "," + num(y) + " ";
    return s + "\"/>";
  };
  auto rect = [&](double rx) {
    return "<rect class=\"shape\" x=\"" + num(-hw) + "\" y=\"" + num(-hh) +
           "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\" rx=\"" +
           num(rx) + "\"/>";
  };
  // Slants/notches shift by h/2 (mermaid's rule); the hexagon's ends are h/4.
  const double lean = hh;
  const double hexlean = hh / 2;

  switch (shape) {
  case ShapeKind::RoundEdges: return rect(5);
  case ShapeKind::Stadium: return rect(hh);
  case ShapeKind::Subroutine:
    return rect(0) +
           "<line class=\"shape\" x1=\"" + num(-hw + 8) + "\" y1=\"" + num(-hh) +
           "\" x2=\"" + num(-hw + 8) + "\" y2=\"" + num(hh) + "\"/>" +
           "<line class=\"shape\" x1=\"" + num(hw - 8) + "\" y1=\"" + num(-hh) +
           "\" x2=\"" + num(hw - 8) + "\" y2=\"" + num(hh) + "\"/>";
  case ShapeKind::Cylinder: {
    // Same construction mermaid uses: the top cap is drawn as TWO arcs (back
    // half then front half) so the ellipse — the "lip" — is actually visible;
    // then down the left side, the front arc across the bottom, and back up the
    // right side. ry comes from the shared rule so it matches the sizing.
    const double ry = cylinder_ry(w);
    const double body = h - 2 * ry;
    return "<path class=\"shape\" d=\"M" + num(-hw) + "," + num(-hh + ry) +
           " a" + num(hw) + "," + num(ry) + " 0 0 0 " + num(w) + ",0" +
           " a" + num(hw) + "," + num(ry) + " 0 0 0 " + num(-w) + ",0" +
           " l0," + num(body) + " a" + num(hw) + "," + num(ry) + " 0 0 0 " +
           num(w) + ",0" + " l0," + num(-body) + "\"/>";
  }
  case ShapeKind::Circle:
    return "<circle class=\"shape\" r=\"" + num(std::max(hw, hh)) + "\"/>";
  case ShapeKind::DoubleCircle: {
    double r = std::max(hw, hh);
    return "<circle class=\"shape\" r=\"" + num(r) + "\"/>" +
           "<circle class=\"shape\" r=\"" + num(r - 5) + "\"/>";
  }
  case ShapeKind::Asymmetric:
  case ShapeKind::Rhombus:
  case ShapeKind::Hexagon:
  case ShapeKind::LeanRight:
  case ShapeKind::LeanLeft:
  case ShapeKind::Trapezoid:
  case ShapeKind::TrapezoidAlt: {
    // Drawn from the SAME outline the layout clips arrows to, so an arrow can
    // never stop short of (or inside) the border it is pointing at.
    std::string s = "<polygon class=\"shape\" points=\"";
    for (const Point &p : shape_outline(shape, w, h))
      s += num(p.x) + "," + num(p.y) + " ";
    return s + "\"/>";
  }
  case ShapeKind::Rect:
  default:
    return rect(0);
  }
}

// A polyline as an SVG path with rounded corners: straight runs stay straight,
// and each bend is replaced by a quadratic bezier tangent to both segments. A
// 2-point edge has no corner, so it comes out as a plain straight line.
std::string edge_path(const std::vector<Point> &pts, double radius) {
  std::string d = "M" + num(pts[0].x) + "," + num(pts[0].y);
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    const Point &a = pts[i - 1], &v = pts[i], &b = pts[i + 1];
    const double d1 = std::hypot(v.x - a.x, v.y - a.y);
    const double d2 = std::hypot(b.x - v.x, b.y - v.y);
    if (d1 < 1e-6 || d2 < 1e-6)
      continue;
    // Never eat more than half of either neighbouring segment.
    const double r = std::min({radius, d1 / 2, d2 / 2});
    const Point in{v.x + (a.x - v.x) * r / d1, v.y + (a.y - v.y) * r / d1};
    const Point out{v.x + (b.x - v.x) * r / d2, v.y + (b.y - v.y) * r / d2};
    d += " L" + num(in.x) + "," + num(in.y);
    d += " Q" + num(v.x) + "," + num(v.y) + " " + num(out.x) + "," + num(out.y);
  }
  d += " L" + num(pts.back().x) + "," + num(pts.back().y);
  return d;
}

const char *marker_id(ArrowHead h) {
  switch (h) {
  case ArrowHead::Arrow: return "arrowPoint";
  case ArrowHead::Circle: return "arrowCircle";
  case ArrowHead::Cross: return "arrowCross";
  case ArrowHead::None: return nullptr;
  }
  return nullptr;
}

std::string defs() {
  // One marker per arrow type, referenced by marker-start/-end.
  // markerUnits="userSpaceOnUse" is essential: the SVG default is "strokeWidth",
  // which would scale the head by the line width — making thick edges (===>)
  // sprout a 3x arrow. mermaid pins its markers to user space for the same
  // reason, so every arrow head is the same size regardless of stroke.
  return R"(<defs>
<marker id="arrowPoint" viewBox="0 0 10 10" refX="9" refY="5" markerUnits="userSpaceOnUse" markerWidth="9" markerHeight="9" orient="auto"><path d="M0,0 L10,5 L0,10 z"/></marker>
<marker id="arrowCircle" viewBox="0 0 12 12" refX="10" refY="6" markerUnits="userSpaceOnUse" markerWidth="9" markerHeight="9" orient="auto"><circle cx="6" cy="6" r="4"/></marker>
<marker id="arrowCross" viewBox="0 0 12 12" refX="6" refY="6" markerUnits="userSpaceOnUse" markerWidth="10" markerHeight="10" orient="auto"><path d="M2,2 L10,10 M10,2 L2,10" stroke="context-stroke" stroke-width="2"/></marker>
</defs>)";
}

std::string style(const RenderOptions &o) {
  std::ostringstream s;
  s << "<style>"
    << ".nodes .shape{fill:#ECECFF;stroke:#9370DB;stroke-width:1px;}"
    << ".edgePaths .flowchart-link{fill:none;stroke:#333;stroke-width:1px;"
       "stroke-linejoin:round;stroke-linecap:round;}"
    << ".flowchart-link.thick{stroke-width:3px;}"
    << ".flowchart-link.dotted{stroke-dasharray:3;}"
    << ".flowchart-link.invisible{stroke:none;}"
    << "marker{fill:#333;stroke:#333;}"
    << "text{font-family:" << o.font.family << ";font-size:" << num(o.font.size_px)
    << "px;fill:#333;}"
    // Must target the <text> itself: `dominant-baseline` is NOT inherited, so a
    // rule on the wrapping <g class="edgeLabel"> never reaches the text, which
    // then falls back to the alphabetic baseline and rides above the line.
    // (text-anchor IS inherited, which is why only the vertical was off.)
    << ".nodeLabel,.edgeLabel text{text-anchor:middle;dominant-baseline:central;}"
    << ".edgeLabel .bg{fill:#fff;}"
    << "</style>";
  return s.str();
}

std::string stroke_class(Stroke s) {
  switch (s) {
  case Stroke::Thick: return " thick";
  case Stroke::Dotted: return " dotted";
  case Stroke::Invisible: return " invisible";
  case Stroke::Normal: return "";
  }
  return "";
}

} // namespace

std::string render_svg(const Layout &layout, const RenderOptions &opts) {
  std::ostringstream s;
  const double W = layout.diagram.w, H = layout.diagram.h;
  s << "<svg id=\"" << xml_escape(opts.svg_id)
    << "\" xmlns=\"http://www.w3.org/2000/svg\" class=\"flowchart\" width=\""
    << num(W) << "\" height=\"" << num(H) << "\" viewBox=\"0 0 " << num(W) << " "
    << num(H) << "\">";
  s << style(opts) << defs();

  // Edges first (drawn under nodes).
  s << "<g class=\"edgePaths\">";
  for (const LaidEdge &e : layout.edges) {
    s << "<path class=\"flowchart-link" << stroke_class(e.stroke) << "\" d=\""
      << edge_path(e.points, opts.edge_corner_radius) << "\"";
    if (const char *m = marker_id(e.head_end)) s << " marker-end=\"url(#" << m << ")\"";
    if (e.head_start)
      if (const char *m = marker_id(ArrowHead::Arrow)) s << " marker-start=\"url(#" << m << ")\"";
    s << "/>";
  }
  s << "</g>";

  // Edge labels.
  s << "<g class=\"edgeLabels\">";
  for (const LaidEdge &e : layout.edges) {
    if (e.label.empty()) continue;
    // Use the box the layout measured and reserved, so the background matches
    // the space actually allocated (never a re-guess from the string length).
    const double w = e.label_size.w, h = e.label_size.h;
    s << "<g class=\"edgeLabel\" transform=\"translate(" << num(e.label_pos.x)
      << "," << num(e.label_pos.y) << ")\">"
      << "<rect class=\"bg\" x=\"" << num(-w / 2) << "\" y=\"" << num(-h / 2)
      << "\" width=\"" << num(w) << "\" height=\"" << num(h) << "\"/>"
      << "<text>" << xml_escape(e.label) << "</text></g>";
  }
  s << "</g>";

  // Nodes.
  s << "<g class=\"nodes\">";
  for (const LaidNode &n : layout.nodes) {
    s << "<g class=\"node default\" transform=\"translate(" << num(n.center.x)
      << "," << num(n.center.y) << ")\">" << shape_svg(n.shape, n.size.w, n.size.h)
      << "<text class=\"nodeLabel\">" << xml_escape(n.label) << "</text></g>";
  }
  s << "</g>";

  s << "</svg>";
  return s.str();
}

} // namespace mermaid
