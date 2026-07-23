# Mermaid design debate — how (and where) to render

**Theme:** Mermaid design debate

**Source transcript:** `1365d948-53d3-4761-826b-19b783ca2c02.jsonl`  
**Records:** 0–105

**Dates:** 2026-06-12 21:45 → 2026-06-14 08:02  
**User turns:** 11

**Related commits:**

- `0b014d7 add mermaid example in the live editor`
- `90eea37 Refactor code to handle mermaid code blocks`

---

## ▸ Prompt — 2026-06-12 21:45

> Which are the possible paths to integrate mermaid support to this parser? I know mermaid blocks are treated as classical fenced code blocks and then an external library like mermaid.js takes care of the actual interpretation of the text inside the fences. I would like to integrate the mermaid iterpretation in the project.

Let me quickly survey the project structure before answering.

Now I have a clear picture. Here are the two viable paths:

---

## Paths to integrate Mermaid

### Path A — Frontend-only (recommended, zero C++ changes)

The web app already sets `preview.innerHTML` directly from the C++ parser output. Since a `mermaid` fenced block already produces `<pre><code class="language-mermaid">...</code></pre>`, you just need to:

1. Add the mermaid.js CDN script to [web/index.html](web/index.html).
2. After `preview.innerHTML = ...`, call `mermaid.run({ nodes: preview.querySelectorAll('.language-mermaid') })` — mermaid.js will find those elements and replace them with SVG in-place.

That's it. No C++ changes, no rebuild of the WASM. The main tradeoff: mermaid SVGs only appear in the browser preview, not in raw HTML output from the CLI or library.

---

### Path B — C++ renderer emits Mermaid-ready HTML (no JS library dependency in output)

