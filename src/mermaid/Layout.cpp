#include "mermaid/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mermaid {

// ── collapse_subgraphs (v1) ─────────────────────────────────────────────────
FlowDb collapse_subgraphs(const FlowDb &db) {
  if (db.subgraphs.empty()) return db;

  std::set<std::string> sg_ids;
  std::map<std::string, std::string> sg_label;
  for (const Subgraph &s : db.subgraphs) {
    sg_ids.insert(s.id);
    sg_label[s.id] = s.label;
  }

  // Direct owner of each member vertex, and parent of each nested subgraph.
  std::map<std::string, std::string> owner;  // vertex id -> subgraph id
  std::map<std::string, std::string> parent; // subgraph id -> parent subgraph id
  for (const Subgraph &s : db.subgraphs)
    for (const std::string &n : s.nodes) {
      if (sg_ids.count(n)) parent[n] = s.id; // nested subgraph
      else owner[n] = s.id;                  // member vertex
    }

  auto outermost = [&](std::string sg) {
    while (parent.count(sg)) sg = parent[sg];
    return sg;
  };
  // Representative node id for any vertex after collapse.
  auto rep = [&](const std::string &v) -> std::string {
    if (owner.count(v)) return outermost(owner[v]); // member -> outermost cluster
    if (sg_ids.count(v)) return outermost(v);       // a subgraph used as a node
    return v;                                       // free vertex
  };

  FlowDb out;
  out.direction = db.direction;
  out.title = db.title;
  out.classes = db.classes;

  // Vertices: emit each representative once, in first-appearance order.
  std::unordered_map<std::string, int> idx;
  auto ensure = [&](const std::string &id) -> Vertex & {
    auto it = idx.find(id);
    if (it != idx.end()) return out.vertices[it->second];
    idx[id] = static_cast<int>(out.vertices.size());
    Vertex v;
    v.id = id;
    if (sg_ids.count(id)) { // a collapsed subgraph node
      v.label = sg_label[id];
      v.explicit_shape = true;
    } else {
      v.label = id;
    }
    out.vertices.push_back(std::move(v));
    return out.vertices.back();
  };
  for (const Vertex &v : db.vertices) {
    std::string r = rep(v.id);
    Vertex &nv = ensure(r);
    if (r == v.id) nv = v; // keep the real vertex's label/shape/classes/styles
  }

  // Edges: redirect endpoints, drop self-loops, dedup.
  std::set<std::string> seen;
  for (const Edge &e : db.edges) {
    Edge ne = e;
    ne.start = rep(e.start);
    ne.end = rep(e.end);
    if (ne.start == ne.end) continue; // internal to a collapsed subgraph
    std::string key = ne.start + '\x01' + ne.end + '\x01' + ne.label + '\x01' +
                      std::to_string((int)ne.stroke) + std::to_string((int)ne.head_end) +
                      std::to_string(ne.head_start) + std::to_string(ne.length);
    if (!seen.insert(key).second) continue;
    out.edges.push_back(std::move(ne));
  }
  return out;
}

namespace {

// A layout cell: a real node or a routing dummy. Coordinates are computed in
// direction-agnostic axes `a` (order axis) and `b` (rank axis), then mapped to
// (x,y) by the diagram direction.
struct Cell {
  std::string id;
  bool dummy = false;
  int rank = 0;
  double w = 0, h = 0; // real node size (0 for dummies)
  double a = 0, b = 0; // order-axis / rank-axis positions
  std::vector<int> up, down; // neighbour cell indices in adjacent ranks
};

class Layouter {
public:
  Layouter(const FlowDb &db, TextMeasurer &m, const LayoutOptions &o)
      : db_(db), meas_(m), opt_(o) {}

  Layout run() {
    build_nodes();
    break_cycles();
    assign_ranks();
    build_layers_and_chains(); // inserts dummies, records per-edge routing chains
    order_ranks();
    assign_coords();
    return emit();
  }

private:
  // ── phase 0a: nodes + sizes ───────────────────────────────────────────
  void build_nodes() {
    std::vector<std::string> labels;
    for (const Vertex &v : db_.vertices)
      labels.push_back(v.label.empty() ? v.id : v.label);
    std::vector<LabelBox> boxes = meas_.measure(labels, opt_.font);

    for (size_t i = 0; i < db_.vertices.size(); ++i) {
      Cell c;
      c.id = db_.vertices[i].id;
      Size s = shape_pad(db_.vertices[i].shape, boxes[i]);
      c.w = s.w;
      c.h = s.h;
      index_[c.id] = static_cast<int>(cells_.size());
      cells_.push_back(std::move(c));
    }
    real_count_ = static_cast<int>(cells_.size());
  }

