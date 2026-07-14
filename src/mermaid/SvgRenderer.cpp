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
  const double lean = std::min(hw, hh) * 0.6; // slant for lean/trapezoid shapes

  switch (shape) {
  case ShapeKind::RoundEdges: return rect(6);
  case ShapeKind::Stadium: return rect(hh);
  case ShapeKind::Subroutine:
    return rect(0) +
           "<line class=\"shape\" x1=\"" + num(-hw + 6) + "\" y1=\"" + num(-hh) +
           "\" x2=\"" + num(-hw + 6) + "\" y2=\"" + num(hh) + "\"/>" +
           "<line class=\"shape\" x1=\"" + num(hw - 6) + "\" y1=\"" + num(-hh) +
           "\" x2=\"" + num(hw - 6) + "\" y2=\"" + num(hh) + "\"/>";
  case ShapeKind::Cylinder: {
    double ry = hh * 0.25;
    return "<path class=\"shape\" d=\"M" + num(-hw) + "," + num(-hh + ry) +
           " a" + num(hw) + "," + num(ry) + " 0 0 0 " + num(w) + ",0" +
           " v" + num(h - 2 * ry) + " a" + num(hw) + "," + num(ry) +
           " 0 0 1 " + num(-w) + ",0 z\"/>" +
           "<path class=\"shape\" d=\"M" + num(-hw) + "," + num(-hh + ry) +
           " a" + num(hw) + "," + num(ry) + " 0 0 0 " + num(w) + ",0\"/>";
  }
  case ShapeKind::Circle:
    return "<circle class=\"shape\" r=\"" + num(std::max(hw, hh)) + "\"/>";
  case ShapeKind::DoubleCircle: {
    double r = std::max(hw, hh);
    return "<circle class=\"shape\" r=\"" + num(r) + "\"/>" +
           "<circle class=\"shape\" r=\"" + num(r - 4) + "\"/>";
  }
  case ShapeKind::Asymmetric: // odd flag: rectangle with pointed left
    return poly({{-hw + hh, -hh}, {hw, -hh}, {hw, hh}, {-hw + hh, hh}, {-hw, 0}});
  case ShapeKind::Rhombus:
    return poly({{0, -hh}, {hw, 0}, {0, hh}, {-hw, 0}});
  case ShapeKind::Hexagon:
    return poly({{-hw + hh, -hh}, {hw - hh, -hh}, {hw, 0}, {hw - hh, hh}, {-hw + hh, hh}, {-hw, 0}});
  case ShapeKind::LeanRight:
    return poly({{-hw + lean, -hh}, {hw, -hh}, {hw - lean, hh}, {-hw, hh}});
  case ShapeKind::LeanLeft:
    return poly({{-hw, -hh}, {hw - lean, -hh}, {hw, hh}, {-hw + lean, hh}});
  case ShapeKind::Trapezoid:
    return poly({{-hw + lean, -hh}, {hw - lean, -hh}, {hw, hh}, {-hw, hh}});
  case ShapeKind::TrapezoidAlt:
    return poly({{-hw, -hh}, {hw, -hh}, {hw - lean, hh}, {-hw + lean, hh}});
  case ShapeKind::Rect:
  default:
    return rect(0);
  }
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
  return R"(<defs>
<marker id="arrowPoint" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L10,5 L0,10 z"/></marker>
<marker id="arrowCircle" viewBox="0 0 12 12" refX="10" refY="6" markerWidth="9" markerHeight="9" orient="auto"><circle cx="6" cy="6" r="4"/></marker>
<marker id="arrowCross" viewBox="0 0 12 12" refX="6" refY="6" markerWidth="9" markerHeight="9" orient="auto"><path d="M2,2 L10,10 M10,2 L2,10" stroke="context-stroke" stroke-width="2"/></marker>
</defs>)";
}

std::string style(const RenderOptions &o) {
  std::ostringstream s;
  s << "<style>"
    << ".nodes .shape{fill:#ECECFF;stroke:#9370DB;stroke-width:1px;}"
    << ".edgePaths .flowchart-link{fill:none;stroke:#333;stroke-width:1px;}"
    << ".flowchart-link.thick{stroke-width:3px;}"
    << ".flowchart-link.dotted{stroke-dasharray:3;}"
    << ".flowchart-link.invisible{stroke:none;}"
    << "marker{fill:#333;stroke:#333;}"
    << "text{font-family:" << o.font.family << ";font-size:" << num(o.font.size_px)
    << "px;fill:#333;}"
    << ".nodeLabel,.edgeLabel{text-anchor:middle;dominant-baseline:central;}"
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
    s << "<path class=\"flowchart-link" << stroke_class(e.stroke) << "\" d=\"M";
    for (size_t i = 0; i < e.points.size(); ++i)
      s << (i ? " L" : "") << num(e.points[i].x) << "," << num(e.points[i].y);
    s << "\"";
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
    double w = e.label.size() * 0.6 * opts.font.size_px + 8;
    s << "<g class=\"edgeLabel\" transform=\"translate(" << num(e.label_pos.x)
      << "," << num(e.label_pos.y) << ")\">"
      << "<rect class=\"bg\" x=\"" << num(-w / 2) << "\" y=\"-10\" width=\""
      << num(w) << "\" height=\"20\"/>"
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