Modify [src/HtmlRenderer.cpp:193-208](src/HtmlRenderer.cpp#L193-L208) to detect `lang == "mermaid"` and emit a `<div class="mermaid">` instead of a `<pre><code>`. The raw diagram source goes inside the div unescaped. Then mermaid.js (loaded anywhere on the page) auto-initializes on `.mermaid` divs.

This changes the HTML output contract for all consumers (CLI, tests, library users), which is the main tradeoff. It also tightly couples the renderer to a specific mermaid.js convention.

---

**Recommendation:** Path A. It's non-invasive, reversible, and keeps the parser output pure HTML — mermaid interpretation stays in the presentation layer where it belongs, exactly like syntax highlighters (Prism, highlight.js) work alongside this kind of parser.

## ▸ Prompt — 2026-06-12 21:53

> I like path B, but with some changes. I wouldn't change the standard HtmlRender, I would create a new visitor and trigger it by a flag (like --parse-mermaid). Also, is it feasible to parse mermaid blocks in the cpp? are there any limitation?

Good, the architecture is clear. Now for the feasibility question about parsing Mermaid in C++:

---

## Architecture: new `MermaidHtmlRenderer`

Totally clean fit. You'd create `MermaidHtmlRenderer : public HtmlRenderer`, override only `visit(const BlockNode&)`, intercept the `CodeBlock` case when `lang == "mermaid"`, and delegate everything else to `HtmlRenderer::visit`. A `--parse-mermaid` flag in [src/main.cpp](src/main.cpp) instantiates this class instead of `HtmlRenderer`. Zero changes to existing renderers.

---

## Feasibility of parsing Mermaid in C++

**What "parsing" means here matters a lot.** There are two levels:

### Level 1 — Parse the Mermaid DSL into a structured AST (fully feasible)
Mermaid has a well-defined grammar. You could write a C++ parser for each diagram type (flowchart, sequence, class, etc.) that produces an in-memory representation. This is standard parser work — no fundamental C++ limitation. The cost is high: Mermaid has ~10 diagram types, each with its own grammar quirks, and the official grammar is maintained in a ~3000-line Jison file that evolves frequently.

### Level 2 — Render Mermaid to SVG in C++ (where limitations bite)
To produce actual SVG output without JS, you need a layout engine:
- **Flowcharts/graphs** require a DAG layout algorithm (e.g. Sugiyama/ELK). These exist as C++ libs (`ogdf`, `elkjs` has no C++ port) but are large dependencies.
- **Sequence diagrams** are simpler — just vertical lanes and arrows — and could be rendered to SVG with reasonable effort.
- **Gantt, pie, etc.** are relatively tractable.

There is **no existing C++ library that covers the full Mermaid spec**. The only complete implementation is the official mermaid.js, written in TypeScript and tightly coupled to the browser DOM/SVG APIs.

---

## Practical recommendation

**The most realistic C++ path for `--parse-mermaid`** is:

The renderer detects `lang == "mermaid"` and emits `<div class="mermaid">raw diagram source</div>` instead of a fenced code block. The consumer (browser, or a CLI pipeline that pipes through a headless mermaid-cli) handles the actual rendering. This is what every static site generator (Jekyll, Hugo, Docusaurus) does.

If you want **fully self-contained SVG output from the CLI** (no JS at all), the only realistic option today is to shell out to `mmdc` (the mermaid CLI tool, a Node binary) as a subprocess — feasible but adds a runtime dependency.

Want to proceed with the renderer-only approach (emit `<div class="mermaid">`) and pair it with mermaid.js in the web frontend?

## ▸ Prompt — 2026-06-12 22:00

> I don't want to render anything in cpp, I would like to output the svg structure and make the browser render it

So the goal is: C++ produces SVG markup as a string, which gets embedded inline in the HTML output — the browser renders it natively, no JS needed. That's clean.

## What this actually requires in C++

For each Mermaid diagram type you support, you need three stages:

**1. Lexer + parser** — tokenize the Mermaid DSL and build an in-memory structure (nodes, edges, labels, directives). Straightforward C++ work, but each diagram type has its own syntax.

**2. Layout engine** — compute x/y coordinates for each element. This is the hard part:
- **Sequence diagrams**: trivial — fixed vertical lanes, arrows stack top-to-bottom. Fully hand-implementable.
- **Flowcharts / graphs**: require a proper DAG layout algorithm (e.g. Sugiyama layered layout). Non-trivial but there are C++ implementations (e.g. the `ogdf` library). Without one, edges will cross randomly.
- **Pie, Gantt**: just arithmetic — angles or bar widths. Easy.
- **Class / ER diagrams**: similar to flowcharts, need layout.

**3. SVG emitter** — walk the laid-out structure and emit `<svg>`, `<rect>`, `<path>`, `<text>`, etc. Pure string building, no limitations.

## Limitations

- **No single C++ lib covers the full Mermaid spec** — you're writing parsers from scratch or embedding the official Jison grammar as a generated parser (doable with tools like re2c + lemon/bison).
- **Graph layout is the only genuinely hard problem.** If you only target sequence diagrams + pie + Gantt first, you can ship something useful without a layout engine.
- **Font metrics**: SVG `<text>` width depends on the font. Without a font metrics library, label boxes will be approximate. Usually acceptable.
- **Mermaid syntax evolves** — your parser would need maintenance as Mermaid adds features.

## Realistic scope

If the goal is full Mermaid support, the honest path is to shell out to `mmdc` (mermaid's official CLI) as a subprocess — it outputs SVG, you embed it. One external dependency, covers everything, stays up to date.

If the goal is to implement it in pure C++ as a project (which sounds like it might be the case given this is a university project), starting with **sequence diagrams only** is the right call — they're self-contained, layout is trivial, and you can produce clean SVG without any third-party library.

Which direction are you aiming for?

## ▸ Prompt — 2026-06-12 22:07

> Can you explain in detail why a layout engine is needed and what problem it resolves?

Sure. The core problem is: given a set of nodes and edges, **where do you place them on the canvas so the diagram is readable?**

## The problem with no layout engine

Say you have this flowchart:

```
A --> B
A --> C
B --> D
C --> D
```

You have 4 nodes and 4 edges. The SVG needs concrete pixel coordinates for every node — `<rect x="?" y="?" ...>`. You have to pick those numbers somehow.

If you just assign them arbitrarily (e.g. left-to-right in parse order), you might get:

```
[A]───[B]───[C]───[D]
```

Which is technically correct but visually wrong — the diamond shape of the graph is completely lost, and the `A→C` and `B→D` edges would cross or loop back awkwardly.

## What a layout engine does

It takes the graph structure and computes coordinates that satisfy aesthetic criteria:
- **Edges flow in one direction** (top-to-bottom or left-to-right) — no upward arrows
- **Nodes on the same "rank"** (same distance from root) are aligned on the same row
- **Edge crossings are minimized** — the algorithm reorders nodes within each rank to reduce how many edges cross each other
- **Nodes don't overlap**

For the example above, a layout engine produces the diamond shape you'd draw by hand:

```
      [A]
     /   \
   [B]   [C]
     \   /
      [D]
```

## Why it's algorithmically hard

The specific problem of minimizing edge crossings is **NP-hard** in general. Real layout engines (like the Sugiyama framework, which is what Graphviz/dot uses) solve it in stages:

1. **Cycle removal** — reverse some edges temporarily so the graph is a DAG
2. **Layer assignment** — assign each node to a horizontal rank (row)
3. **Crossing minimization** — reorder nodes within each rank using heuristics (barycenter, median)
4. **Coordinate assignment** — compute actual x/y pixel positions, spreading nodes to avoid overlap

Each stage is non-trivial. Stage 3 alone has been the subject of decades of research papers.

## Why sequence diagrams don't need this

In a sequence diagram, the layout is **fully determined by the syntax**:

```
Alice -> Bob: message
Bob -> Alice: reply
```

- Actors are always in a fixed horizontal order (parse order)
- Messages always go top-to-bottom in the order they appear
- There are no free-floating nodes to place

Every coordinate is just arithmetic: actor `n` is at `x = n * lane_width`, message `m` is at `y = m * row_height`. No optimization problem at all.

That's why sequence diagrams are a natural starting point for a pure C++ implementation — you get useful output with zero algorithmic complexity beyond basic parsing.

## ▸ Prompt — 2026-06-12 22:15

> How does mermaid.js understand how big the blocks should be. X/Y coordinates are absolut? Calculated on the html div block it is in?

Great question. Mermaid.js relies entirely on the **browser's font rendering and DOM measurement APIs** — this is actually one of the main reasons it's so hard to replicate outside the browser.

## How it works in mermaid.js

**Step 1 — Render text invisibly to measure it**

To know how wide a node box should be, mermaid first needs to know how wide the label text is. It does this by:
1. Creating a temporary SVG `<text>` element and appending it to the DOM (often hidden off-screen)
2. Calling `element.getBBox()` — a browser API that returns the exact bounding box of the rendered text in pixels, accounting for the actual font, size, kerning, and ligatures

Only after measuring does it know the node width. The box is then `text_width + padding`.

**Step 2 — Coordinates are absolute pixels within the SVG viewport**

Yes, all coordinates in the final SVG are absolute pixel values — `<rect x="42" y="108" width="120" height="36">`. The SVG has a fixed `width` and `height` on its root element, computed after layout.

The SVG doesn't adapt to the containing `<div>`. Instead mermaid computes the total bounding box of all laid-out elements, sets the SVG dimensions to fit exactly, and the browser then scales/scrolls it like any inline element.

## Why this is a problem in C++

Without a browser, you have no `getBBox()`. To know how wide a label is you'd need a **font metrics library** — something that reads a TTF/OTF font file and computes the advance width of each glyph at a given size. Options like FreeType can do this, but it's a heavy dependency and you'd need to bundle the font file itself to get consistent results.

The common workaround in non-browser SVG generators is to **approximate** text width with a fixed character width (e.g. `char_count × 8px` for a monospace font, or `char_count × 6.5px` for a proportional font). It's inaccurate but usually good enough for boxes to not overlap badly.

## For sequence diagrams specifically

This is much less of an issue because:
- Lane width can be set to a fixed constant or derived from the longest actor name with the approximation above
- Message labels sit on arrows and don't need precisely-sized boxes
- There's no crossing minimization that depends on node sizes

So the approximation error is cosmetic (slightly too wide or narrow boxes) rather than structural.

## ▸ Prompt — 2026-06-12 22:43

> So it would be extremely difficult to build the svg structure natively from cpp. not because it cannot recognise the mermaid syntactic constructs but because it doesn't have a DOM that tells it exactly the size to create the boxes. But what about mmdc? (mermaid's official CLI) how can it detect the font size etc...?

Exactly right on the diagnosis. And `mmdc` solves the same problem the same way mermaid.js does — because **mmdc is just mermaid.js running inside a headless browser**.

## What mmdc actually is

`mmdc` (the mermaid CLI) uses **Puppeteer** under the hood, which launches a real Chromium instance in headless mode. The flow is:

1. Puppeteer opens a blank HTML page in headless Chromium
2. It injects the mermaid diagram source into that page
3. mermaid.js runs normally — `getBBox()` works because there's a real browser DOM
4. The resulting SVG is extracted from the DOM and written to a file

So mmdc has full font metrics, full layout, full everything — because it's literally running in Chrome. It just happens to be Chrome with no window.

## The irony

This means:
- mermaid.js **requires** a DOM to measure text
- mmdc **provides** that DOM by bundling an entire browser
- The output SVG has hardcoded pixel coordinates computed by that browser

There's no lightweight path. The Mermaid project has explicitly chosen to make the browser the rendering engine, and everything else (mmdc, mermaid-js server-side) is just a wrapper that sneaks a browser in through the back door.

## What this means for your project

If you want self-contained SVG from C++ with no external processes, the text measurement approximation is unavoidable. But as discussed, for sequence diagrams it's cosmetic. For flowcharts with a layout engine, slightly wrong box sizes would cause nodes to overlap or leave too much space — noticeable but not catastrophic if you add generous padding.

The cleanest architecture for your `--parse-mermaid` flag is probably:
- **C++ handles**: parsing, layout arithmetic (for sequence diagrams), SVG string generation
- **Accept the limitation**: fixed-width font approximation for text sizing
- **Document it**: the renderer targets a monospace font at a fixed size, so box sizing is predictable and consistent

## ▸ Prompt — 2026-06-12 22:48

> What does mermaid.js need to detect mermaid.js blocks inside the injected html outputed by the cpp parser?

Just the `<div class="mermaid">` wrapper around the raw diagram source.

When mermaid.js initializes (either via `mermaid.initialize()` + auto-detection, or an explicit `mermaid.run()`), it scans the DOM for elements matching its selector — by default `.mermaid` — and replaces their text content with the rendered SVG in-place.

So the C++ renderer just needs to emit:

```html
<div class="mermaid">
graph TD
    A --> B
    A --> C
    B --> D
    C --> D
</div>
```

instead of:

```html
<pre><code class="language-mermaid">
graph TD
...
</code></pre>
```

The diagram source goes inside **unescaped** (mermaid reads it as raw text, not HTML). That's the only contract.

## How this maps to your architecture

- `MermaidHtmlRenderer` inherits `HtmlRenderer`, overrides only the `CodeBlock` case
- When `lang == "mermaid"`: emit `<div class="mermaid">` + raw `node.string_content` + `</div>`
- For every other language: call `HtmlRenderer::visit(node)` as normal
- The web frontend (`index.html`) loads mermaid.js from CDN and calls `mermaid.run()` after `preview.innerHTML` is set
- The CLI output under `--parse-mermaid` is just HTML that any page with mermaid.js can consume

This is actually the simplest possible integration — no C++ parsing of Mermaid syntax at all, no SVG generation, no font metrics problem. The C++ just changes the wrapping tag and leaves the content alone.

## ▸ Prompt — 2026-06-12 22:49

> It is the easies but it seams the only one feasible

Yes, exactly. Let's recap why each alternative falls apart:

**Native C++ SVG generation** — you can parse the Mermaid DSL fine, but without font metrics you can't size boxes accurately. You could approximate for sequence diagrams, but flowcharts also need a layout engine on top of that. Significant work for limited and imperfect output.

**Shell out to mmdc** — works, produces perfect SVG, but mmdc silently bundles a full Chromium. You're adding ~200MB of runtime dependency to what is otherwise a lightweight C++ parser. Defeats the purpose.

**Embed a JS engine (V8/QuickJS)** — you could run mermaid.js inside C++ via an embedded JS runtime, but you'd still need a headless DOM for `getBBox()`. V8 alone isn't enough; you'd need something closer to a full browser engine. Even more complexity than mmdc.

**The `<div class="mermaid">` approach** — the C++ stays completely out of the rendering problem. It just correctly identifies the block, preserves the content unescaped, and changes the wrapper tag. mermaid.js in the browser handles everything it was designed to handle.

So it's not just the easiest — it's the only one where the complexity budget is proportional to what the C++ parser is actually responsible for. The parser's job is structure, not rendering.

## ▸ Prompt — 2026-06-12 22:52

> There is no way of partially parsing the mermaid in some intermidiate representation and then give it to mermaid.js to handle only the svg and layout part?

Good instinct, but mermaid.js doesn't expose that seam as a public API.

## What mermaid.js actually exposes

- `mermaid.render(id, text)` — full pipeline, takes raw text, returns SVG string
- `mermaid.parse(text)` — validates syntax, returns true/throws, no AST returned
- `mermaid.run()` — scans DOM for `.mermaid` divs, runs full pipeline on each

The internal stages (lexer → parser → AST → layout → SVG) exist in the source but are **not exported**. The AST format is also undocumented, diagram-type-specific, and changes between versions since it's considered an implementation detail.

## The theoretical path

You could in principle:
1. Read the Jison grammar sources for a specific diagram type to reverse-engineer the internal AST shape
2. Have C++ output JSON matching that shape
3. Write a thin JS shim that deserializes it and injects it past the parser into the layout stage

But that shim would be calling private internal functions, so it would break on any mermaid.js minor version update. You'd be maintaining a brittle bridge against an undocumented moving target.

## What would actually make sense

If the goal is "C++ does structural work, JS does rendering", the only clean split that exists today is exactly the `<div class="mermaid">` one — C++ identifies and extracts the block, JS owns everything after that. The Mermaid project simply never designed for an external pre-parser to plug in.

## ▸ Prompt — 2026-06-12 23:02

> It could be possible to add a parameter following --parse-mermaid as optional which specifies the size font. the wasm binary takes in input both text and that param and could then generate the SVGs acconrdingly to the parameters

Yes, and this is actually a sound approach. And for the WASM case specifically you can go one step further and make it **exact**.

## CLI path (`--parse-mermaid 14`)

Font size is a known constant at render time. You approximate character width as:

```
char_width ≈ font_size * ratio
```

Where `ratio` is ~0.6 for proportional fonts, 0.5 for monospace (well-established typographic approximation). Box width becomes `label_length * char_width + padding`. Not pixel-perfect but consistent and predictable, especially if you document that the renderer targets a specific font.

## WASM path — can be exact

This is the interesting case. The browser already knows the font. Before calling the WASM function, the JS can measure a reference string directly:

```js
function measureCharWidth(fontSize) {
    const span = document.createElement('span');
    span.style.font = `${fontSize}px monospace`;
    span.style.visibility = 'hidden';
    span.textContent = 'M'.repeat(100);
    document.body.appendChild(span);
    const w = span.getBoundingClientRect().width / 100;
    document.body.removeChild(span);
    return w;
}

Module.parseMarkdown(text, fontSize, measureCharWidth(fontSize));
```

The JS does the one measurement the browser is good at, passes `char_width` as a parameter, and the WASM generates geometrically accurate SVG. No `getBBox()` per node needed — just one calibration measurement upfront.

## What the C++ signature looks like

```cpp
// wasm_bindings.cpp
std::string parseMarkdown(std::string text, float font_size, float char_width);

// CLI
--parse-mermaid [font_size]   // char_width approximated from ratio
```

This is genuinely feasible and elegant — it keeps all SVG generation in C++, uses the browser only for what it uniquely provides (one font measurement), and the CLI degrades gracefully with an approximation.

## ▸ Prompt — 2026-06-14 08:02

> Let's go with this for the moment:
>
> The <div class="mermaid"> approach — the C++ stays completely out of the rendering problem. It just correctly identifies the block, preserves the content unescaped, and changes the wrapper tag. mermaid.js in the browser handles everything it was designed to handle. 
>
> Let's design the needed changes

Let me read the relevant files to design precisely.

Here's the full design:

---

## Files to create

**`include/markdown_parser/MermaidHtmlRenderer.hpp`**
```cpp
#pragma once
#include "HtmlRenderer.hpp"

class MermaidHtmlRenderer : public HtmlRenderer {
public:
  void visit(const BlockNode &) override;
};
```

**`src/MermaidHtmlRenderer.cpp`**
- Override `visit(const BlockNode&)`
- If `node.type == CodeBlock && lang == "mermaid"`: emit `<div class="mermaid">` + `node.string_content` (raw, unescaped) + `</div>\n`
- Otherwise: delegate to `HtmlRenderer::visit(node)`

---

## Files to modify

**[src/main.cpp](src/main.cpp)**
- Add `bool mermaid_mode = false`
- Add `--parse-mermaid` to the flag parsing loop
- Update the usage string
- Add an `else if (mermaid_mode)` branch that instantiates `MermaidHtmlRenderer`

**[src/wasm_bindings.cpp](src/wasm_bindings.cpp)**
- Add a second exported function `parseMarkdownMermaid(const std::string& input)` that uses `MermaidHtmlRenderer`
- Register it via `EMSCRIPTEN_BINDINGS`

**[web/index.html](web/index.html)**
- Add mermaid.js from CDN in `<head>`
- After `preview.innerHTML = ...` in the `render()` function, call `mermaid.run({ nodes: [...preview.querySelectorAll('.mermaid')] })`
- Add a toggle (checkbox or button) in the header that switches between `parseMarkdown` and `parseMarkdownMermaid`

**[CMakeLists.txt](CMakeLists.txt)** (likely)
- Add `MermaidHtmlRenderer.cpp` to the source list

---

## Key constraint

`node.string_content` must go into the div **unescaped** — mermaid.js reads it as plain text, not HTML. The existing `HtmlRenderer` escapes it via `escapeHtml()`, which would corrupt the diagram syntax. `MermaidHtmlRenderer` must skip that call for mermaid blocks.

Want to proceed with implementation?

