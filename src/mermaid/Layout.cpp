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
  if (db.subgraphs.empty())
    return db;

  std::set<std::string> sg_ids;
  std::map<std::string, std::string> sg_label;
  for (const Subgraph &s : db.subgraphs) {
    sg_ids.insert(s.id);
    sg_label[s.id] = s.label;
  }

  // Direct owner of each member vertex, and parent of each nested subgraph.
  std::map<std::string, std::string> owner; // vertex id -> subgraph id
  std::map<std::string, std::string>
      parent; // subgraph id -> parent subgraph id
  for (const Subgraph &s : db.subgraphs)
    for (const std::string &n : s.nodes) {
      if (sg_ids.count(n))
        parent[n] = s.id; // nested subgraph
      else
        owner[n] = s.id; // member vertex
    }

  auto outermost = [&](std::string sg) {
    while (parent.count(sg))
      sg = parent[sg];
    return sg;
  };
  // Representative node id for any vertex after collapse.
  auto rep = [&](const std::string &v) -> std::string {
    if (owner.count(v))
      return outermost(owner[v]); // member -> outermost cluster
    if (sg_ids.count(v))
      return outermost(v); // a subgraph used as a node
    return v;              // free vertex
  };

  FlowDb out;
  out.direction = db.direction;
  out.title = db.title;
  out.classes = db.classes;

  // Vertices: emit each representative once, in first-appearance order.
  std::unordered_map<std::string, int> idx;
  auto ensure = [&](const std::string &id) -> Vertex & {
    auto it = idx.find(id);
    if (it != idx.end())
      return out.vertices[it->second];
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
    if (r == v.id)
      nv = v; // keep the real vertex's label/shape/classes/styles
  }

  // Edges: redirect endpoints, drop self-loops, dedup.
  std::set<std::string> seen;
  for (const Edge &e : db.edges) {
    Edge ne = e;
    ne.start = rep(e.start);
    ne.end = rep(e.end);
    if (ne.start == ne.end)
      continue; // internal to a collapsed subgraph
    std::string key = ne.start + '\x01' + ne.end + '\x01' + ne.label + '\x01' +
                      std::to_string((int)ne.stroke) +
                      std::to_string((int)ne.head_end) +
                      std::to_string(ne.head_start) + std::to_string(ne.length);
    if (!seen.insert(key).second)
      continue;
    out.edges.push_back(std::move(ne));
  }
  return out;
}

