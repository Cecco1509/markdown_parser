# Mermaid flowchart rendering — layout & SVG plan

The rendering stage turns a `FlowDb` (the verified output of `lower()`) into an
SVG string. This document is the plan the implementation follows; keep it in sync
with the code.

## Pipeline

```
FlowDb ─▶ collapse_subgraphs ─▶ measure labels ─▶ layout (phases 0–4) ─▶ render_svg ─▶ SVG
                (v1 only)          (TextMeasurer)      (Layout IR)
```

- **`collapse_subgraphs(FlowDb) → FlowDb`** — *v1 simplification*: we do NOT do
  cluster layout yet. Each subgraph is collapsed into a single node (see below).
- **`measure(labels) → sizes`** — batch text measurement via a `TextMeasurer`.
- **`layout(FlowDb, TextMeasurer) → Layout`** — the hand-written Sugiyama passes.
- **`render_svg(Layout) → std::string`** — pure geometry → SVG, mermaid classes.

We build **CLI-first**: a `mermaid_svg` executable (sibling of `mermaid_ast`)
runs the whole pipeline with `ApproxMeasurer`, so the engine is testable before
touching WASM or the markdown handler.

## Design decisions (and why)

| Decision | Choice | Why |
|---|---|---|
| Layout library | **Hand-written minimal Sugiyama** | No external dependencies (same principle as the hand-written LR generator). dagre/Graphviz/OGDF add deps or promise exact-match fidelity we don't need. |
| Fidelity to mermaid | **Style/structure, not geometry** | Pixel-matching dagre is infeasible; we emit mermaid's CSS classes + shapes so it reads as mermaid, with our own coordinates. |
| SVG verification | **Self-consistency, not diff vs mermaid** | The AST comparison is the correctness gate (already green). The SVG check verifies our output is well-formed and complete; mermaid's `.svg` stays a human visual reference. |
| Curve style | **Straight polylines** | Simplest; upgrade to béziers later. |
| Shapes | **One template per `ShapeKind`** (14) | Direct, explicit geometry. |
| Arrow heads | **One `<marker>` per arrow type** | `arrow_point` / `arrow_circle` / `arrow_cross`; open = no marker. |
| Subgraphs | **Collapsed to a node (v1)** | Avoid recursive cluster layout for the first cut; add real clusters later. |
| Visual styling | **Deferred** | `style`/`classDef`/`linkStyle` parsed but not applied yet (see `deferred.md`). |
| Label sizing | **`ApproxMeasurer` only (v1)** | Char-advance heuristic, zero deps; better measurers plug in behind the interface. |

## Label measurement — the `TextMeasurer` abstraction

Layout must not know *how* labels are measured, so it takes a measurer:

```cpp
struct FontSpec { std::string family; double size_px; /* weight… */ };
struct LabelBox { double w, h; };

class TextMeasurer {
public:
  virtual ~TextMeasurer() = default;
  // BATCH: measure every label in one call (one round-trip for the browser).
  virtual std::vector<LabelBox> measure(const std::vector<std::string>& labels,
                                        const FontSpec& font) = 0;
};
```

Implementations (same interface, different builds):

- **`ApproxMeasurer`** — char-advance heuristic. Default for the CLI; used by both
  builds in v1. Zero dependencies.
- **`BrowserMeasurer`** *(later, WASM only)* — calls JS (Emscripten) to measure
  all labels with the real browser font engine (`canvas.measureText` /
  `<text>.getBBox()`). Accurate; compiled only in the Emscripten target.
- **`HeadlessMeasurer` / font-metrics table** *(possible future)* — offline
  accuracy for the CLI without a browser dependency. Not planned for v1.

Measurement is a **batch step before layout**: collect all node labels + edge
labels (subgraph titles too, once clusters exist) → one `measure()` call → size
map. `FontSpec` must match the renderer's CSS, or boxes won't fit the text.
Layout then applies **shape padding** on top: `node_size = shape_pad(shape,
text_box)` (a diamond/circle needs the text box inflated).

## v1 subgraph collapse

Instead of cluster layout, `collapse_subgraphs` rewrites the graph so each
subgraph is a single node:

1. **Owner map**: each vertex that belongs to a subgraph is represented by its
   **outermost** ancestor subgraph (nested subgraphs collapse into that
   ancestor). Vertices in no subgraph represent themselves.
2. **Node set**: drop the member vertices; add one node per outermost subgraph
   (`id` = subgraph id, `label` = subgraph title or id).
3. **Edges**: redirect each endpoint through the owner map, **drop self-loops**
   (edges that become internal to a collapsed subgraph), and **dedup**.
4. **Subgraphs**: cleared (no boxes drawn in v1).

Example (`f18_composite`): the `ci` subgraph `{LINT,TEST,BUILD}` collapses to a
node `ci`; internal `LINT→TEST→BUILD` vanish; `START→ci` stays; `BUILD→DONE`
becomes `ci→DONE`; `TEST --x FAIL` becomes `ci --x FAIL`.

> Real recursive cluster layout (bounding boxes, border-node edge routing) is the
> planned upgrade — see Roadmap.

