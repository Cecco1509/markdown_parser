# Markdown Parser with Mermaid Diagram Support

A Markdown parser written in **C++20** that implements the
[CommonMark v0.31.2](https://spec.commonmark.org/0.31.2/) specification and adds
first-class support for **Mermaid** flowchart code blocks. ` ```mermaid ` fences
are detected during parsing and tagged as distinct diagram nodes in the output,
and an integrated Mermaid engine can parse, lay out, and render those diagrams to
standalone **SVG** — no browser or JavaScript required.

The project produces a structured AST that can be emitted as **HTML** or as a
**JSON AST**, and ships with a live in-browser demo compiled to **WebAssembly**.

### ▶ [Try it live](https://cecco1509.github.io/markdown_parser/web/)

A two-pane Markdown editor running the parser **entirely client-side** as
WebAssembly — type Markdown on the left, see rendered HTML (with live Mermaid
diagrams) on the right. Source in [`web/`](web/).

> _AP course project B02 — AI-Assisted Markdown Parser with Mermaid Diagram Support._

---

## Features

- **Full CommonMark v0.31.2** block and inline parsing: ATX/Setext headings,
  ordered/bullet lists, fenced & indented code blocks, blockquotes, thematic
  breaks, links & images (inline, reference, autolinks), emphasis/strong, code
  spans, HTML blocks, entities, and the tab-expansion rules.
- **Mermaid detection & tagging** — ` ```mermaid ` blocks become dedicated
  diagram nodes rather than plain code blocks.
- **Native Mermaid rendering** — a self-contained flowchart pipeline
  (lex → parse → lower → layout → SVG) produces inline SVG for each diagram,
  targeting the Mermaid 11 flowchart subset.
- **Two output formats** — HTML, or a JSON AST conforming to
  [mdast](https://github.com/syntax-tree/mdast) (the remark/unified standard).
  The JSON tree is _reference-preserving_: link reference definitions stay as
  `definition` nodes and their uses as `linkReference`/`imageReference`, so the
  AST faithfully records the source rather than only the rendered result.
- **WebAssembly demo** — a live, two-pane Markdown editor running the parser
  entirely client-side.
- **Dual conformance testing** — every CommonMark spec example is checked twice:
  HTML against the spec's expected output, and JSON against the reference mdast
  produced by **remark**. ~1340 cases in total, plus Mermaid golden-file
  verification against the official Mermaid library.

---

## Architecture at a glance

The parser runs in two phases (per the CommonMark reference algorithm):

```
source ──▶ ScannedLine ──▶ SpineHandler ──▶ InlineParser ──▶ Renderer ──▶ HTML / JSON
           (line scan)     (block tree)      (inline tree)   (Html/Json)
```

The AST is deliberately **render-neutral**: normalization that is specific to an
output format (e.g. code-span line endings becoming spaces, or resolving a
reference link to a URL) happens in the renderer, not the parser. This is what
lets the same tree produce both spec-exact HTML and reference-preserving mdast.

The Mermaid engine is an independent module; the only coupling point is the
Markdown fence handler that dispatches ` ```mermaid ` blocks into it:

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

| Target                  | Description                                      |
| ----------------------- | ------------------------------------------------ |
| `md_parser_bin`         | Markdown → HTML/JSON CLI (the main demo program) |
| `mermaid_ast`           | Parse a `.mmd` file and print its AST            |
| `mermaid_svg`           | Parse + layout + render a `.mmd` file to SVG     |
| `md_parser` / `mermaid` | Static libraries                                 |

---

## Running the demo (CLI)

```bash
# Render Markdown to HTML
./build/md_parser_bin input.md

# Render Markdown to a JSON AST
./build/md_parser_bin --json input.md

# Render Markdown to HTML, rendering mermaid blocks to inline SVG
./build/md_parser_bin --parse-mmd input.md
```

Options:

| Flag           | Effect                                                                                |
| -------------- | ------------------------------------------------------------------------------------- |
| _(none)_       | Emit HTML; mermaid blocks stay as `<pre><code>` code blocks                           |
| `--json`       | Emit the JSON AST instead of HTML                                                     |
| `--parse-mmd`  | Render ` ```mermaid ` blocks to inline `<svg>`                                        |
| `--parse-math` | Render math blocks                                                                    |
| `--debug`      | Emit annotated/debug HTML                                                             |

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
{
  "type": "root",
  "children": [
    {
      "type": "heading",
      "depth": 1,
      "children": [{ "type": "text", "value": "Hi" }]
    },
    {
      "type": "paragraph",
      "children": [
        { "type": "text", "value": "Some " },
        { "type": "strong", "children": [{ "type": "text", "value": "bold" }] },
        { "type": "text", "value": " text." }
      ]
    },
    {
      "type": "code",
      "lang": "mermaid",
      "meta": null,
      "value": "graph TD\nA-->B"
    }
  ]
}
```

The Mermaid block surfaces as a `code` node tagged `"lang":"mermaid"` — the
spec-correct mdast representation — so downstream tools can pick diagrams out of
the tree. With `--parse-mmd`, that same node is instead rendered to an inline
`<div class="mermaid"><svg …>…</svg></div>` in the HTML output.

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

**Live at → https://cecco1509.github.io/markdown_parser/web/**

The parser compiles to WebAssembly for a live browser demo — a two-pane editor
that re-parses on every keystroke, with toggles for Mermaid and math rendering.
The page is served from [`web/`](web/); CI rebuilds the `.wasm` on every push to
`main` and commits it to [`web/dist/`](web/dist/), so the live demo always tracks
the latest parser.

To build it yourself, use [Emscripten](https://emscripten.org/) (`emsdk` activated):

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

## AI usage: chat-to-commit map

This project was built with AI assistance; the full prompt record is in [`prompts/`](prompts/) (indexed by [`prompts/README.md`](prompts/README.md)). Each chat file names the commits it produced, and the tables below map every conversation to that code.

Every chat file lists, in its header, the commits it produced (**Related commits:**).
The table below gathers that in one place, so a reader can go straight from a
conversation to the code it resulted in. Some early conversations were learning or
design only and produced no direct commit; separately, a number of bug-fix and
refactor commits have no saved chat at all (they are listed in the report appendix,
"Commits without a recorded chat").

### Early design and learning (claude.ai)

| Chat | File | Commits |
| --- | --- | --- |
| What is Markdown (.md) and whe | `prompts/web/01-what-is-markdown-md.md` | _none (research)_ |
| Mermaid diagram handling in cmark parser | `prompts/web/02-mermaid-diagram-handling-in-cmark-parser.md` | `90eea37` |
| Smart pointers in C++ | `prompts/web/03-smart-pointers-in-cpp.md` | `18303cc`, `9a057dd` |
| C++ project structure and file conventions | `prompts/web/04-cpp-project-structure-and-file-conventions.md` | `834f683`, `25b78a8` |
| Markdown parser architecture and component design | `prompts/web/05-markdown-parser-architecture-and-component-design.md` | `053d360`, `3e30935`, `6ae7eeb` |
| Markdown to HTML and JSON specification | `prompts/web/06-markdown-to-html-and-json-specification.md` | `4dfd2cc`, `f42de5e` |
| GitHub workflows and automated testing | `prompts/web/07-github-workflows-and-automated-testing.md` | `0e2b8a7`, `ceba43f` |
| HTML rendering rules for list items | `prompts/web/08-html-rendering-rules-for-list-items.md` | `8722ae1`, `817995f`, `7722ba9` |
| Deploying C++ parser to web | `prompts/web/09-deploying-cpp-parser-to-web.md` | `011b672`, `523342e` |
| CommonMark block quote code parsing | `prompts/web/10-commonmark-block-quote-code-parsing.md` | `0e17dec`, `378f572`, `c8c27c8` |
| Markdown blockquote indentation spacing | `prompts/web/11-markdown-blockquote-indentation-spacing.md` | `4670163`, `56081cb`, `8c345b6` |
| Hosting a parser on GitHub pages | `prompts/web/12-hosting-a-parser-on-github-pages.md` | `dc0da1a`, `3f21252`, `d58bfb5` |
| Mermaid block types | `prompts/web/13-mermaid-block-types.md` | `42f53a4`, `0209274` |

### Development sessions (Claude Code)

| Session | File | Commits |
| --- | --- | --- |
| Mermaid design debate — how (and where) to render | `prompts/01-mermaid-rendering-approach.md` | `0b014d7`, `90eea37` |
| Fence handlers: visitor override, factory, Meyers singleton | `prompts/02-fence-handlers-factory-singleton.md` | `90eea37`, `4638baa` |
| Project-wide namespace unification | `prompts/03-namespace-unification.md` | `466541d`, `1e25aa6` |
| Templating parse() and defining the Renderer concept | `prompts/04-renderer-concept-and-templates.md` | `57b64a5`, `3ac6500` |
| Restructuring include/ and src/ into directories | `prompts/05-directory-restructure.md` | `57b64a5`, `511f0f2`, `022af05` |
| Pivot to Path B — native C++ Mermaid parsing | `prompts/06-mermaid-native-parsing-pivot.md` | `0e7421b`, `42f53a4`, `0209274` |
| Flowchart grammar and the decision to generate a parser | `prompts/07-grammar-and-generator-design.md` | `42f53a4`, `0e7421b` |
| LR(1)/LALR item sets and the lrgen generator | `prompts/08-lr1-generator-implementation.md` | `4ab3331` |
| AST types, token adapter, and the lowering stage | `prompts/09-mermaid-ast-and-lowering.md` | `c4473cb` |
| Golden-file verification against the official Mermaid library | `prompts/10-golden-verification-vs-mermaid.md` | `9dff979`, `106d944`, `89daa4f` |
| Sugiyama layout: ranking, ordering, coordinates | `prompts/11-layout-sugiyama.md` | `c4473cb`, `ab77280` |
| WASM integration, emscripten::val and text measurement | `prompts/12-wasm-integration-and-text-measurement.md` | `4714bd7`, `46e5652`, `7526636` |
| SVG shapes, edge routing and layout debugging | `prompts/13-svg-shapes-and-layout-debugging.md` | `80baf5c`, `0829719`, `ab77280`, `08e87f9`, `bd9983e` |
| Delivery review and test-folder restructure | `prompts/14-delivery-prep-and-test-structure.md` | `495e860` |
| The --json flag and aligning the AST with mdast | `prompts/15-json-output-and-mdast-alignment.md` | `4dfd2cc`, `5228dc0`, `f42de5e` |
| Link reference definitions as first-class nodes | `prompts/16-link-definitions-and-references.md` | `0166741`, `6d31019` |
| Who normalizes: parser or renderer? | `prompts/17-normalization-parser-vs-renderer.md` | `f2e0cf3`, `60c84d7` |
| Shared test reporting, README and docs rewrite | `prompts/18-test-suite-refactor-and-docs.md` | `c189134`, `31caff8`, `0b67771` |
| Is a leak possible? Ownership audit of the node tree | `prompts/19-memory-ownership-and-leak-investigation.md` | `0b67771` |
| Profiling and fixing the O(n^2) bracket/label path | `prompts/20-quadratic-bracket-path.md` | `8c21e84` |
| Recursive destruction, nesting caps and stack size | `prompts/21-recursive-destructor-and-nesting-caps.md` | `8c21e84` |

