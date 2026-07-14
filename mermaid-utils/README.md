# mermaid-extractor

Generates two golden files per `.mmd` input:

| Output | Contents |
|--------|----------|
| `<name>.ast.json` | Parsed AST from `flowDb`: vertices, edges, subgraphs, direction, title, classes |
| `<name>.svg` | Fully rendered SVG from `mermaid.render()` |

Uses the mermaid library directly — no headless browser, no Puppeteer.

---

## Setup

```bash
npm install
```

---

## Usage

```bash
# All .mmd files in the current directory
node extract.mjs
node extract.mjs *

# Explicit files
node extract.mjs diagrams/f01.mmd diagrams/f02.mmd

# Files in another directory (outputs land next to each input)
node extract.mjs /path/to/fixtures/*.mmd
```

Outputs are written **alongside each input file**:
```
fixtures/
  f01_directions.mmd
  f01_directions.ast.json   ← generated
  f01_directions.svg        ← generated
```

---

## Generating fixtures for the C++ verification suite

The C++ test `tests/mermaid/test_flow_ast.cpp` compares our parser's `FlowDb`
(via `lower(parse(src))`) against the `.ast.json` golden files produced here.
Those golden files are the source of truth for the AST verification gate.

**To add a new verification fixture** (run from the repo root):

1. Create the diagram in the fixtures dir:
   ```
   tests/test-files/f20_mything.mmd
   ```
2. Generate its golden AST (and SVG) with this tool:
   ```bash
   node mermaid-utils/extract.mjs tests/test-files/f20_mything.mmd
   ```
   This writes `f20_mything.ast.json` (+ `.svg`) next to the input. Commit both
   the `.mmd` and the `.ast.json`.
3. Register the **basename** in the `kFixtures` list in
   `tests/mermaid/test_flow_ast.cpp`:
   ```cpp
   const std::vector<std::string> kFixtures = { /* … */ "f20_mything" };
   ```
4. Rebuild and run just the fixture comparisons:
   ```bash
   cmake --build build --target mermaid_tests -j4
   cd build && ctest -R FlowAst --output-on-failure
   ```

**Regenerate every golden at once** (e.g. after a mermaid version bump):
```bash
node mermaid-utils/extract.mjs tests/test-files/*.mmd
```

### Stay within the supported subset

The comparison targets the **mermaid 11.0.0** flowchart subset. Some constructs
are parsed but not yet lowered/compared — see `docs/mermaid/deferred.md`. Avoid
these in verification fixtures (they will mismatch):

- `click` interactions (tooltip / link)
- front-matter `title:` / `config:` and `%%{init:}%%`
- HTML entity escapes in labels (`#35;`, `&amp;`, `<br/>`)
- edge IDs / animation (`e1@-->`, `@{ animate }`) and `@{ shape: }` — post-11.0.0

`linkStyle` **is** fine to include: it does not affect the compared AST.

---

## AST JSON schema

```jsonc
{
  "diagramType": "flowchart",
  "title": "My diagram",        // from front-matter, or null
  "direction": "LR",            // TB | LR | BT | RL | TD
  "vertices": [
    {
      "id":      "A",
      "label":   "Start",
      "shape":   "stadium",     // rect | round | stadium | subroutine |
                                //   cylinder | circle | asymmetric | diamond |
                                //   hexagon | lean_right | lean_left |
                                //   trapezoid | inv_trapezoid | doublecircle
      "classes": ["primary"],
      "styles":  ["fill:#f9f"],
      "tooltip": null,
      "link":    null,
      "domId":   "flowchart-A-0"
    }
  ],
  "edges": [
    {
      "index":     0,
      "id":        "e1",          // named edge id, or null
      "start":     "A",
      "end":       "B",
      "label":     "yes",         // edge label text, or null
      "stroke":    "normal",      // normal | dotted | thick
      "arrowHead": "arrow",       // arrow | circle | cross | none
      "length":    1              // 1 = default, 2+ = extra dashes
    }
  ],
  "subgraphs": [
    {
      "id":        "sg1",
      "label":     "My group",
      "nodes":     ["A", "B"],    // node ids that belong to this subgraph
      "direction": "LR"           // per-subgraph direction override, or null
    }
  ],
  "classes": {
    "primary": { "styles": ["fill:#4a90d9", "color:#fff"] }
  }
}
```

---

## Notes

### Sequential processing
Mermaid's internal `flowDb` is **global mutable state**. The script processes
files one at a time to avoid race conditions between parse calls.

### SVG render requires a DOM
Mermaid calls DOM APIs during rendering (`document.createElementNS`, SVG geometry
methods, etc.). The script bootstraps `svgdom` + `jsdom` as globals before
importing mermaid. If a diagram fails to render (SVG step), the AST is still
written — SVG failures are non-fatal.

### `htmlLabels: false`
`svgdom` does not support `<foreignObject>` well enough for HTML labels.
The script disables HTML labels globally, which means labels are rendered as
`<text>` elements. This does not affect the AST output at all.

### Version pinning
The `package.json` targets mermaid `^11.0.0`. If you need to test against a
specific version, pin it exactly:
```bash
npm install mermaid@11.4.0
```
Regenerate your golden files whenever you upgrade mermaid.