## Layout — the phases (simplest forms)

Ranks/orders are computed **abstractly**; `direction` (TB/BT/LR/RL) is applied
only at coordinate time as an axis swap/flip, so the algorithm is
direction-agnostic.

**Phase 0 — preprocessing**
- *Cycle breaking:* DFS; reverse each back-edge to get a DAG; restore true
  direction only when drawing.
- *Node sizing:* `ApproxMeasurer` → text box → shape padding → node size.

**Phase 1 — rank assignment (longest-path)**
- `rank(source)=0`; `rank(v)=max(rank(u)+1)` over incoming `u→v`, in topo order.
- *Dummy nodes:* split any edge spanning >1 rank into unit-length segments
  through invisible dummy nodes, so every edge connects adjacent ranks.

**Phase 2 — ordering (barycenter)**
- Within each rank, order nodes to reduce crossings: iterate down/up a fixed
  number of sweeps, setting each node's position to the average position of its
  neighbours in the adjacent (fixed) rank, then sort.

**Phase 3 — coordinate assignment**
- Easy axis from rank: `y = rank·(nodeH+rankSep)` (for TB).
- Hard axis from order: initial `x = order_index·(nodeW+nodeSep)`, then a simple
  alignment nudge toward the mean neighbour `x` (keeping order + non-overlap).
- Apply `direction` here (BT flips y; LR swaps axes; RL swaps+flips).

**Phase 4 — edge routing**
- Short edge: polyline source-bottom → target-top.
- Long edge: polyline through its dummy waypoints (dummies then discarded).
- Arrowheads per `arrowHead` at the target end (and source end if bidirectional).
- Reversed edges (from Phase 0) drawn in their original direction.
- Label at the edge midpoint.

### Running example

`A→B, A→C, B→D, C→D, A→D` (TB): ranks `A(0) | B,C(1) | D(2)`, dummy `d1` at rank 1
for `A→D`; order `[B, d1, C]`; a clean diamond with `A→D` straight down the
middle through `d1`. (Full trace in the design discussion / commit history.)

## Layout IR (the model `render_svg` consumes)

```cpp
struct Point { double x, y; };
struct Size  { double w, h; };

struct LaidNode { std::string id, label; ShapeKind shape;
                  Point center; Size size; std::vector<std::string> classes; };
struct LaidEdge { std::string start, end, label;
                  std::vector<Point> points;      // waypoints incl. endpoints
                  Stroke stroke; ArrowHead head_end; bool head_start;
                  Point label_pos; };
struct LaidSubgraph { std::string id, label; Point origin; Size size; int depth; };

struct Layout { Size diagram;                      // -> SVG viewBox
                std::vector<LaidNode> nodes;
                std::vector<LaidEdge> edges;
                std::vector<LaidSubgraph> subgraphs; };  // empty in v1
```

`render_svg` is pure geometry → SVG: `<g class="nodes">` (shape `<path>` per
node), `<g class="edgePaths">` (polyline + arrow `<marker>` per edge),
`<g class="clusters">` (later), all with mermaid's class names, plus the diagram
`viewBox` from `Layout::diagram`.

## Module / file structure

```
include/mermaid/
  TextMeasure.hpp   # TextMeasurer + ApproxMeasurer + FontSpec/LabelBox
  Layout.hpp        # Layout IR + layout() + collapse_subgraphs()
  SvgRenderer.hpp   # render_svg()
src/mermaid/
  TextMeasure.cpp   # ApproxMeasurer
  Layout.cpp        # phases 0–4 + collapse_subgraphs   (the big one)
  SvgRenderer.cpp   # shape templates, arrow markers, emission
  cli_svg.cpp       # mermaid_svg executable (uses ApproxMeasurer)
  BrowserMeasure.cpp# EMSCRIPTEN-only (later)
```

Build-agnostic code lives in `libmermaid`; `BrowserMeasure.cpp` compiles only in
the Emscripten target. The **caller** picks the measurer: CLI → `ApproxMeasurer`;
WASM → `BrowserMeasurer` (later); the markdown fence handler uses an injectable
default (`Approx` until browser measurement is wired).

## Verification

- **Self-consistency** (automated) — *left for later development*: well-formed
  XML; every `LaidNode` → one node group; every `LaidEdge` → one path; labels
  present; (optional) no node-box overlaps. This is the planned SVG-stage gate;
  the first cut ships without it.
- **Visual** (human): open our SVG next to the pre-generated `*.svg` from
  `extract.mjs` in a browser — same diagram, different exact coordinates.
- The **AST comparison** remains the structural correctness gate (upstream).

## Roadmap (post-v1 upgrades, behind the same interfaces)

1. `BrowserMeasurer` + WASM wiring (accurate label sizes in the web build).
2. Real recursive **subgraph cluster layout** (bounding boxes + border-node edge
   routing) replacing the v1 collapse.
3. Visual **styling** (`style`/`classDef`/`linkStyle` → fill/stroke/color).
4. Better **coordinate assignment** (Brandes–Köpf-lite) and **bézier** curves.
5. Offline **font-metrics table** for sharper CLI sizing.
6. Multi-line / markdown labels.
```
