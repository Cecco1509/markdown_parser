# Restructuring include/ and src/ into directories

**Theme:** Jun 14 refactor day

**Source transcript:** `1365d948-53d3-4761-826b-19b783ca2c02.jsonl`  
**Records:** 978–1056

**Dates:** 2026-06-14 15:05 → 2026-06-14 15:19  
**User turns:** 5

**Related commits:**

- `57b64a5 Code structural refactor`
- `511f0f2 refactor SpineHandler and Inline Parser raw pointers usage`
- `022af05 string assignment optimizations`

---

## ▸ Prompt — 2026-06-14 15:05

> It is becoming complicated for me to navigate all the files, I'd like to structure them with directories, both include and src

## ▸ Prompt — 2026-06-14 15:06

> It is becoming complicated for me to navigate all the files, I'd like to structure them with directories, both include and src, propose a structure

Here's a proposed structure grouped by responsibility:

```
include/markdown_parser/
├── core/                          # AST nodes, types, visitor interface
│   ├── Types.hpp
│   ├── BlockNode.hpp
│   ├── InlineNode.hpp
│   └── NodeVisitor.hpp
│
├── parser/                        # Parsing pipeline
│   ├── parser.hpp                 # public parse() entry point
│   ├── SpineHandler.hpp
│   ├── InlineParser.hpp
│   ├── ScannedLine.hpp
│   ├── block_rules.hpp
│   └── commonmark_constants.hpp
│
├── renderer/                      # Output renderers + concept
│   ├── renderer_concept.hpp
│   ├── HtmlRenderer.hpp
│   ├── HtmlRendererDebug.hpp
│   ├── HtmlRendererFactory.hpp
│   └── JsonRenderer.hpp
│
├── handlers/                      # Handler registry (fenced code extensions)
│   └── HandlerRegistry.hpp
│
└── utils/                         # Shared utilities
    ├── string_utils.hpp
    ├── entities.hpp
    └── unicode_fold.hpp

src/
├── core/                          # (empty — nodes are header-only structs)
│
├── parser/
│   ├── SpineHandler.cpp
│   ├── InlineParser.cpp
│   ├── ScannedLine.cpp
│   ├── block_rules.cpp
│   └── entities.cpp               # could move here or keep in utils
│
├── renderer/
│   ├── HtmlRenderer.cpp
│   ├── HtmlRenderDebug.cpp
│   ├── HtmlRendererFactory.cpp
│   └── JsonRenderer.cpp
│
├── handlers/
│   ├── HandlerRegistry.cpp
│   ├── MathHandler.cpp
│   └── MermaidHandler.cpp
│
├── utils/
│   ├── string_utils.cpp
│   ├── entities.cpp
│   └── unicode_fold.cpp
│
├── main.cpp
└── wasm_bindings.cpp
```

