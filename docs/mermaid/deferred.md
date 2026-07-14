# Mermaid flowchart — deferred work

Things the pipeline **parses but does not yet fully process**, with the reason and
what it takes to finish them. Kept here so deferrals stay visible instead of
silently rotting.

Target: mermaid **11.0.0** flowchart subset. Verification compares our model
against `mermaid-utils/extract.mjs` output (the "extract AST").

---

## 1. `click` — tooltips / links (F-11)

- **Status:** lexed + parsed into `ast::ClickStmt`; **not lowered** into `FlowDb`.
- **Why deferred:** `FlowDb::Vertex` is intentionally minimal (`id, label, shape,
  classes, styles`) and has no `tooltip` / `link` / `link_target` fields.
- **Important:** the extract AST **does** serialize `tooltip` and `link` per
  vertex (`extract.mjs` `serializeVertices`). So this is a real gap in *our*
  model, not a limitation of the comparison target — F-11 cannot be verified
  until we add the fields.
- **To un-defer:**
  1. Add `std::optional<std::string> tooltip, link, link_target;` to
     `Vertex` in `include/mermaid/FlowDb.hpp`.
  2. Handle `ast::ClickStmt` in `lower()` (set those fields on the target vertex).
  3. Teach the AST comparator to compare `tooltip` / `link` (still dropping
     `domId`).
- **Affects fixtures:** F-11.

## 2. `linkStyle` — per-edge styling by index (F-10)

- **Status:** lexed + parsed into `ast::LinkStyleStmt`; **not lowered**.
- **Why deferred:** `FlowDb::Edge` has no style representation, and — unlike
  click — the **extract AST does not serialize edge styles** (`serializeEdges`
  emits only `start, end, label, stroke, arrowHead, length`). So omitting it does
  **not** affect the AST verification gate at all.
- **When it matters:** the SVG/layout stage — `linkStyle` is real render
  information (stroke color/width/dash by 0-based edge index, plus
  `linkStyle default`).
- **To un-defer (at render time):**
  1. Decide representation: per-`Edge` `std::vector<std::string> styles;` plus a
     `FlowDb`-level default, or a separate `link_styles` list keyed by index.
  2. Apply `LinkStyleStmt` (single index, comma list, and `default`) in `lower()`.
- **Affects fixtures:** F-10 (render only).

## 3. Front-matter `title` (and config) (F-12)

- **Status:** the lexer captures the raw front-matter block (`LexResult::front_matter`)
  and the `%%{init:}%%` directive (`init_directive`); **neither is parsed**.
- **Why deferred:** no YAML/JSON mini-parser yet. `FlowDb::title` **already
  exists** as a field — it is simply left empty. (This is *populate-later*, not a
  missing model field.)
- **Note:** the extract AST captures `title` via `getDiagramTitle()`. The
  `simple-mermaid` fixture has `title: ""`, so current tests are unaffected.
- **To un-defer:**
  1. Minimal `title:` extraction from `front_matter` → set `FlowDb::title`.
  2. (Optional) parse `config:` / `%%{init}%%` for theme/curve/direction at the
     render stage.
- **Affects fixtures:** F-12, F-13.

## 4. Label entity decoding (F-16)

- **Status:** labels are kept **raw** by the lexer (e.g. `#35;`, `&amp;`,
  `<br/>`). No HTML-entity / numeric-escape decoding.
- **Why deferred:** decoding is a presentation concern; the parser stays literal.
- **To un-defer:** decode entities either in `lower()` (if the extract AST stores
  decoded labels — verify) or in the SVG renderer.
- **Affects fixtures:** F-03 (entity cases), F-16.

---

## Out of scope (not deferred — excluded by the 11.0.0 target)

These are **not** planned; they postdate 11.0.0 and were excluded when the scope
was pinned:

- **`@{ shape: … }` node syntax** (F-02, second half) — v11.3+.
- **Edge IDs `e1@-->` and `@{ animate }`** (F-15) — v11.10+.

If the target mermaid version is ever bumped, revisit these.
