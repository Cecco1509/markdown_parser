# 1. Project structure

← [Index](index.md) | Next: [2. Data types and node structures](02_data_types.md) →

---

```
markdown_parser/
├── CMakeLists.txt                       # top-level build config
├── README.md
│
├── include/
│   ├── markdown_parser/
│   │   ├── core/
│   │   │   ├── Types.hpp                # NodeType, InlineType, BlockData, InlineData
│   │   │   ├── BlockNode.hpp            # BlockNode
│   │   │   ├── InlineNode.hpp           # InlineNode, Delimiter, BracketEntry
│   │   │   └── NodeVisitor.hpp          # visitor interface implemented by renderers
│   │   ├── parser/
│   │   │   ├── ScannedLine.hpp          # per-line scan (indent, tabs, blankness)
│   │   │   ├── SpineHandler.hpp         # phase 1: block tree
│   │   │   ├── InlineParser.hpp         # phase 2: inline tree
│   │   │   ├── block_rules.hpp          # continuation/open/close predicates
│   │   │   ├── commonmark_constants.hpp
│   │   │   └── parser.hpp               # parse(source, renderer) entry point
│   │   ├── renderer/
│   │   │   ├── HtmlRenderer.hpp
│   │   │   ├── HtmlRendererDebug.hpp
│   │   │   ├── HtmlRendererFactory.hpp
│   │   │   ├── JsonRenderer.hpp         # mdast-conformant JSON
│   │   │   └── renderer_concept.hpp     # `Renderer` C++20 concept
│   │   ├── handlers/
│   │   │   └── HandlerRegistry.hpp      # fenced-block handlers (mermaid, math)
│   │   └── utils/
│   │       ├── entities.hpp             # HTML entity decoding
│   │       ├── string_utils.hpp         # escapes, HTML/URL escaping, line init
│   │       └── unicode_fold.hpp         # case folding for reference labels
│   └── mermaid/                         # standalone mermaid engine headers
│
├── src/
│   ├── main.cpp                         # CLI: --json / --debug / --parse-mmd
│   ├── wasm_bindings.cpp                # Emscripten bindings for the web demo
│   ├── markdown_parser/
│   │   ├── parser/{ScannedLine,SpineHandler,InlineParser,block_rules}.cpp
│   │   ├── renderer/{HtmlRenderer,HtmlRenderDebug,HtmlRendererFactory,JsonRenderer}.cpp
│   │   ├── handlers/{HandlerRegistry,MermaidHandler,MathHandler}.cpp
│   │   └── utils/{entities,string_utils,unicode_fold}.cpp
│   └── mermaid/                         # mermaid engine + flowchart.grammar + CLIs
│
├── tools/lrgen/                         # build-time LR parser generator (mermaid)
│
├── tests/
│   ├── CMakeLists.txt
│   ├── markdown/
│   │   ├── test_commonmark_spec.cpp     # HTML conformance vs the spec
│   │   ├── test_json_mdast.cpp          # JSON conformance vs remark
│   │   ├── commonmark_spec_case.hpp     # spec fixture loader
│   │   └── case_report.hpp              # shared failure-report formatting
│   ├── mermaid/                         # mermaid lexer/parse/lower/golden tests
│   └── test-files/
│       ├── markdown/                    # commonmark_spec.json + *_mdast.json
│       └── mermaid/                     # .mmd + .ast.json + .svg goldens
│
├── markdown-utils/                      # Node: generates mdast goldens via remark
├── mermaid-utils/                       # Node: generates mermaid goldens
├── web/                                 # WebAssembly live demo
└── docs/                                # this documentation (legacy/ = old spec)
```

## Layering

```
core/  ←  parser/  ←  renderer/
   ↑                     ↑
   └──── utils/ ─────────┘
```

`core/` depends on nothing else. `parser/` builds the tree from source;
`renderer/` only consumes it. **Renderers never call into the parser** — the one
shared dependency is `utils/`. This is what keeps the AST render-neutral (see
[Index](index.md#design-principle-a-render-neutral-ast)).

The `Renderer` concept in `renderer_concept.hpp` is the whole contract:

```cpp
template <typename T>
concept Renderer = requires(T r, const BlockNode &node) {
    { r.render(node) } -> std::convertible_to<std::string>;
};
```

`parse()` is templated on it, so adding an output format means writing one class
— no parser changes.

## CMake targets

| Target | Kind | Contents |
|---|---|---|
| `md_parser` | static lib | parser + renderers + utils |
| `mermaid` | static lib | mermaid engine (+ generated flowchart parser) |
| `md_parser_bin` | exe | the CLI demo |
| `mermaid_ast`, `mermaid_svg` | exe | mermaid-only CLIs |
| `markdown_tests` | exe | `CommonMarkSpecTest` (HTML) + `JsonMdastTest` (JSON) |
| `mermaid_tests` | exe | mermaid unit + golden tests |
| `markdown_parser_wasm` | exe | Emscripten build → `web/dist/` |

GoogleTest and nlohmann/json are fetched by CMake (`FetchContent`); nothing is
vendored. The mermaid flowchart parser is **generated at build time** by
`tools/lrgen` from `src/mermaid/flowchart.grammar` and is not committed.

Handler sources (`MermaidHandler`, `MathHandler`) are linked directly into
executables rather than through `md_parser`, so their static-initializer
self-registration is not dropped by the linker.

---

← [Index](index.md) | Next: [2. Data types and node structures](02_data_types.md) →