  static Size shape_pad(ShapeKind shape, const LabelBox &t) {
    const double px = 12, py = 8;
    double w = t.w + 2 * px, h = t.h + 2 * py;
    switch (shape) {
    case ShapeKind::Rhombus:
    case ShapeKind::Hexagon:
      w = t.w + 2 * px + t.h; // wider to fit angled sides
      h = t.h + 2 * py;
      break;
    case ShapeKind::Circle:
    case ShapeKind::DoubleCircle: {
      double d = std::max(w, h);
      w = h = d;
      break;
    }
    default:
      break;
    }
    return {std::max(w, 24.0), std::max(h, 20.0)};
  }

  // ── phase 0b: cycle breaking (DFS back-edge reversal) ─────────────────
  void break_cycles() {
    // Directed adjacency over real edges (skip self-loops).
    std::vector<std::vector<std::pair<int, int>>> adj(real_count_); // (edgeIdx, to)
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      int u = index_.count(db_.edges[i].start) ? index_[db_.edges[i].start] : -1;
      int v = index_.count(db_.edges[i].end) ? index_[db_.edges[i].end] : -1;
      if (u < 0 || v < 0 || u == v) continue;
      adj[u].push_back({i, v});
    }
    reversed_.assign(db_.edges.size(), false);
    std::vector<int> state(real_count_, 0); // 0 white, 1 gray, 2 black
    // Iterative DFS to avoid deep recursion.
    for (int s = 0; s < real_count_; ++s) {
      if (state[s]) continue;
      std::vector<std::pair<int, size_t>> stk{{s, 0}};
      state[s] = 1;
      while (!stk.empty()) {
        auto &[u, k] = stk.back();
        if (k < adj[u].size()) {
          auto [ei, v] = adj[u][k++];
          if (state[v] == 1) reversed_[ei] = true; // back edge -> reverse
          else if (state[v] == 0) { state[v] = 1; stk.push_back({v, 0}); }
        } else {
          state[u] = 2;
          stk.pop_back();
        }
      }
    }
  }

  // Direction of an edge after cycle-breaking: (from,to) as a DAG edge.
  std::pair<int, int> dag_dir(int edgeIdx) const {
    int u = index_.at(db_.edges[edgeIdx].start);
    int v = index_.at(db_.edges[edgeIdx].end);
    return reversed_[edgeIdx] ? std::make_pair(v, u) : std::make_pair(u, v);
  }

