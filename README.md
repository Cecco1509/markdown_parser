# Markdown Parser with Mermaid Diagram Support

A Markdown parser written in **C++20** that implements the
[CommonMark v0.31.2](https://spec.commonmark.org/0.31.2/) specification and adds
first-class support for **Mermaid** flowchart code blocks. `` ```mermaid `` fences
are detected during parsing and tagged as distinct diagram nodes in the output,
and an integrated Mermaid engine can parse, lay out, and render those diagrams to
standalone **SVG** — no browser or JavaScript required.

The project produces a structured AST that can be emitted as **HTML** or as a
**JSON AST**, and ships with a live in-browser demo compiled to **WebAssembly**.

> *AP course project B02 — AI-Assisted Markdown Parser with Mermaid Diagram Support.*

---

## Features

- **Full CommonMark v0.31.2** block and inline parsing: ATX/Setext headings,
  ordered/bullet lists, fenced & indented code blocks, blockquotes, thematic
  breaks, links & images (inline, reference, autolinks), emphasis/strong, code
  spans, HTML blocks, entities, and the tab-expansion rules.
- **Mermaid detection & tagging** — `` ```mermaid `` blocks become dedicated
  diagram nodes rather than plain code blocks.
- **Native Mermaid rendering** — a self-contained flowchart pipeline
  (lex → parse → lower → layout → SVG) produces inline SVG for each diagram,
  targeting the Mermaid 11 flowchart subset.
- **Two output formats** — HTML or a JSON AST (`mdast`-style).
- **WebAssembly demo** — a live, two-pane Markdown editor running the parser
  entirely client-side.
- **Extensive test suite** — ~595 tests, including the complete CommonMark spec
  conformance suite plus Mermaid golden-file verification.

---

## Architecture at a glance

The parser runs in two phases (per the CommonMark reference algorithm):

```
source ──▶ PreScanner ──▶ SpineHandler ──▶ InlineParser ──▶ Renderer ──▶ HTML / JSON
           (line scan)    (block tree)      (inline tree)    (Html/Json)
```

The Mermaid engine is an independent module; the only coupling point is the
Markdown fence handler that dispatches `` ```mermaid `` blocks into it:

```
mermaid src ──▶ Lexer ──▶ FlowParse ──▶ Lower ──▶ Layout ──▶ SvgRenderer ──▶ SVG
                          (LR parser)   (FlowDb)  (positions)
```

The flowchart parser is generated at build time from
[`src/mermaid/flowchart.grammar`](src/mermaid/flowchart.grammar) by the `lrgen`
LR-parser generator in [`tools/lrgen/`](tools/lrgen/).

A full design specification lives in [`docs/`](docs/index.md).

---

## Building

Requires **CMake ≥ 3.20** and a **C++20** compiler. GoogleTest and nlohmann/json
are fetched automatically by CMake.

```bash
cmake -S . -B build
cmake --build build -j
```

This produces:

| Target                  | Description                                        |
|-------------------------|----------------------------------------------------|
| `md_parser_bin`         | Markdown → HTML/JSON CLI (the main demo program)   |
| `mermaid_ast`           | Parse a `.mmd` file and print its AST              |
| `mermaid_svg`           | Parse + layout + render a `.mmd` file to SVG       |
| `md_parser` / `mermaid` | Static libraries                                   |

---

## Running the demo (CLI)

```bash
# Render Markdown to HTML
./build/md_parser_bin input.md

# Render Markdown to a JSON AST
./build/md_parser_bin --json input.md

# Render Markdown to HTML, rendering mermaid blocks to inline SVG
./build/md_parser_bin --parse-mermaid input.md
```

Options:

| Flag              | Effect                                                       |
|-------------------|--------------------------------------------------------------|
| *(none)*          | Emit HTML; mermaid blocks stay as `<pre><code>` code blocks  |
| `--json`          | Emit the JSON AST instead of HTML                            |
| `--parse-mermaid` | Render `` ```mermaid `` blocks to inline `<svg>`             |
| `--parse-math`    | Render math blocks                                           |
| `--debug`         | Emit annotated/debug HTML                                    |

### Example

Input `demo.md`:

````markdown
# Hi

Some **bold** text.

```mermaid
graph TD
A-->B
```
````

`./build/md_parser_bin demo.md`:

```html
<h1>Hi</h1>
<p>Some <strong>bold</strong> text.</p>
<pre><code class="language-mermaid">graph TD
A--&gt;B
</code></pre>
```

`./build/md_parser_bin --json demo.md`:

```json
{"type":"root","children":[
  {"type":"heading","depth":1,"children":[{"type":"text","value":"Hi"}]},
  {"type":"paragraph","children":[/* … */{"type":"strong","children":[{"type":"text","value":"bold"}]}/* … */]},
  {"type":"code","lang":"mermaid","value":"graph TD\nA-->B\n"}]}
```

With `--parse-mermaid`, the third node is emitted as a rendered
`<div class="mermaid"><svg …>…</svg></div>`.

---

## Testing

The full suite (~1340 cases) runs under CTest:

```bash
cmake --build build -j
cd build && ctest --output-on-failure
```

It builds two executables. **`markdown_tests`** holds two independent
parameterized suites — one per output format — over the same CommonMark v0.31.2
spec examples:

- **`CommonMarkSpecTest`** (HTML) — compares our HTML against the spec's expected
  HTML ([`commonmark_spec.json`](tests/test-files/markdown/commonmark_spec.json)).
- **`JsonMdastTest`** (JSON) — compares our JSON AST against the reference
  [mdast](https://github.com/syntax-tree/mdast) produced by **remark**
  ([`commonmark_spec_mdast.json`](tests/test-files/markdown/commonmark_spec_mdast.json);
  see [`markdown-utils/`](markdown-utils/) for how those goldens are generated).

**`mermaid_tests`** holds the Mermaid lexer/parse/lower unit tests plus
**golden-file verification** comparing our `FlowDb` AST against the official
Mermaid library's output (fixtures in
[`tests/test-files/mermaid/`](tests/test-files/mermaid/); see
[`mermaid-utils/`](mermaid-utils/README.md)).

### Targeting a single test type

Each suite can be run on its own, either through the test binary
(`--gtest_filter`) or through CTest (`-R` matches the test name, `-L` the label):

```bash
# HTML conformance only
./build/tests/markdown_tests --gtest_filter='*CommonMarkSpec*'
cd build && ctest -R CommonMarkSpec --output-on-failure   # (or: ctest -L spec)

# JSON / mdast conformance only
./build/tests/markdown_tests --gtest_filter='*JsonMdast*'
cd build && ctest -R JsonMdast --output-on-failure

# Mermaid: whole suite, or just the AST golden verification
./build/tests/mermaid_tests
cd build && ctest -R FlowAst --output-on-failure
```

A single example is addressable by its generated name, e.g.
`--gtest_filter='*JsonMdast*/023_*'` runs spec example 23 in the JSON suite.

---

## Web demo (WebAssembly)

The parser compiles to WebAssembly for a live browser demo — a two-pane editor
that re-parses on every keystroke, with a toggle for Mermaid rendering.

Build with [Emscripten](https://emscripten.org/) (`emsdk` activated):

```bash
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
```

This emits `web/dist/markdown_parser.js` + `.wasm`. Serve the `web/` directory
over HTTP (WASM cannot be loaded from `file://`):

```bash
cd web && python3 -m http.server 8000
# open http://localhost:8000
```

---

## Project layout

```
markdown_parser/
├── include/markdown_parser/   # public headers (core, parser, renderer, handlers, utils)
├── include/mermaid/           # mermaid engine headers
├── src/markdown_parser/       # parser, renderers, fence handlers
├── src/mermaid/               # mermaid engine + flowchart.grammar + CLIs
├── tools/lrgen/               # build-time LR parser generator
├── tests/
│   ├── markdown/              # CommonMark spec conformance suite (C++)
│   ├── mermaid/               # mermaid lexer/parse/lower + golden tests (C++)
│   └── test-files/            # test data: markdown spec json, mermaid goldens
├── mermaid-utils/             # Node tool to generate mermaid golden files
├── web/                       # WebAssembly live demo
└── docs/                      # full design specification
```

See [`docs/index.md`](docs/index.md) for the complete design documentation.