Key decisions in this proposal:
- **`core/`** holds the AST — everything else depends on it, nothing in it depends on anything else
- **`parser/`** groups the parsing pipeline; `parser.hpp` stays here as the public entry point
- **`renderer/`** groups all output formats plus the concept that defines their contract
- **`handlers/`** groups the registry and the self-registering handlers together (they're tightly coupled)
- **`utils/`** holds pure utilities with no parser-specific knowledge
- `main.cpp` and `wasm_bindings.cpp` stay at the root of `src/` — they're executables, not library code

Include paths would change from `"markdown_parser/HtmlRenderer.hpp"` to `"markdown_parser/renderer/HtmlRenderer.hpp"`. Want to proceed with this?

## ▸ Prompt — 2026-06-14 15:08

> Would it make sense to add markdown_parser folder in src, containing all the directories?

Yes, it makes sense for symmetry with `include/markdown_parser/`. The structure would be:

```
include/markdown_parser/
├── core/
├── parser/
├── renderer/
├── handlers/
└── utils/

src/
├── markdown_parser/               # mirrors include/markdown_parser/
│   ├── parser/
│   ├── renderer/
│   ├── handlers/
│   └── utils/
├── main.cpp
└── wasm_bindings.cpp
```

`main.cpp` and `wasm_bindings.cpp` stay outside because they're entry points, not library internals — the `markdown_parser/` directory in `src/` would contain only the library implementation files.

The tradeoff: it's slightly more typing to navigate (`src/markdown_parser/renderer/` vs `src/renderer/`) but the mirror structure makes it immediately obvious where the `.cpp` for any given `.hpp` lives. In larger projects this is the standard convention.

Want to proceed with this?

## ▸ Prompt — 2026-06-14 15:09

> Yes

I'll move all files, update includes, and update CMakeLists in one shot.

Now update all `#include` paths across every file:

sed is treating the newline-separated list as a single path. Let me use Python instead:

Now update CMakeLists.txt with the new paths:

Some headers inside `include/markdown_parser/parser/` and `include/markdown_parser/renderer/` include siblings with bare names like `"BlockNode.hpp"` instead of the full path. The Python script only replaced quoted paths prefixed with `markdown_parser/` — intra-package relative includes were left unchanged. Let me fix them:

Clean build. Final structure:

```
include/markdown_parser/
├── core/       — Types, BlockNode, InlineNode, NodeVisitor
├── parser/     — parser.hpp, SpineHandler, InlineParser, ScannedLine, block_rules, commonmark_constants
├── renderer/   — renderer_concept, HtmlRenderer, HtmlRendererDebug, HtmlRendererFactory, JsonRenderer
├── handlers/   — HandlerRegistry
└── utils/      — string_utils, entities, unicode_fold

src/
├── markdown_parser/
│   ├── parser/     — SpineHandler, InlineParser, ScannedLine, block_rules
│   ├── renderer/   — HtmlRenderer, HtmlRenderDebug, HtmlRendererFactory, JsonRenderer
│   ├── handlers/   — HandlerRegistry, MathHandler, MermaidHandler
│   └── utils/      — string_utils, entities, unicode_fold
├── main.cpp
└── wasm_bindings.cpp
```

## ▸ Prompt — 2026-06-14 15:19

> Let's recap the mermaid solutions, expecially the one that involves parsing mermaid directly in cpp

From the earlier conversation, three paths were considered:

---

**Path A — `<div class="mermaid">` wrapper (current implementation)**
C++ identifies the fenced code block with lang `mermaid`, wraps the content unescaped in `<div class="mermaid">...</div>`, and does nothing else. Mermaid.js in the browser reads those divs and renders the SVG entirely client-side.

- No C++ rendering logic at all
- Requires mermaid.js loaded in the browser
- Works perfectly for the web target, useless for CLI output

---

**Path B — Native C++ parsing of Mermaid syntax**
C++ would parse the Mermaid diagram definition (e.g. `graph TD; A-->B`) and produce an SVG directly, with no browser dependency.

Why this is extremely hard:
- SVG requires knowing **exact pixel sizes of text labels** to position boxes and edges correctly. That requires a font metrics system (glyph widths, line heights) which only exists inside a browser DOM or a native GUI toolkit
- `mmdc` (the official Mermaid CLI) sidesteps this by running a headless Chromium instance — it literally uses a real browser internally to measure fonts and render
- A pure C++ implementation would need to either bundle a font metrics table (incomplete, locale-dependent) or produce fixed-size boxes (ugly, wrong for long labels)

Conclusion: feasible to parse the syntax, not feasible to produce a correct SVG without a rendering engine.

---

**Path C — Shell out to `mmdc`**
C++ calls `mmdc` as a subprocess, pipes the diagram source in, gets SVG back, and inlines it into the HTML output.

- Correct output — mmdc handles font metrics via headless Chromium
- Adds a hard runtime dependency on Node.js + mmdc being installed
- Slow (spawns a process per diagram)
- Breaks the WASM target entirely (no subprocess from browser)
- Makes the CLI output self-contained (no mermaid.js needed in the HTML)

---

**Why Path A was chosen**: it's the only one that works cleanly for both the CLI (browser opens the output) and the WASM/web target, with zero C++ rendering complexity and correct output by design.

