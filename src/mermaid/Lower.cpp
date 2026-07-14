#include "mermaid/Lower.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace mermaid {
namespace {

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void add_unique(std::vector<std::string> &v, const std::string &s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

// Split a "fill:#f00,stroke:#333" body into trimmed, non-empty entries.
std::vector<std::string> split_styles(const std::string &body) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < body.size()) {
    size_t j = body.find(',', i);
    if (j == std::string::npos) j = body.size();
    size_t b = i, e = j;
    while (b < e && std::isspace((unsigned char)body[b])) ++b;
    while (e > b && std::isspace((unsigned char)body[e - 1])) --e;
    if (e > b) out.push_back(body.substr(b, e - b));
    i = j + 1;
  }
  return out;
}

class Lowering {
public:
  FlowDb run(const ast::Document &doc) {
    db_.direction = doc.direction.value_or(Direction::TB);
    // title, click, linkStyle intentionally deferred (docs/mermaid/deferred.md)
    lower_stmts(doc.stmts, nullptr);
    return std::move(db_);
  }

private:
  // Ensure a vertex exists (create-on-reference); return its index.
  int ensure_vertex(const std::string &id) {
    auto it = index_.find(id);
    if (it != index_.end()) return it->second;
    int idx = static_cast<int>(db_.vertices.size());
    Vertex v;
    v.id = id;
    v.label = id; // bare default; overwritten by a shaped mention
    db_.vertices.push_back(std::move(v));
    index_[id] = idx;
    return idx;
  }

  // Register/update a vertex from a node reference; add to `cur` membership.
  void apply_node(const ast::Node &n, Subgraph *cur) {
    Vertex &v = db_.vertices[ensure_vertex(n.id)];
    if (n.shape) { // last-label-wins
      v.label = n.shape->label;
      v.shape = n.shape->shape;
      v.explicit_shape = true;
    }
    if (n.cls) v.classes.push_back(*n.cls); // mermaid does not dedup classes
    if (cur) add_unique(cur->nodes, n.id);
  }

  void lower_edge(const ast::EdgeStmt &e, Subgraph *cur) {
    for (const ast::NodeGroup &g : e.groups)
      for (const ast::Node &n : g) apply_node(n, cur);

    for (size_t i = 0; i + 1 < e.groups.size(); ++i) {
      const ast::NodeGroup &L = e.groups[i];
      const ast::NodeGroup &R = e.groups[i + 1];
      const ast::EdgeOp &op = e.ops[i];
      for (const ast::Node &a : L)           // left group outer,
        for (const ast::Node &b : R) {       // right group inner (matches mermaid)
          Edge ed;
          ed.start = a.id;
          ed.end = b.id;
          ed.label = op.pipe_label ? *op.pipe_label : op.link.label;
          ed.stroke = op.link.stroke;
          ed.head_end = op.link.head_end;
          ed.head_start = op.link.head_start;
          ed.length = op.link.length;
          db_.edges.push_back(std::move(ed));
        }
    }
  }

  void lower_subgraph(const ast::SubgraphNode &sg, Subgraph *parent) {
    Subgraph current; // nodes + dir filled while lowering the body
    lower_stmts(sg.body, &current);

    // Post-order registration: the counter advances for every subgraph, so a
    // named one still consumes an index (matches mermaid's subGraphN scheme).
    int index = subgraph_counter_++;
    current.id = sg.head.id.value_or("subGraph" + std::to_string(index));
    current.label = sg.head.title.value_or(current.id);

    db_.subgraphs.push_back(std::move(current));
    if (parent) add_unique(parent->nodes, db_.subgraphs.back().id);
  }

  void lower_style(const ast::StyleStmt &s) {
    Vertex &v = db_.vertices[ensure_vertex(s.target)];
    for (std::string &e : split_styles(s.body)) v.styles.push_back(std::move(e));
  }

  void lower_classdef(const ast::ClassDefStmt &c) {
    std::string name = c.is_default ? "default" : c.name;
    ClassDef def;
    def.id = name;
    def.styles = split_styles(c.body);
    db_.classes[name] = std::move(def);
  }

  void lower_class(const ast::ClassStmt &c) {
    for (const std::string &id : c.ids) {
      Vertex &v = db_.vertices[ensure_vertex(id)];
      v.classes.push_back(c.cls); // mermaid does not dedup classes
    }
  }

  void lower_stmts(const std::vector<ast::Stmt> &stmts, Subgraph *cur) {
    for (const ast::Stmt &s : stmts) {
      std::visit(overloaded{
                     [&](const ast::EdgeStmt &e) { lower_edge(e, cur); },
                     [&](const ast::SubgraphPtr &sg) { lower_subgraph(*sg, cur); },
                     [&](const ast::DirectionStmt &d) { if (cur) cur->dir = d.dir; },
                     [&](const ast::StyleStmt &st) { lower_style(st); },
                     [&](const ast::ClassDefStmt &c) { lower_classdef(c); },
                     [&](const ast::ClassStmt &c) { lower_class(c); },
                     [&](const ast::LinkStyleStmt &) { /* deferred */ },
                     [&](const ast::ClickStmt &) { /* deferred */ },
                 },
                 s);
    }
  }

  FlowDb db_;
  std::unordered_map<std::string, int> index_; // vertex id -> index
  int subgraph_counter_ = 0;
};

} // namespace

FlowDb lower(const ast::Document &doc) { return Lowering().run(doc); }

} // namespace mermaid
