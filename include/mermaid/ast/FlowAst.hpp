#pragma once

#include "mermaid/FlowDb.hpp" // Direction, ShapeKind, Stroke, ArrowHead
#include "mermaid/Token.hpp"  // ShapeTok, LinkTok

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ── Flow AST ────────────────────────────────────────────────────────────────
// The parse tree produced by the generated LR(1) parser. It mirrors the grammar
// structure and does NO semantics: the messy, stateful work (& cross-product,
// last-label-wins, subgraph membership, class resolution, direction defaulting)
// is deferred to lower(Document) -> FlowDb.
//
// Grammar actions are pure calls to the factory functions at the bottom of this
// file, so the C++ compiler type-checks every production against these types.
// SHAPE/LINK terminals carry ShapeTok/LinkTok, which already model the parsed
// data, so the AST reuses them directly.
// ---------------------------------------------------------------------------

namespace mermaid::ast {

struct Node {                      // A / A[..] / A:::c / A[..]:::c
  std::string id;
  std::optional<ShapeTok> shape;   // label + ShapeKind, or none (bare id)
  std::optional<std::string> cls;  // :::className, or none
};

using NodeGroup = std::vector<Node>; // the `A & B & C` group

struct EdgeOp {                    // one operator between two node groups
  LinkTok link;                    // stroke / head / length / inline label
  std::optional<std::string> pipe_label; // the -->|text| form, or none
};

struct EdgeStmt {                  // chain: g0 op0 g1 op1 g2 ...
  std::vector<NodeGroup> groups;   // size N
  std::vector<EdgeOp> ops;         // size N-1
};

struct DirectionStmt { Direction dir; };
struct StyleStmt { std::string target; std::string body; };
struct ClassDefStmt { std::string name; std::string body; bool is_default = false; };
struct ClassStmt { std::vector<std::string> ids; std::string cls; };
struct LinkStyleStmt { std::vector<std::string> indices; std::string body; bool is_default = false; };
struct ClickStmt { std::string id; std::vector<std::string> args; };

struct SubgraphHead {
  std::optional<std::string> id;
  std::optional<std::string> title;
};

struct SubgraphNode; // forward decl breaks the Stmt <-> Subgraph recursion
using SubgraphPtr = std::unique_ptr<SubgraphNode>;

using Stmt = std::variant<EdgeStmt, SubgraphPtr, DirectionStmt, StyleStmt,
                          ClassDefStmt, ClassStmt, LinkStyleStmt, ClickStmt>;

struct SubgraphNode {
  SubgraphHead head;
  std::vector<Stmt> body;
};

struct Document {
  std::optional<Direction> direction;
  std::vector<Stmt> stmts;
};

// ── factories (called by grammar actions) ───────────────────────────────────

// generic list + statement-wrapper helpers
template <class T> std::vector<T> one(T x) {
  std::vector<T> v;
  v.push_back(std::move(x));
  return v;
}
template <class T> std::vector<T> push(std::vector<T> v, T x) {
  v.push_back(std::move(x));
  return v;
}
template <class T> Stmt stmt(T x) { return Stmt{std::move(x)}; }

// document / header
inline Document document(std::optional<Direction> dir, std::vector<Stmt> stmts) {
  return Document{std::move(dir), std::move(stmts)};
}
inline std::optional<Direction> no_dir() { return std::nullopt; }
inline std::optional<Direction> some_dir(Direction d) { return d; }
inline std::vector<Stmt> nil_stmts() { return {}; }

// edges & nodes
inline EdgeStmt chain(NodeGroup g) {
  EdgeStmt e;
  e.groups.push_back(std::move(g));
  return e;
}
inline EdgeStmt chain(EdgeStmt e, EdgeOp op, NodeGroup g) {
  e.ops.push_back(std::move(op));
  e.groups.push_back(std::move(g));
  return e;
}
inline EdgeOp edge_op(LinkTok l) { return EdgeOp{std::move(l), std::nullopt}; }
inline EdgeOp edge_op(LinkTok l, std::string pipe) {
  return EdgeOp{std::move(l), std::move(pipe)};
}
inline Node node(std::string id) { return Node{std::move(id), std::nullopt, std::nullopt}; }
inline Node node(std::string id, ShapeTok shape) {
  return Node{std::move(id), std::move(shape), std::nullopt};
}
inline Node node_cls(std::string id, std::string cls) {
  return Node{std::move(id), std::nullopt, std::move(cls)};
}
inline Node node_cls(std::string id, ShapeTok shape, std::string cls) {
  return Node{std::move(id), std::move(shape), std::move(cls)};
}

// subgraphs
inline SubgraphHead sg_head() { return SubgraphHead{}; }
inline SubgraphHead sg_head_id(std::string id) {
  return SubgraphHead{std::move(id), std::nullopt};
}
inline SubgraphHead sg_head_id(std::string id, ShapeTok shape) {
  return SubgraphHead{std::move(id), std::move(shape.label)};
}
inline SubgraphHead sg_head_title(std::string title) {
  return SubgraphHead{std::nullopt, std::move(title)};
}
inline SubgraphPtr subgraph(SubgraphHead head, std::vector<Stmt> body) {
  auto p = std::make_unique<SubgraphNode>();
  p->head = std::move(head);
  p->body = std::move(body);
  return p;
}

// styling & interaction
inline DirectionStmt direction_stmt(Direction d) { return DirectionStmt{d}; }
inline StyleStmt style_stmt(std::string target, std::string body) {
  return StyleStmt{std::move(target), std::move(body)};
}
inline ClassDefStmt classdef_stmt(std::string name, std::string body) {
  return ClassDefStmt{std::move(name), std::move(body), false};
}
inline ClassDefStmt classdef_default(std::string body) {
  return ClassDefStmt{std::string{}, std::move(body), true};
}
inline ClassStmt class_stmt(std::vector<std::string> ids, std::string cls) {
  return ClassStmt{std::move(ids), std::move(cls)};
}
inline LinkStyleStmt linkstyle_stmt(std::vector<std::string> indices, std::string body) {
  return LinkStyleStmt{std::move(indices), std::move(body), false};
}
inline LinkStyleStmt linkstyle_default(std::string body) {
  return LinkStyleStmt{{}, std::move(body), true};
}
inline ClickStmt click_stmt(std::string id, std::vector<std::string> args) {
  return ClickStmt{std::move(id), std::move(args)};
}
inline std::vector<std::string> pair(std::string a, std::string b) {
  std::vector<std::string> v;
  v.push_back(std::move(a));
  v.push_back(std::move(b));
  return v;
}
inline std::vector<std::string> triple(std::string a, std::string b, std::string c) {
  std::vector<std::string> v;
  v.push_back(std::move(a));
  v.push_back(std::move(b));
  v.push_back(std::move(c));
  return v;
}

} // namespace mermaid::ast