  // ── phase 1: longest-path ranking ─────────────────────────────────────
  void assign_ranks() {
    std::vector<std::vector<int>> succ(real_count_);
    std::vector<int> indeg(real_count_, 0);
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      const auto &e = db_.edges[i];
      if (!index_.count(e.start) || !index_.count(e.end)) continue;
      if (index_[e.start] == index_[e.end]) continue;
      auto [u, v] = dag_dir(i);
      succ[u].push_back(v);
      indeg[v]++;
    }
    std::vector<int> rank(real_count_, 0), q;
    for (int i = 0; i < real_count_; ++i)
      if (indeg[i] == 0) q.push_back(i);
    for (size_t h = 0; h < q.size(); ++h) { // Kahn topo order
      int u = q[h];
      for (int v : succ[u]) {
        rank[v] = std::max(rank[v], rank[u] + 1);
        if (--indeg[v] == 0) q.push_back(v);
      }
    }
    for (int i = 0; i < real_count_; ++i) cells_[i].rank = rank[i];
    max_rank_ = 0;
    for (int r : rank) max_rank_ = std::max(max_rank_, r);
  }

  // ── build layers, insert dummies, record routing chains ───────────────
  void build_layers_and_chains() {
    ranks_.assign(max_rank_ + 1, {});
    for (int i = 0; i < real_count_; ++i) ranks_[cells_[i].rank].push_back(i);

    chains_.assign(db_.edges.size(), {});
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      const auto &e = db_.edges[i];
      if (!index_.count(e.start) || !index_.count(e.end)) continue;
      int a = index_[e.start], b = index_[e.end];
      if (a == b) continue; // self-loop: not drawn in v1
      int ra = cells_[a].rank, rb = cells_[b].rank;
      // Chain of cells in ORIGINAL direction start..end; dummies fill the gap.
      std::vector<int> chain{a};
      if (std::abs(ra - rb) > 1) {
        int step = ra < rb ? 1 : -1;
        for (int r = ra + step; r != rb; r += step) {
          Cell d;
          d.dummy = true;
          d.rank = r;
          d.id = "__d" + std::to_string(cells_.size());
          int di = static_cast<int>(cells_.size());
          cells_.push_back(std::move(d));
          ranks_[r].push_back(di);
          chain.push_back(di);
        }
      }
      chain.push_back(b);
      // Register adjacency between consecutive chain cells (adjacent ranks).
      for (size_t k = 0; k + 1 < chain.size(); ++k) {
        int x = chain[k], y = chain[k + 1];
        if (cells_[x].rank < cells_[y].rank) { cells_[x].down.push_back(y); cells_[y].up.push_back(x); }
        else { cells_[x].up.push_back(y); cells_[y].down.push_back(x); }
      }
      chains_[i] = std::move(chain);
    }
  }

  // ── phase 2: barycenter ordering ──────────────────────────────────────
  void order_ranks() {
    auto pos = [&](std::vector<int> &members) {
      std::unordered_map<int, int> p;
      for (int k = 0; k < (int)members.size(); ++k) p[members[k]] = k;
      return p;
    };
    auto bary = [&](int cell, std::unordered_map<int, int> &p,
                    const std::vector<int> &neigh) -> double {
      if (neigh.empty()) return -1; // keep in place
      double sum = 0;
      for (int n : neigh) sum += p[n];
      return sum / neigh.size();
    };

    for (int iter = 0; iter < 4; ++iter) {
      // down sweep: order rank r by parents in r-1
      for (int r = 1; r <= max_rank_; ++r) {
        auto p = pos(ranks_[r - 1]);
        sort_rank(ranks_[r], [&](int c) { return bary(c, p, cells_[c].up); });
      }
      // up sweep: order rank r by children in r+1
      for (int r = max_rank_ - 1; r >= 0; --r) {
        auto p = pos(ranks_[r + 1]);
        sort_rank(ranks_[r], [&](int c) { return bary(c, p, cells_[c].down); });
      }
    }
  }

  template <class F> void sort_rank(std::vector<int> &members, F key) {
    std::vector<double> b(members.size());
    for (size_t k = 0; k < members.size(); ++k) b[k] = key(members[k]);
    // Stable sort keeping cells with no neighbours (b<0) at their current index.
    std::vector<int> order(members.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = (int)k;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
      double bx = b[x] < 0 ? x : b[x], by = b[y] < 0 ? y : b[y];
      return bx < by;
    });
    std::vector<int> res(members.size());
    for (size_t k = 0; k < order.size(); ++k) res[k] = members[order[k]];
    members = std::move(res);
  }

  // ── phase 3: coordinate assignment ────────────────────────────────────
  bool rank_is_vertical() const {
    return db_.direction == Direction::TB || db_.direction == Direction::BT;
  }
  double order_extent(const Cell &c) const {
    if (c.dummy) return 8;
    return rank_is_vertical() ? c.w : c.h;
  }
  double rank_extent(const Cell &c) const {
    if (c.dummy) return 0;
    return rank_is_vertical() ? c.h : c.w;
  }

  void assign_coords() {
    // Pack the order axis within each rank; track each rank's width.
    std::vector<double> rank_width(max_rank_ + 1, 0);
    double max_width = 0;
    for (int r = 0; r <= max_rank_; ++r) {
      double cur = 0;
      for (int c : ranks_[r]) {
        double e = order_extent(cells_[c]);
        cells_[c].a = cur + e / 2;
        cur += e + opt_.node_sep;
      }
      rank_width[r] = ranks_[r].empty() ? 0 : cur - opt_.node_sep;
      max_width = std::max(max_width, rank_width[r]);
    }
    // Center each rank along the order axis.
    for (int r = 0; r <= max_rank_; ++r) {
      double shift = (max_width - rank_width[r]) / 2;
      for (int c : ranks_[r]) cells_[c].a += shift;
    }
    // Pack the rank axis: successive ranks separated by rank_sep.
    std::vector<double> rank_b(max_rank_ + 1, 0);
    double cur = 0;
    for (int r = 0; r <= max_rank_; ++r) {
      double ext = 0;
      for (int c : ranks_[r]) ext = std::max(ext, rank_extent(cells_[c]));
      rank_b[r] = cur + ext / 2;
      cur += ext + opt_.rank_sep;
      for (int c : ranks_[r]) cells_[c].b = rank_b[r];
    }
    total_b_ = cur - opt_.rank_sep;
  }

  Point to_xy(const Cell &c) const {
    switch (db_.direction) {
    case Direction::TB: return {c.a, c.b};
    case Direction::BT: return {c.a, total_b_ - c.b};
    case Direction::LR: return {c.b, c.a};
    case Direction::RL: return {total_b_ - c.b, c.a};
    }
    return {c.a, c.b};
  }

  // ── phase 4: routing + emit ───────────────────────────────────────────
  // Clip a segment from a node centre to its bounding-box border toward `t`.
  static Point clip(Point center, double w, double h, Point t) {
    double dx = t.x - center.x, dy = t.y - center.y;
    if (dx == 0 && dy == 0) return center;
    double tx = dx != 0 ? (w / 2) / std::abs(dx) : std::numeric_limits<double>::infinity();
    double ty = dy != 0 ? (h / 2) / std::abs(dy) : std::numeric_limits<double>::infinity();
    double s = std::min({tx, ty, 1.0});
    return {center.x + dx * s, center.y + dy * s};
  }

  Layout emit() {
    for (Cell &c : cells_) {
      Point p = to_xy(c);
      c.a = p.x; // reuse a/b to stash absolute x/y
      c.b = p.y;
    }
    // Bounding box (real nodes only) to normalize origin + compute viewBox.
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (int i = 0; i < real_count_; ++i) {
      Cell &c = cells_[i];
      minx = std::min(minx, c.a - c.w / 2);
      miny = std::min(miny, c.b - c.h / 2);
      maxx = std::max(maxx, c.a + c.w / 2);
      maxy = std::max(maxy, c.b + c.h / 2);
    }
    if (real_count_ == 0) { minx = miny = 0; maxx = maxy = 0; }
    double ox = opt_.margin - minx, oy = opt_.margin - miny;

    Layout out;
    out.diagram = {maxx - minx + 2 * opt_.margin, maxy - miny + 2 * opt_.margin};

    for (int i = 0; i < real_count_; ++i) {
      const Cell &c = cells_[i];
      const Vertex &v = db_.vertices[i];
      LaidNode n;
      n.id = v.id;
      n.label = v.label.empty() ? v.id : v.label;
      n.shape = v.shape;
      n.center = {c.a + ox, c.b + oy};
      n.size = {c.w, c.h};
      n.classes = v.classes;
      out.nodes.push_back(std::move(n));
    }

    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      if (chains_[i].empty()) continue; // self-loop / dangling
      const Edge &e = db_.edges[i];
      std::vector<Point> pts;
      for (int c : chains_[i]) pts.push_back({cells_[c].a + ox, cells_[c].b + oy});
      // Clip first/last endpoints to the node borders.
      const Cell &sc = cells_[chains_[i].front()];
      const Cell &ec = cells_[chains_[i].back()];
      pts.front() = clip({sc.a + ox, sc.b + oy}, sc.w, sc.h, pts[1]);
      pts.back() = clip({ec.a + ox, ec.b + oy}, ec.w, ec.h, pts[pts.size() - 2]);

      LaidEdge le;
      le.start = e.start;
      le.end = e.end;
      le.label = e.label;
      le.stroke = e.stroke;
      le.head_end = e.head_end;
      le.head_start = e.head_start;
      le.points = pts;
      size_t mid = pts.size() / 2;
      le.label_pos = pts.size() % 2 ? pts[mid]
                                    : Point{(pts[mid - 1].x + pts[mid].x) / 2,
                                            (pts[mid - 1].y + pts[mid].y) / 2};
      out.edges.push_back(std::move(le));
    }
    return out;
  }

  const FlowDb &db_;
  TextMeasurer &meas_;
  LayoutOptions opt_;

  std::vector<Cell> cells_;
  std::unordered_map<std::string, int> index_; // real node id -> cell index
  int real_count_ = 0;
  int max_rank_ = 0;
  double total_b_ = 0;
  std::vector<bool> reversed_;
  std::vector<std::vector<int>> ranks_;   // cell indices per rank
  std::vector<std::vector<int>> chains_;  // per db edge: cell chain start..end
};

} // namespace

Layout layout(const FlowDb &db, TextMeasurer &measurer, const LayoutOptions &opts) {
  if (opts.collapse_subgraphs) {
    FlowDb g = collapse_subgraphs(db);
    return Layouter(g, measurer, opts).run();
  }
  return Layouter(db, measurer, opts).run();
}

} // namespace mermaid
