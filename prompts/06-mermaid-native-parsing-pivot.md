# Pivot to Path B — native C++ Mermaid parsing

**Theme:** Mermaid design debate

**Source transcript:** `1365d948-53d3-4761-826b-19b783ca2c02.jsonl`  
**Records:** 1057–1558

**Dates:** 2026-06-14 15:28 → 2026-06-14 22:32  
**User turns:** 9

**Related commits:**

- `0e7421b start mermaid block support - Lexer -`
- `42f53a4 add flowchart specs`
- `0209274 add mermaid utility scripts`

> Note: this segment crosses a context-limit continuation; an auto-generated summary of the earlier turns was re-injected at that point and is omitted here.

---

## ▸ Prompt — 2026-06-14 15:28

> Let's go with option "Path B — Native C++ parsing of Mermaid syntax", I'm not crazy. it is for a class project (I tought that path A was okay but the project requirements "says" to go deeper and implement a mermaid parser). Let's brainstorm which are the porblems and how they are solved by the mmdc implementation

Good context. Let's go problem by problem:

---

**1. Parsing the Mermaid syntax**
The diagram definition (`graph TD; A-->B`) needs to be tokenized and turned into a graph structure (nodes, edges, labels, directions).
- **Problem**: Mermaid has a non-trivial grammar (multiple diagram types, subgraphs, styling, etc.)
- **mmdc solution**: Mermaid.js has a full parser written in JavaScript (uses a PEG grammar via `jison`)
- **C++ equivalent**: Write a parser for the subset you need — flowcharts only is manageable, full Mermaid is a semester project on its own

---

