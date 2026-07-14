#include "mermaid/AstDump.hpp"

#include <sstream>
#include <string>
#include <variant>

namespace mermaid {
namespace {

const char *dir_name(Direction d) {
  switch (d) {
  case Direction::TB: return "TB";
  case Direction::BT: return "BT";
  case Direction::LR: return "LR";
  case Direction::RL: return "RL";
  }
  return "?";
}
const char *shape_name(ShapeKind s) {
  switch (s) {
  case ShapeKind::Rect: return "Rect";
  case ShapeKind::RoundEdges: return "RoundEdges";
  case ShapeKind::Stadium: return "Stadium";
  case ShapeKind::Subroutine: return "Subroutine";
  case ShapeKind::Cylinder: return "Cylinder";
  case ShapeKind::Circle: return "Circle";
  case ShapeKind::Asymmetric: return "Asymmetric";
  case ShapeKind::Rhombus: return "Rhombus";
  case ShapeKind::Hexagon: return "Hexagon";
  case ShapeKind::LeanRight: return "LeanRight";
  case ShapeKind::LeanLeft: return "LeanLeft";
  case ShapeKind::Trapezoid: return "Trapezoid";
  case ShapeKind::TrapezoidAlt: return "TrapezoidAlt";
  case ShapeKind::DoubleCircle: return "DoubleCircle";
  }
  return "?";
}
const char *stroke_name(Stroke s) {
  switch (s) {
  case Stroke::Normal: return "normal";
  case Stroke::Thick: return "thick";
  case Stroke::Dotted: return "dotted";
  case Stroke::Invisible: return "invisible";
  }
  return "?";
}
const char *head_name(ArrowHead h) {
  switch (h) {
  case ArrowHead::None: return "none";
  case ArrowHead::Arrow: return "arrow";
  case ArrowHead::Circle: return "circle";
  case ArrowHead::Cross: return "cross";
  }
  return "?";
}

class Printer {
public:
  std::string run(const ast::Document &doc) {
    line(0, std::string("Document direction=") +
                (doc.direction ? dir_name(*doc.direction) : "(default)"));
    for (const ast::Stmt &s : doc.stmts) stmt(1, s);
    return out_.str();
  }

private:
  void line(int depth, const std::string &text) {
    out_ << std::string(depth * 2, ' ') << text << '\n';
  }

  std::string node(const ast::Node &n) {
    std::string r = n.id;
    if (n.shape)
      r += std::string("[") + shape_name(n.shape->shape) + " \"" +
           n.shape->label + "\"]";
    if (n.cls) r += ":::" + *n.cls;
    return r;
  }

  std::string group(const ast::NodeGroup &g) {
    std::string r;
    for (size_t i = 0; i < g.size(); ++i) r += (i ? " & " : "") + node(g[i]);
    return r;
  }

  std::string op(const ast::EdgeOp &e) {
    std::string r = std::string("--") + stroke_name(e.link.stroke) + "/" +
                    head_name(e.link.head_end);
    if (e.link.head_start) r += "/bi";
    r += " len=" + std::to_string(e.link.length);
    if (e.link.has_label) r += " label=\"" + e.link.label + "\"";
    if (e.pipe_label) r += " pipe=\"" + *e.pipe_label + "\"";
    return r + "-->";
  }

  void stmt(int depth, const ast::Stmt &s) {
    std::visit(
        [&](const auto &x) { this->emit(depth, x); }, s);
  }

  void emit(int depth, const ast::EdgeStmt &e) {
    line(depth, "EdgeStmt");
    for (size_t i = 0; i < e.groups.size(); ++i) {
      line(depth + 1, "group[" + std::to_string(i) + "] " + group(e.groups[i]));
      if (i < e.ops.size()) line(depth + 1, op(e.ops[i]));
    }
  }
  void emit(int depth, const ast::SubgraphPtr &sg) {
    std::string h = "Subgraph";
    if (sg->head.id) h += " id=" + *sg->head.id;
    if (sg->head.title) h += " title=\"" + *sg->head.title + "\"";
    line(depth, h);
    for (const ast::Stmt &s : sg->body) stmt(depth + 1, s);
  }
  void emit(int depth, const ast::DirectionStmt &d) {
    line(depth, std::string("Direction ") + dir_name(d.dir));
  }
  void emit(int depth, const ast::StyleStmt &s) {
    line(depth, "Style target=" + s.target + " body=\"" + s.body + "\"");
  }
  void emit(int depth, const ast::ClassDefStmt &c) {
    line(depth, "ClassDef " + (c.is_default ? std::string("default") : c.name) +
                    " body=\"" + c.body + "\"");
  }
  void emit(int depth, const ast::ClassStmt &c) {
    std::string ids;
    for (size_t i = 0; i < c.ids.size(); ++i) ids += (i ? "," : "") + c.ids[i];
    line(depth, "Class ids=[" + ids + "] class=" + c.cls);
  }
  void emit(int depth, const ast::LinkStyleStmt &l) {
    std::string idx;
    for (size_t i = 0; i < l.indices.size(); ++i)
      idx += (i ? "," : "") + l.indices[i];
    line(depth, "LinkStyle " +
                    (l.is_default ? std::string("default") : "[" + idx + "]") +
                    " body=\"" + l.body + "\"");
  }
  void emit(int depth, const ast::ClickStmt &c) {
    std::string args;
    for (size_t i = 0; i < c.args.size(); ++i)
      args += (i ? " " : "") + ("\"" + c.args[i] + "\"");
    line(depth, "Click id=" + c.id + " args=[" + args + "]");
  }

  std::ostringstream out_;
};

} // namespace

std::string dump_ast(const ast::Document &doc) { return Printer().run(doc); }

} // namespace mermaid