std::vector<Point> shape_outline(ShapeKind shape, double w, double h) {
  const double hw = w / 2, hh = h / 2;
  const double lean = hh;       // leans/trapezoids slant by h/2
  const double hexlean = hh / 2; // hexagon ends are h/4 deep
  switch (shape) {
  case ShapeKind::Rhombus:
    return {{0, -hh}, {hw, 0}, {0, hh}, {-hw, 0}};
  case ShapeKind::Hexagon:
    return {{-hw + hexlean, -hh}, {hw - hexlean, -hh}, {hw, 0},
            {hw - hexlean, hh},   {-hw + hexlean, hh}, {-hw, 0}};
  case ShapeKind::Asymmetric:
    return {{-hw, hh}, {hw, hh}, {hw, -hh}, {-hw, -hh}, {-hw + hh, 0}};
  case ShapeKind::LeanRight:
    return {{-hw + lean, -hh}, {hw, -hh}, {hw - lean, hh}, {-hw, hh}};
  case ShapeKind::LeanLeft:
    return {{-hw, -hh}, {hw - lean, -hh}, {hw, hh}, {-hw + lean, hh}};
  case ShapeKind::Trapezoid:
    return {{-hw + lean, -hh}, {hw - lean, -hh}, {hw, hh}, {-hw, hh}};
  case ShapeKind::TrapezoidAlt:
    return {{-hw, -hh}, {hw, -hh}, {hw - lean, hh}, {-hw + lean, hh}};
  case ShapeKind::Circle:
  case ShapeKind::DoubleCircle: {
    std::vector<Point> p;
    const double r = std::max(hw, hh);
    const int n = 32;
    for (int i = 0; i < n; ++i) {
      const double a = 2 * 3.14159265358979323846 * i / n;
      p.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return p;
  }
  default: // Rect, RoundEdges, Stadium, Subroutine, Cylinder -> their box
    return {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
  }
}

namespace {

// A layout cell: a real node or a routing dummy. Coordinates are computed in
// direction-agnostic axes `a` (order axis) and `b` (rank axis), then mapped to
// (x,y) by the diagram direction.
struct Cell {
  std::string id;
  bool dummy = false;
  bool is_label = false; // a dummy holding an edge label: reserves space like a
                         // node, but is never drawn and never a waypoint
  int rank = 0;
  double w = 0, h = 0; // node size, or label size for a label dummy (else 0)
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
    build_layers_and_chains(); // inserts dummies, records per-edge routing
                               // chains
    order_ranks();
    assign_coords();
    align_coords();
    separate_components();
    return emit();
  }

private:
  // ── phase 0a: nodes + sizes ───────────────────────────────────────────
  // Every label in the diagram — node labels AND edge labels — is measured in a
  // SINGLE batch call, so the browser measurer needs only one JS round-trip.
  void build_nodes() {
    const size_t nv = db_.vertices.size();
    std::vector<std::string> labels;
    labels.reserve(nv + db_.edges.size());
    for (const Vertex &v : db_.vertices)
      labels.push_back(v.label.empty() ? v.id : v.label);
    for (const Edge &e : db_.edges)
      labels.push_back(e.label); // may be empty; zeroed out below

    const std::vector<LabelBox> boxes = meas_.measure(labels, opt_.font);

    // [0, nv) -> node labels
    for (size_t i = 0; i < nv; ++i) {
      Cell c;
      c.id = db_.vertices[i].id;
      Size s = shape_pad(db_.vertices[i].shape, boxes[i]);
      c.w = s.w;
      c.h = s.h;
      index_[c.id] = static_cast<int>(cells_.size());
      cells_.push_back(std::move(c));
    }
    real_count_ = static_cast<int>(cells_.size());

    // [nv, nv+ne) -> edge labels (padded box the layout will reserve space for)
    edge_label_.assign(db_.edges.size(), Size{0, 0});
    for (size_t i = 0; i < db_.edges.size(); ++i) {
      if (db_.edges[i].label.empty())
        continue; // unlabelled -> no reservation
      const LabelBox &b = boxes[nv + i];
      edge_label_[i] = {b.w + 2 * kLabelPadX, b.h + 2 * kLabelPadY};
    }
  }

  static Size shape_pad(ShapeKind shape, const LabelBox &t) {
    // mermaid's base node padding is 15/side (goldens: dH=+30 on every shape).
    // Its square ([text]) is the odd one out, padding 30/side horizontally
    // (dW=+60) — which is a big part of why a plain box must not look dwarfed by
    // a rhombus next to it.
    const double px = 15, py = 15;
    double w = t.w + 2 * px, h = t.h + 2 * py;
    switch (shape) {
    case ShapeKind::Rect:
      w = t.w + 4 * px; // square: +60 total, matching mermaid
      break;
    case ShapeKind::Rhombus: {
      // A rhombus inscribing a w*h text box needs w/W + h/H <= 1, so a box the
      // size of the text plus padding does NOT fit — the text spills past the
      // angled sides. mermaid uses a square rotated 45 degrees whose side is the
      // padded text width plus the text height; match that.
      const double s = t.w + 2 * px + t.h;
      w = h = s;
      break;
    }
    case ShapeKind::Hexagon:
    case ShapeKind::Asymmetric:
    case ShapeKind::LeanRight:
    case ShapeKind::LeanLeft:
    case ShapeKind::Trapezoid:
    case ShapeKind::TrapezoidAlt:
      // All of these eat into the box: the hexagon's angled ends, the odd
      // shape's notch, and the slanted sides of the leans/trapezoids all shift
      // by h/2 in total (mermaid's rule). Widen so the text still fits.
      w = t.w + 2 * px + h / 2;
      break;
    case ShapeKind::Cylinder:
      // Grow so the text sits BETWEEN the two cap ellipses instead of colliding
      // with them.
      h = t.h + 2 * py + 2 * cylinder_ry(w);
      break;
    case ShapeKind::Circle:
    case ShapeKind::DoubleCircle: {
      // A circle must CIRCUMSCRIBE the text box, so its radius follows the box's
      // half-diagonal (max(w,h) both overshot for wide text and could clip tall
      // text).
      const double r = std::hypot(t.w / 2, t.h / 2) + px;
      w = h = 2 * r;
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
    std::vector<std::vector<std::pair<int, int>>> adj(
        real_count_); // (edgeIdx, to)
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      int u =
          index_.count(db_.edges[i].start) ? index_[db_.edges[i].start] : -1;
      int v = index_.count(db_.edges[i].end) ? index_[db_.edges[i].end] : -1;
      if (u < 0 || v < 0 || u == v)
        continue;
      adj[u].push_back({i, v});
    }
    reversed_.assign(db_.edges.size(), false);
    std::vector<int> state(real_count_, 0); // 0 white, 1 gray, 2 black
    // Iterative DFS to avoid deep recursion.
    for (int s = 0; s < real_count_; ++s) {
      if (state[s])
        continue;
      std::vector<std::pair<int, size_t>> stk{{s, 0}};
      state[s] = 1;
      while (!stk.empty()) {
        auto &[u, k] = stk.back();
        if (k < adj[u].size()) {
          auto [ei, v] = adj[u][k++];
          if (state[v] == 1)
            reversed_[ei] = true; // back edge -> reverse
          else if (state[v] == 0) {
            state[v] = 1;
            stk.push_back({v, 0});
          }
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
  // mermaid's edge `length` is dagre's minlen: `A ---> C` (length 2) puts C two
  // ranks below A. Every minlen is doubled so each edge has an intermediate rank
  // that can hold its label block (see build_layers_and_chains).
  void assign_ranks() {
    std::vector<std::vector<std::pair<int, int>>> succ(
        real_count_); // (v, minlen)
    std::vector<int> indeg(real_count_, 0);
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      const auto &e = db_.edges[i];
      if (!index_.count(e.start) || !index_.count(e.end))
        continue;
      if (index_[e.start] == index_[e.end])
        continue;
      auto [u, v] = dag_dir(i);
      succ[u].push_back({v, std::max(1, e.length) * 2});
      indeg[v]++;
    }
    std::vector<int> rank(real_count_, 0), q;
    for (int i = 0; i < real_count_; ++i)
      if (indeg[i] == 0)
        q.push_back(i);
    for (size_t h = 0; h < q.size(); ++h) { // Kahn topo order
      int u = q[h];
      for (auto [v, minlen] : succ[u]) {
        rank[v] = std::max(rank[v], rank[u] + minlen);
        if (--indeg[v] == 0)
          q.push_back(v);
      }
    }
    for (int i = 0; i < real_count_; ++i)
      cells_[i].rank = rank[i];
    max_rank_ = 0;
    for (int r : rank)
      max_rank_ = std::max(max_rank_, r);
  }

  // ── build layers, insert dummies, record routing chains ───────────────
  void build_layers_and_chains() {
    ranks_.assign(max_rank_ + 1, {});
    for (int i = 0; i < real_count_; ++i)
      ranks_[cells_[i].rank].push_back(i);

    chains_.assign(db_.edges.size(), {});
    label_cell_.assign(db_.edges.size(), -1);
    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      const auto &e = db_.edges[i];
      if (!index_.count(e.start) || !index_.count(e.end))
        continue;
      int a = index_[e.start], b = index_[e.end];
      if (a == b)
        continue; // self-loop: not drawn in v1
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

      // Give the label a real cell in the middle of the chain, sized to the
      // measured label. It reserves space through the same rank/order extent
      // machinery as a node — in BOTH axes — so a long label can't overlap the
      // diagram. It is never drawn as a shape and never used as a waypoint.
      if (edge_label_[i].w > 0 && chain.size() >= 3) {
        int mid = chain[chain.size() / 2]; // interior => always a dummy
        cells_[mid].is_label = true;
        cells_[mid].w = edge_label_[i].w;
        cells_[mid].h = edge_label_[i].h;
        label_cell_[i] = mid;
      }

      // Register adjacency between consecutive chain cells (adjacent ranks).
      for (size_t k = 0; k + 1 < chain.size(); ++k) {
        int x = chain[k], y = chain[k + 1];
        if (cells_[x].rank < cells_[y].rank) {
          cells_[x].down.push_back(y);
          cells_[y].up.push_back(x);
        } else {
          cells_[x].up.push_back(y);
          cells_[y].down.push_back(x);
        }
      }
      chains_[i] = std::move(chain);
    }
  }

  // ── phase 2: barycenter ordering ──────────────────────────────────────
  void order_ranks() {
    auto pos = [&](std::vector<int> &members) {
      std::unordered_map<int, int> p;
      for (int k = 0; k < (int)members.size(); ++k)
        p[members[k]] = k;
      return p;
    };
    auto bary = [&](int cell, std::unordered_map<int, int> &p,
                    const std::vector<int> &neigh) -> double {
      if (neigh.empty())
        return -1; // keep in place
      double sum = 0;
      for (int n : neigh)
        sum += p[n];
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
    for (size_t k = 0; k < members.size(); ++k)
      b[k] = key(members[k]);
    // Stable sort keeping cells with no neighbours (b<0) at their current
    // index.
    std::vector<int> order(members.size());
    for (size_t k = 0; k < order.size(); ++k)
      order[k] = (int)k;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
      double bx = b[x] < 0 ? x : b[x], by = b[y] < 0 ? y : b[y];
      return bx < by;
    });
    std::vector<int> res(members.size());
    for (size_t k = 0; k < order.size(); ++k)
      res[k] = members[order[k]];
    members = std::move(res);
  }

  // ── phase 3: coordinate assignment ────────────────────────────────────
  bool rank_is_vertical() const {
    return db_.direction == Direction::TB || db_.direction == Direction::BT;
  }
  // Label blocks are sized like nodes so the layout reserves room for them in
  // both axes; plain routing dummies are just a thin lane on the order axis and
  // take no space along the rank axis.
  double order_extent(const Cell &c) const {
    if (c.dummy && !c.is_label)
      return 8;
    return rank_is_vertical() ? c.w : c.h;
  }
  double rank_extent(const Cell &c) const {
    if (c.dummy && !c.is_label)
      return 0;
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
      for (int c : ranks_[r])
        cells_[c].a += shift;
    }
    // Pack the rank axis.
    std::vector<double> rank_b(max_rank_ + 1, 0);
    double cur = 0;
    for (int r = 0; r <= max_rank_; ++r) {
      double ext = 0;
      for (int c : ranks_[r])
        ext = std::max(ext, rank_extent(cells_[c]));
      rank_b[r] = cur + ext / 2;
      for (int c : ranks_[r])
        cells_[c].b = rank_b[r];
      cur += ext;
      if (r < max_rank_)
        cur += opt_.rank_sep;
    }
    total_b_ = cur;
  }

  // ── phase 3b: coordinate alignment ────────────────────────────────────
  // Packing alone puts cells in the same slot of different ranks at DIFFERENT
  // coordinates: each rank is packed from its own extents and centred
  // independently, so a rank of 23px label dummies and a rank of 35px nodes
  // drift apart. Edges then bend (visibly so through label dummies, which the
  // polyline routes through). Fix: pull each cell toward the mean of its
  // neighbours in the adjacent rank, preserving order and node_sep.
  void align_coords() {
    for (int iter = 0; iter < 8; ++iter) {
      const bool down = (iter % 2 == 0);
      if (down)
        for (int r = 1; r <= max_rank_; ++r)
          relax_rank(r);
      else
        for (int r = max_rank_ - 1; r >= 0; --r)
          relax_rank(r);
    }
  }

  double gap_between(int c1, int c2) const {
    return (order_extent(cells_[c1]) + order_extent(cells_[c2])) / 2 +
           opt_.node_sep;
  }

  void relax_rank(int r) {
    std::vector<int> &m = ranks_[r];
    const int n = static_cast<int>(m.size());
    if (n == 0)
      return;

    std::vector<double> desired(n);
    for (int i = 0; i < n; ++i) {
      const Cell &c = cells_[m[i]];
      // Average BOTH sides. Using only the sweep's side pins every cell onto
      // its down-neighbour (the last sweep wins), which parks each routing
      // dummy directly on the node below it and leaves an L-kink in the edge. A
      // cell — a routing dummy especially — belongs on the line *between* its
      // neighbours, so the corridor is straight unless constraints bend it.
      double sum = 0;
      int cnt = 0;
      for (int nb : c.up) {
        sum += cells_[nb].a;
        ++cnt;
      }
      for (int nb : c.down) {
        sum += cells_[nb].a;
        ++cnt;
      }
      desired[i] = cnt ? sum / cnt : c.a; // unconstrained: stay put
    }

    // Build two feasible solutions — one biased left, one biased right. The gap
    // constraints are linear, so their average is feasible too, and it equals
    // `desired` exactly when `desired` already satisfies every gap.
    std::vector<double> lo(n), hi(n);
    lo[0] = desired[0];
    for (int i = 1; i < n; ++i)
      lo[i] = std::max(desired[i], lo[i - 1] + gap_between(m[i - 1], m[i]));
    hi[n - 1] = desired[n - 1];
    for (int i = n - 2; i >= 0; --i)
      hi[i] = std::min(desired[i], hi[i + 1] - gap_between(m[i], m[i + 1]));

    for (int i = 0; i < n; ++i)
      cells_[m[i]].a = (lo[i] + hi[i]) / 2;
  }

  // ── phase 3c: component separation ────────────────────────────────────
  // Separation is only enforced BETWEEN CELLS OF THE SAME RANK, so nothing
  // stops a deep cell of one component from sliding into another component's
  // column (e.g. J at rank 4 landing left of F at rank 2, which has no
  // constraint tying them). Disconnected components must own disjoint bands on
  // the order axis — that is what mermaid/dagre produces. Pack each component
  // into its own band, preserving the relative geometry the earlier phases
  // computed.
  void separate_components() {
    // Connected components over the UNDIRECTED graph of real nodes.
    std::vector<std::vector<int>> adj(real_count_);
    for (const Edge &e : db_.edges) {
      auto a = index_.find(e.start), b = index_.find(e.end);
      if (a == index_.end() || b == index_.end() || a->second == b->second)
        continue;
      adj[a->second].push_back(b->second);
      adj[b->second].push_back(a->second);
    }
    std::vector<int> comp(cells_.size(), -1);
    int ncomp = 0;
    for (int i = 0; i < real_count_; ++i) {
      if (comp[i] != -1)
        continue;
      std::vector<int> stk{i};
      comp[i] = ncomp;
      while (!stk.empty()) {
        int u = stk.back();
        stk.pop_back();
        for (int v : adj[u])
          if (comp[v] == -1) {
            comp[v] = ncomp;
            stk.push_back(v);
          }
      }
      ++ncomp;
    }
    if (ncomp <= 1)
      return; // single component: nothing to separate

    // A routing/label dummy belongs to its edge's component.
    for (size_t i = 0; i < chains_.size(); ++i) {
      if (chains_[i].empty())
        continue;
      const int c = comp[chains_[i].front()];
      for (int cell : chains_[i])
        comp[cell] = c;
    }

    // Current extent of each component along the order axis.
    std::vector<double> lo(ncomp, 1e18), hi(ncomp, -1e18);
    for (size_t i = 0; i < cells_.size(); ++i) {
      if (comp[i] < 0)
        continue;
      const double half = order_extent(cells_[i]) / 2;
      lo[comp[i]] = std::min(lo[comp[i]], cells_[i].a - half);
      hi[comp[i]] = std::max(hi[comp[i]], cells_[i].a + half);
    }

    // Pack the bands back-to-back, keeping the order the layout already chose.
    std::vector<int> order(ncomp);
    for (int i = 0; i < ncomp; ++i)
      order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](int x, int y) { return lo[x] < lo[y]; });

    std::vector<double> shift(ncomp, 0);
    double cursor = 0;
    for (int c : order) {
      if (lo[c] > hi[c])
        continue; // empty component
      shift[c] = cursor - lo[c];
      cursor += (hi[c] - lo[c]) + opt_.node_sep;
    }
    for (size_t i = 0; i < cells_.size(); ++i)
      if (comp[i] >= 0)
        cells_[i].a += shift[comp[i]];
  }

  Point to_xy(const Cell &c) const {
    switch (db_.direction) {
    case Direction::TB:
      return {c.a, c.b};
    case Direction::BT:
      return {c.a, total_b_ - c.b};
    case Direction::LR:
      return {c.b, c.a};
    case Direction::RL:
      return {total_b_ - c.b, c.a};
    }
    return {c.a, c.b};
  }

  // ── phase 4: routing + emit ───────────────────────────────────────────
  // Where the ray from a node's centre toward `t` crosses the node's OUTLINE.
  // Clipping to the bounding box instead would leave arrows floating in the
  // empty corners of shapes like the rhombus.
  static Point clip(Point center, ShapeKind shape, double w, double h, Point t) {
    const double dx = t.x - center.x, dy = t.y - center.y;
    if (dx == 0 && dy == 0)
      return center;
    const auto poly = shape_outline(shape, w, h);
    auto cross = [](double ax, double ay, double bx, double by) {
      return ax * by - ay * bx;
    };
    double best = 1.0; // never overshoot the target
    bool hit = false;
    for (size_t i = 0; i < poly.size(); ++i) {
      const Point a = poly[i], b = poly[(i + 1) % poly.size()];
      const double ex = b.x - a.x, ey = b.y - a.y;
      const double den = cross(dx, dy, ex, ey);
      if (std::abs(den) < 1e-9)
        continue; // parallel
      const double tt = cross(a.x, a.y, ex, ey) / den; // along the ray
      const double u = cross(a.x, a.y, dx, dy) / den;  // along the edge
      if (tt > 0 && tt <= 1.0 && u >= 0 && u <= 1) {
        if (!hit || tt < best) {
          best = tt;
          hit = true;
        }
      }
    }
    return {center.x + dx * best, center.y + dy * best};
  }

  // Perpendicular distance from p to the infinite line a-b.
  static double perp_dist(Point p, Point a, Point b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len == 0)
      return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs(dx * (a.y - p.y) - (a.x - p.x) * dy) / len;
  }

  Layout emit() {
    for (Cell &c : cells_) {
      Point p = to_xy(c);
      c.a = p.x; // reuse a/b to stash absolute x/y
      c.b = p.y;
    }

    Layout out;

    // Build the geometry in un-normalised coordinates first. The diagram bounds
    // depend on the edges (a routed back-edge corridor swings outside the nodes)
    // and on the edge labels, so they can only be measured once those exist.
    for (int i = 0; i < real_count_; ++i) {
      const Cell &c = cells_[i];
      const Vertex &v = db_.vertices[i];
      LaidNode n;
      n.id = v.id;
      n.label = v.label.empty() ? v.id : v.label;
      n.shape = v.shape;
      n.center = {c.a, c.b};
      n.size = {c.w, c.h};
      n.classes = v.classes;
      out.nodes.push_back(std::move(n));
    }

    for (int i = 0; i < (int)db_.edges.size(); ++i) {
      if (chains_[i].empty())
        continue; // self-loop / dangling
      const Edge &e = db_.edges[i];
      // Waypoints: the endpoints, the FIRST and LAST routing dummy, and any
      // label block. An edge spanning many ranks therefore leaves its source and
      // enters its target through the reserved corridor, but runs dead straight
      // in between instead of tracing every intermediate lane. The label block is
      // always kept: it is the spot the layout reserved for the label, so
      // following it is what keeps labels on the line and out of the nodes (and
      // what separates parallel edges). For a normal span-2 edge the single
      // dummy is both first and last, so nothing changes.
      const auto &chain = chains_[i];
      std::vector<Point> pts;
      for (size_t k = 0; k < chain.size(); ++k) {
        const Cell &cc = cells_[chain[k]];
        const bool endpoint = (k == 0 || k + 1 == chain.size());
        const bool first_dummy = (k == 1);
        const bool last_dummy = (k + 2 == chain.size());
        if (endpoint || first_dummy || last_dummy || cc.is_label)
          pts.push_back({cc.a, cc.b});
      }

      const Cell &sc = cells_[chains_[i].front()];
      const Cell &ec = cells_[chains_[i].back()];
      const Point p0{sc.a, sc.b}, p1{ec.a, ec.b};
      // Clip the endpoints to the real node borders (chain ends are always real
      // nodes, so their index lines up with db_.vertices).
      pts.front() = clip(p0, db_.vertices[chains_[i].front()].shape, sc.w, sc.h,
                         pts[1]);
      pts.back() = clip(p1, db_.vertices[chains_[i].back()].shape, ec.w, ec.h,
                        pts[pts.size() - 2]);

      LaidEdge le;
      le.start = e.start;
      le.end = e.end;
      le.label = e.label;
      le.stroke = e.stroke;
      le.head_end = e.head_end;
      le.head_start = e.head_start;
      le.points = pts;
      le.label_size = edge_label_[i];
      // The label block is itself a waypoint now, so drawing at the block puts
      // the label on the line AND on the space reserved for it.
      if (label_cell_[i] >= 0) {
        le.label_pos = {cells_[label_cell_[i]].a, cells_[label_cell_[i]].b};
      } else {
        const size_t mid = pts.size() / 2;
        le.label_pos = pts.size() % 2
                           ? pts[mid]
                           : Point{(pts[mid - 1].x + pts[mid].x) / 2,
                                   (pts[mid - 1].y + pts[mid].y) / 2};
      }
      out.edges.push_back(std::move(le));
    }

    // Bounds over EVERYTHING that gets drawn: node boxes, every edge waypoint
    // (back-edge corridors reach outside the nodes) and every edge label box —
    // otherwise a label hanging off the side is clipped by the viewBox.
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    auto ext_box = [&](Point c, Size s) {
      minx = std::min(minx, c.x - s.w / 2);
      miny = std::min(miny, c.y - s.h / 2);
      maxx = std::max(maxx, c.x + s.w / 2);
      maxy = std::max(maxy, c.y + s.h / 2);
    };
    for (const LaidNode &n : out.nodes)
      ext_box(n.center, n.size);
    for (const LaidEdge &e : out.edges) {
      for (const Point &p : e.points)
        ext_box(p, {0, 0});
      if (e.label_size.w > 0)
        ext_box(e.label_pos, e.label_size);
    }
    if (out.nodes.empty() && out.edges.empty()) {
      minx = miny = 0;
      maxx = maxy = 0;
    }

    // Normalise the origin to the margin and size the diagram.
    const double ox = opt_.margin - minx, oy = opt_.margin - miny;
    out.diagram = {maxx - minx + 2 * opt_.margin, maxy - miny + 2 * opt_.margin};
    for (LaidNode &n : out.nodes) {
      n.center.x += ox;
      n.center.y += oy;
    }
    for (LaidEdge &e : out.edges) {
      for (Point &p : e.points) {
        p.x += ox;
        p.y += oy;
      }
      e.label_pos.x += ox;
      e.label_pos.y += oy;
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
  std::vector<std::vector<int>> ranks_;  // cell indices per rank
  std::vector<std::vector<int>> chains_; // per db edge: cell chain start..end
  std::vector<Size> edge_label_; // per db edge: padded label box ({0,0} = none)
  std::vector<int> label_cell_;  // per db edge: label block cell, or -1

  static constexpr double kLabelPadX = 4; // padding inside the label's own box
  static constexpr double kLabelPadY = 2;
  static constexpr double kLabelMargin =
      22; // clear edge either side of a label
};

} // namespace

Layout layout(const FlowDb &db, TextMeasurer &measurer,
              const LayoutOptions &opts) {
  if (opts.collapse_subgraphs) {
    FlowDb g = collapse_subgraphs(db);
    return Layouter(g, measurer, opts).run();
  }
  return Layouter(db, measurer, opts).run();
}

} // namespace mermaid