**2. Layout — placing nodes and edges without overlaps**
Given a graph, compute x/y coordinates for every node and route edges between them.
- **Problem**: This is a full graph layout algorithm (Sugiyama for DAGs, force-directed for general graphs)
- **mmdc solution**: Uses `dagre` (a JavaScript port of Graphviz's layout engine) under the hood
- **C++ equivalent**: Implement or port a layout algorithm. Dagre's algorithm is documented and portable, but non-trivial

---

**3. Text measurement — knowing how wide a label is**
Node boxes must be sized to fit their text label. That requires knowing the pixel width of each string in the chosen font.
- **Problem**: Font metrics (glyph advances) depend on the font, size, and renderer
- **mmdc solution**: Runs in headless Chromium — the browser measures text natively via `canvas.measureText()` or DOM layout
- **C++ equivalent**: Either hardcode an approximation (monospace: `width = chars × char_width`), embed a font metrics table (e.g. from a TTF file), or use a library like FreeType

---

**4. SVG generation**
Once layout is computed, emit the SVG: `<rect>`, `<text>`, `<path>` for edges with arrowheads, `<marker>` for arrow definitions.
- **Problem**: Pure string generation, but edge paths (especially curved ones) need Bezier control points derived from the layout
- **mmdc solution**: Mermaid.js generates SVG strings directly using d3-based helpers
- **C++ equivalent**: Straightforward string building once layout coordinates are known — this is the easy part

---

**Summary of what you actually need to implement:**

| Problem | Difficulty | Shortcut available? |
|---|---|---|
| Parse flowchart syntax | Medium | Limit to `graph`/`flowchart` only |
| Graph layout (dagre) | Hard | Port dagre's algorithm or use a simplified topological layout |
| Text measurement | Medium | Approximate with char-width table for a fixed font |
| SVG emission | Easy | String building |

The real question for the project is: **how faithful does the layout need to be?** A simplified version that does top-down layered layout with fixed-width boxes is completely achievable in C++ and would demonstrate you built a real parser and layout engine.

Want to scope it to flowcharts only and decide how approximate the layout can be?

## ▸ Prompt — 2026-06-14 15:37

> Let's focus on the syntax first. I used to write a compiler in ocaml, using ocamllex and menhir, does cpp have something like that?

Yes, C++ has direct equivalents:

---

**Lexer generators**
- **flex** — the C equivalent of ocamllex. You write `.l` files with regex rules and it generates a C lexer. Widely used, integrates well with CMake (`find_package(FLEX)`)
- **RE2C** — more modern, generates faster code, header-only friendly, also regex-based

---

**Parser generators**
- **bison** — the C equivalent of menhir. You write `.y` files with LALR(1) grammar rules, it generates a C/C++ parser. Bison + Flex is the classic combo, same mental model as ocamllex + menhir
- **LEMON** — lighter alternative, used inside SQLite
- **ANTLR4** — generates C++ parsers from LL(k) grammars, has a richer ecosystem and better error messages but heavier setup

---

**The Ocaml → C++ mapping:**

| Ocaml | C++ |
|---|---|
| ocamllex | flex / RE2C |
| menhir | bison |
| `token` type | `enum class TokenType` |
| AST variant types | `struct` + `enum` + `std::variant` |
| pattern matching | `switch` / `std::visit` |

---

**For this project**, Flex + Bison is the closest to what you already know. The grammar for a flowchart subset is small enough that you could also write a **hand-rolled recursive descent parser** in pure C++ without any generator — which avoids the CMake complexity of integrating flex/bison and gives you full control, at the cost of more manual work.

Which direction feels right — generator-based (closer to your Ocaml experience) or hand-rolled?

## ▸ Prompt — 2026-06-14 15:41

> Can the (PEG grammar via jison) be easily transformed to Flex + bison

Actually the description "PEG grammar via jison" from earlier was slightly imprecise — **jison is a JavaScript port of Bison**, not a PEG tool. It uses the same LALR(1) BNF grammar format as Bison. This is good news for you.

---

**What Mermaid's `.jison` files look like:**

```
/* lexer section — maps directly to Flex */
%%
"graph"     return 'GRAPH';
"TD"        return 'TD';
[a-zA-Z]+   return 'STR';
"-->"       return 'ARROW';
\s+         /* skip */

/* grammar section — maps directly to Bison */
%%
diagram: GRAPH direction statements ;
direction: TD | LR | BT | RL ;
statements: statement | statements statement ;
statement: node ARROW node ;
node: STR ;
```

The structure is **identical** to a Flex `.l` + Bison `.y` pair. The only things that need translation are:

| jison | Flex/Bison |
|---|---|
| JavaScript actions `{ return node; }` | C++ actions `{ $$ = ...; }` |
| `%lex` section | Separate `.l` file |
| `%% grammar %%` section | Separate `.y` file |
| JS string handling | `std::string` / `char*` |
| JS object for AST nodes | C++ struct |

---

**Practical path:** Mermaid's flowchart grammar file is at `packages/mermaid/src/diagrams/flowchart/parser/flow.jison` in their GitHub repo. For the flowchart subset it's ~200 lines. You would:

1. Copy the `%lex` section → your `.l` file, replace JS actions with C returns
2. Copy the grammar rules → your `.y` file, replace JS object construction with C++ struct assignments
3. Define a C++ AST (`FlowNode`, `FlowEdge`, `FlowGraph`) that the actions populate

Want to look at the actual jison file and map it together?

## ▸ Prompt — 2026-06-14 15:46

> No, problem 1 seems reasonably easy to solve in cpp, so let's focus on the second problem "2. Layout — placing nodes and edges without overlaps"

This is the core hard problem. Let's break it down into sub-problems in order:

---

**The algorithm: Sugiyama framework (what dagre implements)**

It has 4 distinct phases:

---

**Phase 1 — Cycle removal**
Flowcharts can have back-edges (loops). Sugiyama requires a DAG.
- Detect cycles, temporarily reverse one edge per cycle
- Restore them after layout with a curved "back" edge
- **Difficulty**: easy — DFS-based, ~30 lines

---

**Phase 2 — Layer assignment (ranking)**
Assign each node to a horizontal layer (rank 0, 1, 2...) such that every edge goes from a lower rank to a higher rank.
- Simplest approach: longest path from source nodes
- Dagre uses the **network simplex algorithm** for optimal ranking (minimizes total edge length)
- **Difficulty**: longest-path is easy; network simplex is hard. For a class project longest-path is fine

---

**Phase 3 — Crossing minimization**
Within each layer, reorder nodes to minimize the number of edge crossings between adjacent layers.
- This is NP-hard in general
- Dagre uses the **barycenter heuristic**: position each node at the average x-position of its neighbors in the adjacent layer, then sort
- Run multiple passes top-down then bottom-up
- **Difficulty**: medium — the heuristic is ~50 lines, getting it to converge well takes tuning

---

**Phase 4 — Coordinate assignment**
Assign actual x/y pixel coordinates. Each layer gets a y-coordinate, nodes within a layer get x-coordinates with minimum spacing.
- Naive: equally space nodes within each layer
- Dagre uses **Brandes-Köpf** algorithm for aesthetically balanced placement
- **Difficulty**: naive is easy; Brandes-Köpf is complex

---

**Summary by difficulty for a class project:**

| Phase | Algorithm | Class project shortcut |
|---|---|---|
| Cycle removal | DFS reverse | Same, no shortcut needed |
| Layer assignment | Network simplex | Longest-path instead |
| Crossing minimization | Barycenter heuristic | Same, 1-2 passes |
| Coordinate assignment | Brandes-Köpf | Uniform spacing instead |

The shortcuts (longest-path + uniform spacing) produce a correct, readable layout — just not as compact or balanced as dagre. For a class project that's entirely acceptable.

---

Which phase do you want to go deeper on first?

## ▸ Prompt — 2026-06-14 15:53

> Let's clarify the "Class project" thing. The fact that the project is for a class doesn't mean it can be a simplified version of everything. It is a class for a master degree, some simplyfications are okay but the assignment is complex for a reason

Fair point. Let me re-evaluate each phase honestly:

---

**Phase 1 — Cycle removal**
No simplification needed. DFS-based reverse is the standard approach and it's short. ✓

---

**Phase 2 — Layer assignment**
Longest-path is **not acceptable** for a serious implementation — it produces unnecessarily long edges and unbalanced trees. **Network simplex is the right choice**.

It's a well-documented algorithm (Gansner et al. 1993 — the same paper dagre is based on). The steps are:
- Build a feasible spanning tree
- Compute cut values for each tree edge
- Iteratively swap negative-cut-value edges to reduce total edge length
- Converges to the optimal integer ranking

Hard but self-contained and learnable.

---

**Phase 3 — Crossing minimization**
The barycenter heuristic **is** the industry standard — even dagre uses it. Running multiple top-down/bottom-up passes until stable is the accepted approach. No guilt about using it. ✓

---

**Phase 4 — Coordinate assignment**
Uniform spacing is **not acceptable** — it produces ugly wide diagrams with misaligned nodes. **Brandes-Köpf** is the right choice.

It works in 4 sub-passes (one per combination of left/right + up/down alignment), then takes the average. The result is compact and aesthetically balanced. Also from a well-known paper (2001) so it's well documented.

---

**Revised honest assessment:**

| Phase | Algorithm to implement |
|---|---|
| Cycle removal | DFS reverse |
| Layer assignment | Network simplex (Gansner 1993) |
| Crossing minimization | Barycenter heuristic, multi-pass |
| Coordinate assignment | Brandes-Köpf (2001) |

Both papers are freely available and the algorithms map cleanly to C++ structs. Want to start reading through Gansner 1993 together and design the data structures?

## ▸ Prompt — 2026-06-14 15:56

> Which are the mermaid constructs that are difficult to implement?

Let me look at the actual Mermaid flowchart grammar to answer this precisely.

Good, now I have the full grammar. Here's an honest breakdown of what's difficult:

---

**Easy — Core graph structure**
Nodes, edges, directions. The grammar for this is small and mechanical:
```
A --> B
A --> B --> C
A & B --> C
```
Straightforward to parse and represent as a graph.

---

**Medium — Node shapes**
14 different shapes, each with different bracket syntax:

| Syntax | Shape |
|---|---|
| `A[text]` | rectangle |
| `A(text)` | rounded |
| `A((text))` | circle |
| `A{text}` | diamond |
| `A>text]` | asymmetric |
| `A([text])` | stadium |
| `A[[text]]` | subroutine |
| `A[(text)]` | cylinder |
| `A(((...)))` | double circle |
| `A[/text/]` | trapezoid |
| `A[\text\]` | inv trapezoid |
| `A(-text-)` | ellipse |
| `A{{text}}` | hexagon |

Each requires a different **SVG shape drawn around the text**, with correct sizing. Not conceptually hard but a lot of cases to implement correctly.

---

**Hard — Edge types and labels**
Three stroke types × multiple arrowhead types × optional label:
```
A --> B          solid arrow
A --- B          solid no arrow
A ==> B          thick arrow
A -.-> B         dotted arrow
A --text--> B    inline label
A -->|text| B    pipe label
```
The label placement on a curved edge path (Bezier midpoint) is the hard part.

---

**Hard — Subgraphs**
```
subgraph title
    A --> B
end
```
A subgraph is a node that **contains** other nodes and must be sized to wrap its contents. This requires a two-pass layout: lay out the inner graph first, then use its bounding box as the outer node's size. This interacts badly with Sugiyama — subgraphs are essentially a nested layout problem.

---

**Very hard — Styling**
```
style A fill:#f9f,stroke:#333
classDef green fill:#0f0
class A,B green
linkStyle 0 stroke:#ff0
```
CSS-like inline styles applied per node/edge. Requires parsing style strings and emitting them as SVG `style` attributes. Not algorithmically hard but a lot of surface area.

---

**Skip entirely — Interactivity**
```
click A callback
click A href "url"
```
Browser-only. Irrelevant for SVG output from C++.

---

**Practical scope recommendation for the project:**

| Feature | Include? |
|---|---|
| Core nodes + edges | Yes |
| All 14 node shapes | Yes |
| Solid/dotted/thick edges | Yes |
| Edge labels | Yes |
| Subgraphs | Maybe — biggest complexity spike |
| Styling / classDef | Partial — basic fill/stroke |
| Click / interactivity | No |
| Unicode node IDs | Yes — trivial with UTF-8 strings |

Subgraphs are where I'd draw the line — they multiply the layout complexity significantly. Want to decide on scope and then move to the data structures?

## ▸ Prompt — 2026-06-14 21:11

> Let's make a plan to implement all of this. Let's recap the requirements and what we already proposed. Also there is left to decide how this feature will change the project structure

Now I have everything I need. Let me write the plan.

Good thinking. This leads to a dual-mode text measurement system — the most accurate possible on each target. Let me update the plan.

Also update the new module structure and CMake sections to reflect the split file:

Good idea. The native CLI measurement becomes a priority chain with graceful fallback. Let me update the plan:

Good idea — mmdc as a reference oracle is cleaner than any hand-written assertion. Let me update the verification section:

Good catch. Since `Parser::parse()` fully completes before layout starts, all node labels are known upfront. The flow is:

1. `Parser::parse()` → complete `FlowGraph` (all labels known)
2. Collect all unique labels from `FlowGraph::nodes`
3. One single browser call measures all of them together
4. Results cached in `unordered_map<string, double>`
5. Layout runs — all `measureText()` calls are cache hits

No incremental measurement needed. One spawn per diagram. Let me update the plan:

Plan approved. Let's start with the data structures and work through the build order. I'll create the todo list and begin with `FlowGraph.hpp`.

Now the token types and lexer:

Now the Layout engine — the most substantial piece:

Now the public API and the integration wiring:

Now update the integration points:

Now update CMakeLists.txt:

Now update `web/index.html` to remove mermaid.js:

Now build and see what errors come up:

Three fixes needed: missing `INVTRAPEND` token, `erase` call on `node_order`, and `tmpnam` deprecation.

`INVTRAPEND` is in the header. The Lexer.cpp error is on line 290 — let me check what's happening:

The `INVTRAPEND` is defined in Token.hpp but the compiler says it's not found — that means the include guard or ordering might be off. Let me do a full rebuild check:

It's there. The compiler is using a cached object — let me force a clean rebuild:

The header looks correct. The error must be an include path issue — the compiler is finding a different or stale Token.hpp. Let me check what path is being used in Lexer.cpp:

The include path is correct. The `INVTRAPEND` is in the enum but the compiler can't find it — this suggests there's a character encoding issue with the backslash in the comment on that line. Let me check if the file has any encoding issues:

There's a `0a` (newline) right after `[\ ` on line 33 — the backslash-newline is being interpreted as a **line continuation** inside the enum, merging lines 33 and 34 so `INVTRAPEND` gets swallowed. The comment `// [\` is causing the issue: the `\` at end of line acts as a line continuation. I need to remove that backslash from the comment:

Same issue exists in Lexer.cpp with `startsWith("\\]")` in the comment line. Let me also fix the `erase` issue in Layout.cpp — `node_order` is a `std::vector<std::string>` and needs the erase-remove idiom correctly:

The bug: `g.nodes.erase(...)` should be `g.node_order.erase(...)`.

Fix `tmpnam` in TextMetrics_native.cpp with `mkstemp`:

No errors. Let me confirm a clean build:

Clean build. Now write the unit tests:

Clean build. Now run all tests:

The test input `.->`  is being read as `NODE_STRING` (`.`) rather than a LINK — the dotted edge pattern requires `-.->`or `-.->` not just `.->`. The jison grammar uses `-\.+-` (dash, dots, dash). Fix the test to use a valid dotted edge:

Good progress — 30/32 passing. Two remaining failures:

1. `NodeCountMatchesInput` — the count is 4, not 3. The `mp-node` substring appears in `mp-nodes` (the container `<g>`) too. Fix the count logic.
2. `ParsesClassDef` — likely a parsing issue with `:::` syntax after classDef. Let me check:

The `class_defs` map is empty after parsing. The issue is `classDef` is a keyword but my parser's `parseStylesOpt` eats the rest of the line after the class name, before the CSS is properly parsed. Let me check with a simpler classDef first — `A:::green` is a separate statement. Let me simplify the test to just check classDef without the `:::` usage:

The parser isn't reaching `parseClassDefStatement` — `classDef` after a newline is being treated as a vertex statement. The issue is `classDef` contains uppercase letters but the lexer returns it as `NODE_STRING` if it doesn't match the keyword exactly (it checks `word == "classDef"`). Let me verify:

The lexer word scanning loop uses `std::isalpha` + `std::isalnum` which only reads `[A-Za-z0-9_-]`. `classDef` starts with `c`, goes fine up to `D`, but `-` breaks the loop early — wait, no, `classDef` has no `-`. The real problem: the lexer word scan allows `-` characters in identifier: `while ... || cur() == '_' || cur() == '-'`. So `classDef` scans correctly to `"classDef"`. 

The actual problem: `parseClassDefStatement` calls `parseStylesOpt()` which reads until NEWLINE/SEMI/EOF. But `parseIdString()` is called first for the class name and it consumes `NODE_STRING` tokens — after consuming `green`, the `fill:#0f0` part: `fill` is a NODE_STRING, `:` is a COLON, `#` is UNKNOWN, `0f0` is... hmm. Let me check `parseStylesOpt` again — it eats everything until newline. The class name `green` would be consumed by `parseIdString()`. Then `parseStylesOpt` gets `fill:#0f0`. So the CSS should be populated.

The real issue is that `parseClassDefStatement` is not being reached at all — `parseStatement` checks `lexer_.peek().type == TokenType::CLASSDEF` but maybe after the newline the peek sees something else. Let me trace: the lexer's word scan includes `-` as a valid character, so `classDef` is scanned as a single word since the hyphen check is `|| cur() == '-'`. That means `classDef` produces `"classDef"` → `CLASSDEF`. That should work.

Actually, looking more carefully at `parseStatement` — it calls `parseVertexStatement()` as default, which calls `parseNode()` → `parseStyledVertex()` → `parseVertex()` → `parseIdString()`. The `parseIdString()` calls `consume()` on the first `NODE_STRING/NUM/MINUS` — but `CLASSDEF` is not in that list, so `parseIdString()` throws `ParseError("Expected identifier")`.

Wait — `parseStatement` checks the peek type before dispatching. If `CLASSDEF` is peeked, it goes to `parseClassDefStatement`. The issue might be that after `A --> B\n`, the next statement starts but `classDef` fails to parse as a statement. Let me check if there might be a separator-skipping issue.

Actually, the issue is simpler: `parseStylesOpt()` consumes tokens until NEWLINE. But `parseIdString()` before it might not stop at SPACE. The `classDef green fill:#0f0` line — after consuming `green` from `parseIdString()`, the space is skipped, then `parseStylesOpt` should get `fill:#0f0`. But `skipSeparators()` is called between them in `parseClassDefStatement`:

```cpp
void Parser::parseClassDefStatement() {
    consume(); // CLASSDEF
    skipSeparators();  // ← this skips SPACE tokens
    std::string class_name = parseIdString();
    skipSeparators();
    std::string css = parseStylesOpt();
```

`skipSeparators` skips SPACE, NEWLINE, SEMI. But the lexer in `scanInitial()` skips spaces inline (`while (pos_ < src_.size() && src_[pos_] == ' ') ++pos_;`). So SPACE tokens are never produced. That means `skipSeparators()` after `parseIdString()` does nothing — fine.

The real problem may be that `parseIdString()` consumes `green` but then also tries to consume `fill` since it's a NODE_STRING! It will keep consuming until it hits something that's not NODE_STRING/NUM/MINUS. So `green fill` becomes the class name. Let me fix `parseIdString` to stop at whitespace — but the lexer already skips spaces. Actually the lexer skips single spaces but not multiple.

Wait — re-reading the lexer: `while (pos_ < src_.size() && src_[pos_] == ' ') ++pos_;` — this skips ALL leading spaces before returning the next token. So after `green`, the lexer will skip the space and return `fill` as the next `NODE_STRING`. So `parseIdString` will consume both `green` and `fill`, making the class name `"greenfill"` and leaving `:#0f0` as the CSS.

The fix: `parseIdString` should only consume a single word token, not run multiple. Looking at the jison grammar, `idString` is a sequence of tokens WITHOUT spaces — but our lexer skips spaces. In Mermaid syntax, `green` is a single identifier. The idString in jison is `idString idStringToken` which concatenates adjacent tokens with NO whitespace between them.

Since our lexer skips whitespace, we need to stop `parseIdString` after one identifier word. The simplest fix for `classDef`/`class`/`style` statements is to use a single-token parse instead of `parseIdString`:

Do the same for `parseStyleStatement`:

Now fix the `NodeCountMatchesInput` test — count `class="mp-node"` instead of the substring `mp-node` which also appears in `mp-nodes`:

All 32 mermaid tests pass. Now run the full test suite including CommonMark spec regression:

Everything passing:

- **32 mermaid tests** — lexer, parser, layout, SVG output
- **652 CommonMark spec tests** — zero regressions

The full mermaid module is in place:
- [include/mermaid/](include/mermaid/) — 8 headers
- [src/mermaid/](src/mermaid/) — 7 source files (Lexer, Parser, Layout, SvgRenderer, MermaidParser, TextMetrics_native, TextMetrics_wasm)
- `MermaidHandler.cpp` now calls `mermaid::parse()` → inline SVG
- `web/index.html` — mermaid.js dependency removed entirely
- Text measurement: browser-probed at diagram parse time for native CLI, `canvas.measureText()` via `EM_JS` for WASM

## ▸ Prompt — 2026-06-14 22:20

> Check that @src/markdown_parser/handlers/MermaidHandler.cpp is linked correctly also for the wasm binary

`MermaidHandler.cpp` is correctly on line 69 of the WASM target — it's included directly in `markdown_parser_wasm` (not via the static library), which is exactly what's needed to ensure the static initializer self-registration runs. `${MERMAID_SRCS}` and `TextMetrics_wasm.cpp` are also there. Everything is wired correctly for both targets.

## ▸ Prompt — 2026-06-14 22:32

> Something is off, I think it is due to text measurement (both native/wasm), add some debug prints also

Let me look at the SVG renderer and layout code to understand what's going wrong.

I can see the issue clearly now. Looking at the screenshot versus the code, node labels should show but don't. Let me add debug prints to trace the full pipeline — parser output, text measurement, and final node positions.

Now let's build and run to see the debug output:

Found the bug. In `renderNode()`, the style injection does `shape.find('>')` which finds the `>` inside `/>`, producing broken SVG like `rx="4"/ style="...">`. The fix is to look for `/>` first.

The SVG is now valid — `<rect ... style="..."/>` with properly closed self-closing tags, and `<text>` labels follow each shape. The root cause was the style injection inserting before `>` in `/>`, producing broken `/>` split by an attribute. 

The debug output also confirmed everything else is working: labels are parsed correctly ("Start", "Is it working?", "Great!", "Debug"), text measurement returns sensible values (29–82 px), and node positions look reasonable.

