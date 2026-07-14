# Mermaid flowchart — implementation status & deferred work

Where the mermaid flowchart feature stands after the first implementation, and
what is intentionally **missing, simplified, or deferred**. This is the
entry-point overview; two companion docs hold the details:

- `docs/mermaid/deferred.md` — parser/lower-stage deferrals (per-feature, with
  how to un-defer).
- `docs/mermaid/rendering.md` — the layout/SVG design + roadmap.

Target: **mermaid 11.0.0** flowchart subset. Correctness is gated by comparing
`lower(parse(src))` against `mermaid-utils/extract.mjs` output.

---

## What works (the happy path)

```
markdown ```mermaid``` fence
  → lexer → LR(1) parser (generated) → AST → lower() → FlowDb   [verified vs mermaid]
  → collapse_subgraphs → measure → layout → render_svg → inline SVG
```

- **Lexer + parser**: the whole supported subset; the grammar is verified
  conflict-free canonical LR(1) and the parser is generated at build time by
  `tools/lrgen`.
- **`lower()` → `FlowDb`**: directions, vertices (last-label-wins), `&`
  cross-product edges, chaining, subgraph membership, classes/styles. Verified
  against mermaid across **15 fixtures** (`tests/mermaid/test_flow_ast.cpp`).
- **Layout + SVG**: minimal Sugiyama → mermaid-classed SVG. Renders the whole
  fixture corpus to valid XML.
- **Both builds render server-side**: native (CLI + `md_parser_bin
  --parse-mermaid`) with `ApproxMeasurer`; web/wasm with `BrowserMeasurer`
  (real font metrics via canvas). No client-side mermaid.js required.

---

## Deferred / missing, by stage

### Parser / lower  (details: `deferred.md`)

| Item | State | Effect |
|---|---|---|
| `click` (tooltip / link) | parsed, **not lowered** | F-11 not verifiable until `Vertex` gains `tooltip`/`link` |
| `linkStyle` | parsed, **not lowered** | invisible to the AST gate; needed for edge styling at render |
| Front-matter `title` / config, `%%{init}%%` | captured raw, **not parsed** | `FlowDb::title` stays empty (F-12/13) |
| Label entity decoding (`#35;`, `&amp;`, `<br/>`) | labels kept **raw** | F-16 / entity cases would mismatch |
| `@{ shape: }`, edge IDs `e1@-->` | **out of scope** | post-11.0.0 syntax |

### Layout  (v1 = simplest choice everywhere; details + roadmap: `rendering.md`)

- **Subgraphs are collapsed to single nodes** — no real recursive cluster
  layout (no bounding boxes, no border-node edge routing). Nested subgraphs
  collapse into their outermost ancestor.
- **Ranking**: longest-path only — no tightening pass, so some edges are longer
  than necessary.
- **Ordering**: barycenter, fixed number of sweeps (not iterated to stability).
- **Coordinates**: rank centering only — no per-node alignment nudge, so edges
  are wavier than dagre's.
- **Edge routing**: straight polylines — no béziers/curve styles.
- **Self-loops** (`A --> A`) are **not drawn**.
- **Geometry does not match mermaid** by design — style/structure only, our own
  coordinates.

### Rendering (SVG)

- **Visual styling not applied** — `style` / `classDef` / `linkStyle` are parsed
  but the SVG uses only mermaid's *default* node/edge appearance. No per-node
  fill/stroke/color, no per-edge linkStyle.
- **Node sizing is approximate on the CLI** — `ApproxMeasurer` is a
  char-advance heuristic (the web build measures accurately). No font-metrics
  table.
- **Shape templates are approximate** for a few kinds (e.g. cylinder).
- **Single-line labels only** — no multi-line / markdown / `<br/>` labels,
  no `<tspan>` wrapping.
- **Arrow markers are basic** (three types); no per-edge marker theming.

### Build / WASM

- **Web render needs a browser DOM.** `BrowserMeasurer` uses a canvas; in a
  non-DOM wasm host (e.g. bare node) measurement throws — it is not wrapped in a
  fallback to `ApproxMeasurer` yet.
- Building the wasm target **regenerates the committed** `web/dist/markdown_parser.{js,wasm}`.

### Verification

- **AST comparison is the only automated gate** (15 fixtures).
- **SVG self-consistency test is deferred** (`rendering.md`, "Verification"):
  well-formed XML, one node group per node, one path per edge, labels present,
  no overlaps. Currently the SVG is only checked by hand against the
  pre-generated `*.svg`.

---

## Suggested next steps (priority order)

1. **SVG self-consistency test** — cheap, closes the render-stage gate.
2. **Visual styling** — lower + apply `classDef` / `style` / `linkStyle`
   (also un-defers `Vertex` style fidelity). High visual payoff.
3. **Real recursive subgraph clusters** — boxes + border-node edge routing,
   replacing the v1 collapse.
4. **WASM measurement fallback** — fall back to `ApproxMeasurer` when no canvas.
5. **Layout polish** — coordinate alignment nudge, béziers, rank tightening.
6. **Font-metrics table** for sharper CLI sizing; **multi-line labels**.
7. **Un-defer parser items** as needed: `title` (front-matter parse), `click`
   (Vertex fields), entity decoding.
