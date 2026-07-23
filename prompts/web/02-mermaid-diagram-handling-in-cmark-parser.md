# Mermaid diagram handling in cmark parser

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `4fe196d8-6695-4852-8fdd-6390c7beb076`  
**Created:** 2026-06-02  
**Messages:** 8 (4 prompts)

**Related commits:** Early Mermaid orientation — no direct commit; native Mermaid support came later.

- `90eea37 Refactor code to handle mermaid code blocks`

---

## ▸ Prompt — 2026-06-02 12:36

> How is mermaid handled by the cmark parser, it is handled at all or is just rendered as a fenced block with language "mermaid"?

The CommonMark spec (which cmark implements) has no concept of diagram languages — it only defines fenced code blocks with an optional **info string** (the language tag). So cmark itself treats a mermaid block like any other fenced code block:

````markdown
```mermaid
graph TD
  A --> B
```
````

cmark parses this as a `code_block` AST node with:
- `info` = `"mermaid"`
- `literal` = the raw diagram source

That's it. cmark renders it as:

```html
<pre><code class="language-mermaid">graph TD
  A --> B
</code></pre>
```

**Mermaid rendering is always a client-side concern.** The typical pipeline is:

1. cmark (or any CommonMark parser) outputs the `<pre><code class="language-mermaid">` HTML as above.
2. The **browser** loads the [Mermaid.js](https://mermaid.js.org/) library.
3. Mermaid.js scans the DOM for elements matching `.language-mermaid` (or `<pre class="mermaid">` depending on version/config) and replaces them with rendered SVGs.

Some platforms (GitHub, GitLab, Notion, Obsidian, etc.) handle step 3 server-side or via a post-processing step, but the parser itself is oblivious to it.

**If you're using cmark programmatically** and want to intercept mermaid blocks, you'd hook into the AST before rendering — e.g. walk nodes looking for `code_block` nodes whose `info` field equals `"mermaid"`, and emit `<div class="mermaid">...</div>` instead of the default `<pre><code>` wrapping.

## ▸ Prompt — 2026-06-02 12:43

> Does Mermaid.js output only html content after interpreting the raw-content of a fenced code block node with language equal to "mermaid"?

No — Mermaid.js outputs **SVG**, not generic HTML. Specifically, it generates an `<svg>` element with all the diagram geometry encoded as SVG shapes, paths, text nodes, etc.

For example, a simple graph like:

```
graph TD
  A --> B
```

produces something like:

```svg
<svg id="mermaid-..." xmlns="http://www.w3.org/2000/svg" ...>
  <g>
    <g class="node">...</g>
    <g class="edgePath">...</g>
  </g>
</svg>
```

SVG is technically valid XML and embeddable in HTML, but Mermaid's output is specifically an SVG document fragment, not prose HTML elements like `<div>`, `<p>`, `<table>`, etc.

**A few nuances:**

- Mermaid may also wrap the `<svg>` in a `<div class="mermaid">` or similar container depending on how you invoke it (the v9/v10 API changed some of this).
- Some diagram types (notably `gantt` in older versions) used to produce HTML tables, but that was largely phased out in favor of SVG-only output.
- When called via `mermaid.render()` (the programmatic API), it returns the raw SVG string directly, with no wrapping HTML — giving you a clean SVG fragment you can inject wherever you want.

So the short answer: **pure SVG**, which you can embed inline in an HTML document, save as a `.svg` file, or pipe into something like `cairosvg` or Inkscape for PDF/PNG conversion.

## ▸ Prompt — 2026-06-02 12:51

> are there some test suits to test a mermaid interpreter? like the test suit provided by common mark
>
> I want to integrate mermaid block interpretation natively without handling it to an external js library

This is a great question, and the honest answer is: **Mermaid has no equivalent to the CommonMark spec test suite.**

CommonMark's test suite is a formal, language-agnostic JSON file of input/output pairs that any conforming implementation can run against. Mermaid has nothing like that.

**What Mermaid does have:**

- Its own internal test suite in the [mermaid GitHub repo](https://github.com/mermaid-js/mermaid), written in Vitest/Jest. These test the JavaScript implementation itself (parser + renderer together), not a separable grammar spec. They're not designed to validate a third-party implementation.
- Some snapshot tests that check SVG output, but those are tightly coupled to the JS renderer internals.

**The deeper problem for native integration:**

Mermaid's "language" is not formally specified. There is no BNF grammar document, no railroad diagrams, no spec analogous to CommonMark's. The source of truth *is* the JavaScript implementation. Each diagram type (flowchart, sequence, gantt, class, ER, etc.) has its own grammar defined in **Nearley** or **ANTLR** `.jison`/`.langium` files inside the repo — so the closest thing to a spec is those grammar files:

- `src/diagrams/flowchart/parser/flow.jison`
- `src/diagrams/sequence/parser/sequenceDiagram.jison`
- etc.

**Practical options if you want native interpretation:**

1. **Extract and reuse the grammar files** — the `.jison` files are the most authoritative description of each diagram's syntax. You could port them to a parser generator in your target language.
2. **Use the JS engine via embedding** — e.g. call Mermaid via a headless JS runtime (QuickJS, Deno, Node) from your native code, getting SVG back over a pipe/IPC. Avoids reimplementing the grammar but keeps it "native" from the user's perspective.
3. **Target a subset** — flowchart and sequence diagrams are the most used and most stable; their grammars are relatively self-contained.
4. **Watch the Langium migration** — Mermaid has been migrating parsers to [Langium](https://langium.org/), which produces more formal, readable grammar definitions. Those are easier to port than raw jison files.

So in short: you're largely on your own for test coverage, and your best bet for correctness is treating the jison/langium grammar files as the spec and building integration tests against the reference JS implementation's output.

## ▸ Prompt — 2026-06-04 07:47

> Are there any mermaid interpreters in cpp? or any other public source code I can use as reference?

Honest answer: **there is no known, mature, standalone Mermaid interpreter in C++.** It's a fairly niche undertaking. Here's what exists or comes close:

---

**Closest things in C/C++:**

- **[mermaid-cpp](https://github.com/gtv-mermaid/mermaid-cpp)** — there have been small experimental repos with this kind of name on GitHub, but none that reached production quality or full diagram coverage. Worth searching GitHub directly for the current state, as things may have changed.
- **Graphviz (C library)** — not Mermaid, but if your goal is *diagram rendering to SVG*, Graphviz's `libgvc` is a mature C library. Mermaid's flowchart syntax is actually loosely inspired by Graphviz's DOT language, so you could translate a subset of Mermaid flowcharts to DOT and let Graphviz render them.

---

**More useful references in other languages:**

- **[mermaid-go](https://github.com/dreampuf/mermaid.go)** — Go wrapper, but calls the JS engine, not a native reimplementation.
- **[mermaid-py](https://github.com/mgaitan/sphinxcontrib-mermaid)** — Python, again mostly a wrapper.
- **Kroki** (Java/Kotlin) — a diagram rendering server that supports Mermaid among many others, but again delegates to the JS engine.

None of these are native reimplementations — they all shell out to the JS library.

---

**The most useful reference for a native implementation:**

The grammar files inside the Mermaid JS repo itself are your best spec. Specifically:

- For **Langium-based diagrams** (newer): `packages/mermaid/src/diagrams/*/` — the `.langium` grammar files are clean and readable, essentially EBNF.
- For **jison-based diagrams** (older, but flowchart/sequence still use these): `.jison` files, which are Bison-style grammars, very portable to any LALR parser generator including Bison/Flex itself.

The jison files in particular are almost directly usable with **Bison + Flex** in C/C++ with minimal adaptation.

---

**Suggested approach for C++:**

1. Pick a target diagram type (flowchart is the most valuable).
2. Take its `.jison` grammar from the Mermaid repo.
3. Adapt it for **Bison/Flex** — the syntax is close enough that it's mostly mechanical work.
4. Build an AST, then write an SVG emitter walking the AST.
5. Use the reference JS implementation to generate expected SVG outputs for your test cases.

That's essentially the only realistic path to a correct, maintainable native C++ Mermaid interpreter right now.

