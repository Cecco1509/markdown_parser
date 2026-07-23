# Flowchart grammar and the decision to generate a parser

**Theme:** Mermaid engine

**Source transcript:** `7316746a-768b-435e-8961-a44e9b8886c9.jsonl`  
**Records:** 0–246

**Dates:** 2026-07-03 10:33 → 2026-07-03 16:45  
**User turns:** 8

**Related commits:**

- `42f53a4 add flowchart specs`
- `0e7421b start mermaid block support - Lexer -`

---

## ▸ Prompt — 2026-07-03 10:33

> Let's plan the implementation for the integration of mermaid code blocks parsing and rendering in the current markdown parser. I already setup the verification strategy which is the following:
>
> 1. pre-generate the mermaid test files (before testing)
> 2. run @mermaid-utils/extract.mjs  on them to generate the SVG and the AST in json format
> 3. (Testing starts) call our mermaid parser and check against the pre-generated SVG and AST:
>          - the SVG output structurally, not pixel by pixel
>          - the AST
>
> Review this strategy.
>
> What is left to do is the actual parser, and creating another test unit to bind the parser to the verification step.
>
> Have a look at @docs/mermaid/flowchart-spec.md , this is the subset of mermaid construct that I want to support, since there are a lot of charts, better to stick with one and keep things simple.
>
>  The next step is to brainstorm on this things:
> - Lexer + Parser: write a simple one or use something like (ocaml menhir) counterpart in cpp
> - Layout engine: this is the actual hard part that we need to think about. Which possibilities do we have? Is it possible to mimic mermaid.js?

Let me look at the codebase structure to ground this in what already exists before brainstorming.

Let me look at the existing Mermaid handler, the current AST/SVG fixtures, and how handlers/tests are wired.

I've now got the full picture. Let me give you a grounded review and brainstorm. First, one **critical bug** I found while reading the fixtures.

## 🔴 Your reference AST is broken — `vertices` is empty

Look at [simple-mermaid.ast.json](tests/test-files/simple-mermaid.ast.json): the `.mmd` clearly declares `A[Start]`, `B{Is it working?}`, `C[Great!]`, `D[Debug]` — but `"vertices": []`. Edges came through fine; nodes didn't.

Cause is almost certainly in [extract.mjs:119-130](mermaid-utils/extract.mjs#L119-L130). In mermaid v11 (you pinned `11.0.0`), `db.getVertices()` returns a **`Map`**, not a plain object. `Object.values(aMap)` → `[]`. `getEdges()` returns an array, which is why edges survived. Fix:

```js
function serializeVertices(vertices) {
  const list = vertices instanceof Map ? [...vertices.values()] : Object.values(vertices);
  return list.map(v => ({ ... }));
}
```

This matters a lot: **right now your AST verification of nodes is vacuous** — any parser that emits zero nodes "passes." Same latent risk for `getSubGraphs()` / `getClasses()`; check their return types against v11 too. Want me to fix and regenerate the fixtures?

## Review of the verification strategy

The two-level golden-file approach is sound, and decoupling AST (parser correctness) from SVG (layout correctness) is the right instinct. Refinements:

**Make the AST the primary gate.** It's deterministic, order-stable, and directly tests what you control. mermaid's IDs (`L_A_B_0`) are deterministic, so exact JSON comparison works once the vertices bug is fixed.

**Reconsider "structural SVG comparison against mermaid's SVG."** This is the weak link. mermaid renders via **dagre** — every coordinate, `viewBox`, and path is a product of its specific layout algorithm and float arithmetic. If you write your own layout engine, your geometry will *never* match, so any comparison must strip geometry entirely and compare only topology (node count, labels, edge connectivity, shape classes). But at that point you're just re-deriving the AST from your own SVG — **redundant with the AST check.**

My recommendation: **don't diff your SVG against mermaid's.** Instead run a *self-consistency* check on your own SVG:
- well-formed XML;
- every AST node → exactly one node `<g>`; every AST edge → one path;
- labels present as text;
- (optional) no node bounding-box overlaps.

Treat mermaid's `.svg` as a *visual reference for humans* (open both in a browser), not a machine assertion. Keep the AST diff as the CI gate.

**Scope your fixtures explicitly.** [flowchart-spec.md](docs/mermaid/flowchart-spec.md) is a superset — F-12/F-13 (front-matter, init), F-15 (edge IDs, v11.10+, and you're on 11.0.0 so these won't even parse the same), F-11 (click). Pick the subset you support, and mark the rest `xfail` so the corpus documents scope rather than silently omitting.

## Lexer + Parser: hand-write it

**Recommendation: hand-written tokenizer + recursive-descent, no generator.**

Reasons:
- **Consistency.** Your codebase already hand-rolls the [block](src/markdown_parser/parser/block_rules.cpp) and [inline](src/markdown_parser/parser/InlineParser.cpp) parsers. A Bison/Flex or PEGTL/lexy dependency would be the odd one out, and this is a course project where the hand-written parser *is* the thing being graded.
- **The grammar fits recursive descent well.** It's line-oriented: each statement is a node decl, an edge chain, a `subgraph…end` block, or a directive (`style`, `classDef`, `class`, `linkStyle`, `direction`). That's a clean top-level dispatch.
- **A generator wouldn't even help on the hard parts.** mermaid's own grammar (jison) is irregular and context-sensitive — the edge operators (`-->`, `-.->`, `==>`, variable-length `----`, `--o`, `--x`, `<-->`) are better recognized by a purpose-built lexer than an LR grammar. You'd fight the tool.

Structure: `Lexer` producing tokens per line → `Parser` building your flow-DB (vertices map, edges vector, subgraphs, classes) mirroring the AST JSON shape so comparison is 1:1.

Bake the spec's warning cases directly into parser unit tests — they're the ones people get wrong:
- **F-06 `&` cross-product**: `A & B --> C & D` expands to 4 edges.
- **F-05 edge length**: count dashes/dots/equals.
- **F-03 last-label-wins**: dedupe by *last* occurrence, not first.
- **F-14 comment**: `%%` only at first non-whitespace; no trailing-comment stripping.
- **F-02 shape zoo** + `:::class` inline.

## Layout engine — the honest answer

**You cannot pixel-match mermaid, and you shouldn't try.** mermaid uses **dagre**, a Sugiyama-style layered layout with four phases:

1. **Rank assignment** — assign nodes to layers (longest-path or network-simplex).
2. **Ordering** — minimize edge crossings within layers (median/barycenter heuristic, iterated).
3. **Coordinate assignment** — horizontal positions (Brandes–Köpf).
4. **Edge routing** — splines through dummy nodes for long edges.

Your realistic options:

| Option | Verdict |
|---|---|
| **Port dagre to C++** | Faithful but large; still won't match exactly (network-simplex tie-breaks, floats). Overkill. |
| **Link Graphviz `dot`** (also Sugiyama) | Real layout for free, but different coordinates from mermaid, and a heavy C dependency. |
| **OGDF** | Powerful, but huge and GPL — likely wrong fit for a course project. |
| **Shell out to real dagre (JS)** | Defeats the purpose. |
| **Write a minimal Sugiyama layout yourself** | ✅ Right scope. |

**Recommendation:** implement a **minimal 4-phase layered layout** yourself for the supported subset:
- longest-path ranking (respect `direction` TB/LR/BT/RL by swapping axes),
- median-heuristic crossing reduction (2–4 sweeps is plenty for these small graphs),
- simple centered coordinate assignment,
- straight or simple-bezier edges with dummy nodes for rank-spanning edges.

Then **emit SVG using mermaid's own class names and shape templates** (`class="node default"`, `class="flowchart-link"`, `<g class="nodes">`, etc. — I already pulled these from your fixture). That way it renders as recognizably "mermaid-style" even though coordinates differ, and a human diffing the two SVGs in a browser sees the same diagram.

So: **mimic mermaid's structure and style, not its geometry.** That's an achievable, gradeable target; exact geometric parity is not.

## Suggested phasing

1. Fix the vertices bug, regenerate fixtures, freeze the supported subset (mark unsupported F-* as xfail).
2. Lexer + parser → flow-DB matching the AST JSON. **Gate: AST diff.**
3. SVG emitter with a minimal layered layout + mermaid CSS classes. **Gate: SVG self-consistency check.**
4. Wire into [MermaidHandler.cpp](src/markdown_parser/handlers/MermaidHandler.cpp) (currently a passthrough `<div class="mermaid">`) so the markdown pipeline emits inline SVG.

Want me to start by fixing the extractor + regenerating fixtures, or by scaffolding the parser skeleton (Lexer/Parser headers + flow-DB type mirroring the AST)?

## ▸ Prompt — 2026-07-03 13:04

> First thing is to fix the bug in extract.mjs as it is crucial for testing.
>
> On the verification strategy: I agree on the proposed change, so the validation is AST matching and a consistency check, which I think it is good. Also I want to point out that the AST coming from extract.mjs is made for the verification, so we shouldn't enforcing the same structure, simply I created the testing before the actual parser. So to check the ASTs we could create a checking function that takes the two types and checks them, or we could adapt the emitted json of extract.mjs to the one of the actual parser. (This was just to say that the actual AST shouldn't be based on the one returned by extract.js)
>
> On the lexer+parser: I agree, let's create our parser, I'd like it to be a LR(1) parser, point out the ambihuities that the subset I choose get wrong. Also I want to support the features up to mermaid 11.0.0 for flowchart diagrams (lnk to doc: https://mermaid.ai/open-source/syntax/flowchart.html)
>
> On Layout Engine: I don't want to add dependency in this project so writing a minimal Sugiyama layout is the choice I'll go to.
>
> Let's go through this step by step, first let's fix the bug in extract.js then let's define the skeleton of the parser and discuss each topic in details

Let's start with the bug fix. Let me fix the serializers to handle `Map` return types (v11), then regenerate the fixture to confirm.

Deps are there. Let me make the serializers robust to both `Map` and plain-object returns:

Now `getClasses()` also returns a `Map` in v11 — but it's stored as an object in the JSON. Let me convert it to a plain object:

Now regenerate the fixture and confirm vertices populate:

Bug fixed — `vertices` now populates with correct `id`, `label`, and `shape`. One thing to note for later: `domId` (`flowchart-A-0`) embeds mermaid's internal insertion index and DOM-id scheme — **exclude it (and mermaid's internal edge `id` like `L_A_B_0`) from AST comparison**, since our parser won't and shouldn't reproduce those.

Let me glance at the existing code conventions so the parser skeleton matches the house style.

Bug's fixed and confirmed. Now let me lay out the parser. I read the house style (`#pragma once`, `namespace markdown_parser`, public-field structs, `snake_case_` privates). Here's the skeleton and the detailed discussion you asked for.

## A. Scope: mermaid 11.0.0 flowchart

You pinned `11.0.0` but linked the *current* docs — they don't match, and the gap is exactly the parser's hardest features, so pinning helps us:

| Feature | In 11.0.0? | Decision |
|---|---|---|
| Classic bracket shapes (14), edges, chains, `&` | ✅ | **support** |
| Subgraphs, nested, per-subgraph `direction` | ✅ | **support** |
| `style`, `classDef`, `class`, `:::`, `linkStyle` | ✅ | **support** |
| `click`, front-matter, `%%{init}%%` | ✅ | support (config parsed minimally) |
| `@{ shape: … }` node syntax (F-02 second half) | ❌ v11.3 (Sep 2024) | **exclude** |
| Edge IDs `e1@-->`, `@{ animate }` (F-15) | ❌ v11.10 | **exclude** |

So F-15 drops entirely and F-02's `@{shape}` block drops. That removes the `@` sigil from the lexer — a real simplification.

## B. The key insight: the ambiguities are *lexical*, not grammatical

mermaid's grammar is LALR(1) (jison proves it) **only because its lexer is stateful and does the disambiguation before the grammar ever sees a token.** A pure LR(1) grammar over a naïve tokenizer is *not* clean. So the honest answer to "can it be LR(1)": **yes — conditional on a stateful lexer.** Every hard case below is resolved in the lexer; the grammar stays LR(1)-clean.

**Ambiguities your subset will get wrong without lexer states:**

1. **Edge operators are variable-length and whitespace-insensitive.** `A---B` = open link, length 2; `A-->B`, `A-.->B`, `A==>B`, `A--oB`, `A--xB`, `A<-->B`. The `-`/`.`/`=` run must be lexed **greedily as one `LINK` token** carrying `(stroke, arrowhead, bidir, length)`. If you tokenize `-` individually, `---` is ambiguous with node text and you get shift/reduce conflicts.

2. **The `-- text -->` context switch** (the nastiest one). `--` can start *either* an open link *or* a labeled edge. Only resolvable by scanning ahead for the closing `-->`/`---`. jison uses a lexer **edge-text mode**; you must too. Related: with a mid-label, extra dashes count **only on the right side** — `A -- x ---> B` is length 3, not 5 (F-05).

3. **Bracket-run longest-match.** `[(`=cylinder vs `[`+`(`; `((`=circle vs `(`=round; `(((`=double circle; `[[`=subroutine; `{{`=hexagon; `>`=asymmetric. The lexer must greedily match the opening delimiter run and then capture text until the *matching* close. This is pure lexer longest-match; the grammar just sees `NODE_ID shape?`.

4. **Keyword-vs-identifier.** `end`, `graph`, `subgraph`, `style`, `class`, `direction`, `click`, `default`, and the direction codes `TB/TD/BT/LR/RL` are all legal node ids in some position. This is a genuine reduce/reduce ambiguity. mermaid resolves it by (a) keywords only at statement start, (b) `end` special-cased, (c) direction codes only right after `graph`/`direction`. **Recommendation for our subset: reserve these words** — forbid them as bare node ids (require quoting), which matches what mermaid effectively warns about. Cleaner grammar, documented limitation.

5. **`&` cross-product** — this one *is* grammar-clean (`nodeGroup : node (AMP node)*`), but the **semantics** are subtle: `A & B --> C & D` expands to 4 edges (full cross product), and in a chain `A --> B & C --> D` each `LINK` cross-products the adjacent groups. Grammar shifts fine; the expansion is a semantic action.

## C. LR(1) grammar sketch (clean, given the lexer above)

```
document    : header sep_stmts
header      : (GRAPH DIR? NEWLINE) | (GRAPH NEWLINE)
sep_stmts   : /* ε */ | stmt (SEP stmt)*
stmt        : edgeStmt        // also covers a bare node (chain of length 0)
            | subgraphStmt
            | styleStmt | classDefStmt | classStmt | linkStyleStmt | clickStmt
            | directionStmt
edgeStmt    : nodeGroup (LINK nodeGroup)*      // 0 links ⇒ bare node decl
nodeGroup   : styledNode (AMP styledNode)*
styledNode  : NODE_ID shape? classAttach?
shape       : SQ_TEXT | ROUND_TEXT | DIAMOND_TEXT | ...   // each carries text+kind
classAttach : TRIPLE_COLON NODE_ID
subgraphStmt: SUBGRAPH sgHeader? NEWLINE sep_stmts END
directionStmt: DIRECTION DIR
```

`edgeStmt`/`nodeStmt` unify (a bare node is a zero-link chain), so the classic "is this a node or an edge" question is resolved by **one token of lookahead** (`LINK`/`AMP` vs `SEP`) — exactly what LR(1) is good at. No conflicts remain once the lexer owns cases 1–4.

## D. Our AST (`FlowDb`) — independent of extract.mjs

Agreed: our AST is its own type; the test layer bridges via a **normalizing comparator**, not a shared schema. Proposed:

```cpp
namespace markdown_parser::mermaid {

enum class Direction { TB, BT, LR, RL };
enum class ShapeKind { Rect, RoundEdges, Stadium, Subroutine, Cylinder, Circle,
                       Asymmetric, Rhombus, Hexagon, LeanRight, LeanLeft,
                       Trapezoid, TrapezoidAlt, DoubleCircle };
enum class Stroke    { Normal, Thick, Dotted };
enum class ArrowHead { None, Arrow, Circle, Cross };

struct Vertex   { std::string id, label; ShapeKind shape = ShapeKind::Rect;
                  std::vector<std::string> classes, styles; };
struct Edge     { std::string start, end, label; Stroke stroke = Stroke::Normal;
                  ArrowHead head_end = ArrowHead::Arrow; bool head_start = false;
                  int length = 1; };
struct Subgraph { std::string id, label; std::vector<std::string> nodes;
                  std::optional<Direction> dir; };
struct ClassDef { std::string id; std::vector<std::string> styles; };

struct FlowDb {
  Direction direction = Direction::TB;
  std::string title;
  std::vector<Vertex>   vertices;    // insertion order; last-label-wins merged
  std::vector<Edge>     edges;
  std::vector<Subgraph> subgraphs;
  std::map<std::string, ClassDef> classes;
};

} // namespace markdown_parser::mermaid
```

**Comparator** (test side): normalize both our `FlowDb` and extract.mjs JSON into a canonical form, **dropping non-portable fields** (`domId`, mermaid's `L_A_B_0` edge id). Compare vertices as a map keyed by id, edges as an ordered list, plus direction/title/subgraphs/classes. Map mermaid's shape names → our `ShapeKind` (e.g. their `"square"`/`"squareRect"` → `Rect`, `"diamond"` → `Rhombus`).

## E. File skeleton

```
include/markdown_parser/mermaid/
  Token.hpp     FlowDb.hpp     Lexer.hpp     Parser.hpp
src/markdown_parser/mermaid/
  Lexer.cpp     Parser.cpp     FlowDb.cpp
```
`MermaidHandler.cpp` then becomes: `parse(src) → FlowDb → (later) layout → SVG` instead of today's passthrough `<div class="mermaid">`.

## One decision before I generate code

True LR(1) means the parse tables (ACTION/GOTO). No-dependency + LR(1) leaves two realistic paths:

Good — build-time LALR generator it is. Let me detail that architecture (this is the meaty part) and lay down the settled foundation files. First let me see how the build is wired.

The CMake setup surfaces a real constraint: **`MermaidHandler.cpp` is in the Emscripten/WASM target too.** A build-time generator is a *host* tool, and you can't cleanly build-and-run a host executable mid-Emscripten-cross-compile. This shapes the generator design, so let me lay down the settled foundation files first, then detail the generator + that one wiring decision.

Two tokens I under-specified — `class A,B name` and `linkStyle 2,3 …` need `Comma` and `Num`. Let me fold those in:

Now the declarative grammar — the generator's input and the single source of truth:

Foundation is down. Here's the generator architecture and the one wiring decision the WASM target forces.

## The build-time LALR generator (`tools/lalrgen`)

A self-contained **host C++ program** (native compiler, zero third-party deps — keeps a single toolchain, no Python/Java requirement):

```
flowchart.grammar ──► lalrgen ──► flowchart_tables.inc
                                    (constexpr ACTION/GOTO/production tables)
```

- **Input**: [flowchart.grammar](src/markdown_parser/mermaid/flowchart.grammar) (just wrote it).
- **Pipeline**: read grammar → compute FIRST/FOLLOW → build LR(1) item sets → **merge to LALR(1)** (states with identical cores) → detect conflicts (fail the build loudly if any — this is how the grammar stays honest) → emit tables.
- **Output**: a generated header of `constexpr` arrays: `ACTION[state][terminal]`, `GOTO[state][nonterminal]`, and a production table `{lhs, rhs_len}` per rule.

**Semantic actions live in `Parser.cpp`, not the grammar.** The generator emits only tables + numbered productions; the runtime driver is a generic shift/reduce loop with:

```cpp
switch (prod_id) {           // hand-written, in Parser.cpp
  case P_node_id_shape:  /* build Vertex, push on value stack */ break;
  case P_edge_chain:     /* cross-product & append edges */     break;
  ...
}
```

This keeps all C++ in `.cpp` files (real tooling/debugging) and keeps the generator purely about tables. The value stack carries a `variant`/tagged union of partial results (Vertex, node-group list, edge attrs, …).

## The decision the WASM target forces

`MermaidHandler.cpp` compiles under **both** the native and Emscripten targets. A host generator can't be run mid-Emscripten-cross-compile without extra machinery. Two clean ways out:

Both decisions locked. Here's where we stand and the build order.

**Done this session**
- ✅ Fixed the `extract.mjs` vertices bug (Map-vs-object in v11) + hardened edges/subgraphs/classes; regenerated the fixture — nodes now populate.
- ✅ [FlowDb.hpp](include/markdown_parser/mermaid/FlowDb.hpp) — our AST, independent of extract.mjs.
- ✅ [Token.hpp](include/markdown_parser/mermaid/Token.hpp) — token stream the stateful lexer emits.
- ✅ [flowchart.grammar](src/markdown_parser/mermaid/flowchart.grammar) — declarative LR(1) grammar, source of truth for the generator.
- ✅ Decisions: build-time **LALR generator**, wired via **host-tool ExternalProject** (nothing generated is committed).

**Remaining build order** (each independently testable):

| # | Component | Depends on | Notes |
|---|---|---|---|
| 1 | **Lexer** (`Lexer.hpp/.cpp`) | Token.hpp | Where all the ambiguity-resolution lives (edge-op greedy match, `-- text -->` mode, bracket longest-match, contextual keywords). Unit-testable in isolation against raw strings. |
| 2 | **lalrgen** (`tools/lalrgen`) + CMake ExternalProject | grammar | Emits `flowchart_tables.inc`; fails build on any conflict. |
| 3 | **Parser driver** (`Parser.cpp`) | 1 + 2 | Generic shift/reduce loop + semantic-action `switch` building FlowDb. |
| 4 | **AST comparator + test unit** | 3, extract.mjs JSON | Normalizes both sides (drop `domId`/mermaid edge ids), gates CI. Binds parser to your verification step. |
| 5 | **Layout** (minimal Sugiyama) | 3 | 4 phases; respects `direction`. |
| 6 | **SVG renderer** (mermaid CSS classes) + wire `MermaidHandler` | 5 | Self-consistency check, not SVG-diff. |

I recommend starting with the **lexer (#1)**: it's the intellectual core (it's what makes the grammar LR(1)-clean), it has zero dependency on the generator, and it's cleanly unit-testable against the F-01…F-18 fixtures before any parsing exists.

Want me to implement the lexer next, or take the generator + CMake wiring first so the whole pipeline compiles end-to-end early?

## ▸ Prompt — 2026-07-03 13:50

> Why do you call the lexer statefull? shouldn't the lexer only emit tokens and then the parser uses the tokens to construct production? The lexer must not have semantic reasoning, why did you enforce this? maybe I'm missing something.
>
> Also you started creating the mermaid files inside the markdown namespaces and folders, I would move them out the markdown scope and have them as a per se include. Logically it could be used even out of the markdown scope, what do you think?
>
> The other choices are sound to me. Let's first discuss the question above before continuing with the implementation

Both are good challenges. The second one you're just right about; the first one I need to both clarify a real distinction *and* concede I overstated it. Let me take them in order.

## 1. "Stateful" ≠ "semantic" — but you're right that I over-reached

You're conflating two different things, and part of that is my fault for lumping them together.

**Semantic reasoning** = meaning: symbol tables, "is this node already declared," last-label-wins, cross-product expansion. That is 100% the parser's / semantic-action's job, and none of it belongs in the lexer. I never intended the lexer to do that.

**Lexical state (start conditions / modes)** = a purely mechanical, textbook lexer feature: *"while scanning this delimited run, different characters are significant."* The canonical example is a **string literal** — in every compiler ever written, the lexer has a mode (or a helper subroutine) that reads from `"` to the closing `"` and treats `<`, `(`, spaces, etc. as ordinary content. `flex` bakes this in as `%x` start conditions. That is not semantics — it's recognizing a token whose content model is "raw text up to a delimiter." No meaning is assigned.

Now, where does mermaid *actually* need this? Let me be honest case by case, because I lumped in things that don't need it:

| Case | Needs lexer state? | Honest verdict |
|---|---|---|
| Edge ops `-->`, `-.->`, `==>`, `---` | **No** | Maximal-munch single-token regex. Stateless. |
| `end`, `direction`, `TB`/`LR` as keyword-vs-id | **No** | I over-reached here. Since **we reserve these words** (our earlier decision), the lexer classifies them unconditionally. The "contextual keyword" statefulness *disappears*. |
| Quoted label `"a > b & c"` | **Yes** | Identical to a string literal. Read to closing `"`. |
| Bracket label `A[a > b (x)]` | **Yes** | Content is raw text to the matching `]`. Without this you'd have to force-quote every non-trivial label (diverging from mermaid) *or* let `>` and `(` tokenize separately and reassemble the string in the grammar — which makes the grammar **not** LR(1), because `]` inside text becomes ambiguous. |
| Pipe label `-->\|text\|` | **Yes** | Same: raw text to `\|`. |
| `-- inline label -->` | **Borderline** | Can be done stateless (emit `LINK_START`, word tokens, `LINK_END`; parser assembles), *or* with a mode. Stateless is viable here. |

So the corrected picture: the lexer's only *essential* statefulness is a handful of **scan-until-delimiter subroutines** for delimited raw-text runs (quoted strings, bracket bodies, pipe labels) — exactly the string-literal pattern. Everything I earlier attributed to "contextual keywords" was me over-reaching; reserving the keywords makes that part stateless.

**Why not push even the delimited runs to the parser (fully context-free lexer)?** You can, but the cost is the grammar stops being LR(1): the parser would receive `[`, `a`, `>`, `b`, `]` as separate tokens and have to prove where the label ends, and `]` appearing *inside* label text creates a genuine shift/reduce ambiguity. The division of labor — **lexer owns "delimited text run," parser owns structure** — is what keeps the grammar clean. That's the whole reason mermaid's own jison lexer uses start conditions.

So: I'll reframe it in the code/docs as a lexer with **a few delimiter sub-scanners**, not a vaguely "stateful, semantic" lexer — and it does zero semantic reasoning. Does that distinction land for you?

## 2. Move mermaid out of the markdown scope — fully agree

You're right. Flowchart parse → layout → SVG is self-contained; markdown only *invokes* it through the fence handler. Coupling it into `markdown_parser::` misrepresents the dependency (markdown depends on mermaid, not vice-versa). Making it a peer module improves reuse and lets it stand alone.

Proposed restructure:
- Namespace `markdown_parser::mermaid` → top-level **`mermaid`**.
- Headers `include/markdown_parser/mermaid/` → **`include/mermaid/`** (same include root, so `#include "mermaid/Lexer.hpp"`).
- Sources → **`src/mermaid/`**; generator stays at **`tools/lalrgen/`**.
- CMake: a standalone **`add_library(mermaid …)`**; `md_parser_bin` / the WASM target link it. The fence handler becomes the *single* coupling point: `mermaid::render(src)`.

Let me apply the move now (only the 3 files I created), then we're aligned to resume.

Restructure done. The module now lives at `include/mermaid/` + `src/mermaid/`, namespace `mermaid`, standalone from markdown.

## Where we landed on your two questions

**Lexer statefulness** — you were right to push. Corrected position:
- The lexer does **zero semantic reasoning** (no symbol table, no last-label-wins, no cross-product — all parser/semantic-action work).
- Its only non-context-free part is a few **scan-until-delimiter subroutines** for delimited raw-text runs (quoted strings, bracket labels, pipe labels) — mechanically identical to how every compiler lexes a string literal.
- The "contextual keyword" statefulness I originally claimed was an over-reach; **reserving keywords removes it**, so `end`/`direction`/`TB` are classified unconditionally.
- The reason those runs live in the lexer and not the grammar: pushing them into the parser makes the grammar non-LR(1) (a `]` inside label text becomes a genuine shift/reduce ambiguity). Lexer owns *delimited text*, parser owns *structure* — clean division, no semantics on either side crossing over.

I baked this framing into the header comments of [Token.hpp](include/mermaid/Token.hpp) so the intent is documented in code.

**Module placement** — agreed and applied: top-level `mermaid` namespace, `include/mermaid/` + `src/mermaid/`, its own future `add_library(mermaid …)`, with the markdown fence handler as the sole coupling point.

We're aligned. Next up in the build order is the **lexer** (`include/mermaid/Lexer.hpp` + `src/mermaid/Lexer.cpp`) — implementing exactly those delimiter sub-scanners plus greedy edge-operator matching, unit-testable against the F-01…F-18 fixtures. Want me to go ahead and implement it?

## ▸ Prompt — 2026-07-03 14:07

> I'm reading the Token structure in @include/mermaid/Token.hpp and I don't understand why you wrote multiple kind types in the same struct. Can you give some examples?

Good question — let me pull up the exact struct and walk through it.

Right — the confusion is fair. The struct is a **flat "fat" record**: one `kind` discriminator, then the *union* of every payload any token might need. For a given token only the fields relevant to its `kind` are populated; the rest sit at their defaults and are dead. It's **not** a real `union` — all fields physically exist in every `Token`; they're just ignored when irrelevant.

Here's what actual tokens look like. Take this source:

```
flowchart LR
    A[Start] -- yes --> B
```

The lexer emits this stream (showing only the *live* fields per token):

| # | `kind` | live fields | dead fields (left at default) |
|---|---|---|---|
| 1 | `KwGraph` | — (the keyword *is* the meaning) | text, shape, value_dir, stroke… |
| 2 | `Dir` | `value_dir = LR` | text, shape, stroke, length… |
| 3 | `Newline` | — | everything |
| 4 | `NodeId` | `text = "A"` | shape, value_dir, stroke, length… |
| 5 | `Shape` | `text = "Start"`, `shape = Rect` | value_dir, stroke, length… |
| 6 | `Link` | `stroke = Normal`, `head_end = Arrow`, `head_start = false`, `length = 1`, `edge_label = "yes"`, `has_inline_label = true` | shape, value_dir |
| 7 | `NodeId` | `text = "B"` | shape, stroke… |
| 8 | `Newline` | — | everything |

So `value_dir` (line 65, the one you selected) is only meaningful when `kind == Dir` — token #2. For token #6 (`Link`) it's just noise sitting at `TB`. Conversely the whole `stroke/head_end/length/edge_label` block is only meaningful for `Link` (#6) and is dead weight on every other token. `line`/`col` are the exception — populated for *all* tokens, for diagnostics.

A few more, for the payload-carrying kinds:

```
A{{Hexagon}}          → NodeId{text:"A"}, Shape{text:"Hexagon", shape:Hexagon}
X ==> Y               → ..., Link{stroke:Thick, head_end:Arrow, length:1}, ...
P <--x Q              → ..., Link{stroke:Normal, head_end:Cross, head_start:true}, ...
A --->  B             → ..., Link{stroke:Normal, head_end:Arrow, length:2}, ...
linkStyle 2,3 ...     → KwLinkStyle, Num{text:"2"}, Comma, Num{text:"3"}, StyleBody{text:"stroke:#00f"}
"a > b"               → Str{text:"a > b"}
```

## Why it's built this way (and the alternative)

This flat design is the pragmatic, common choice for lexers:
- **Trivial to produce and consume** — the parser reads `tok.text`, `tok.shape`, etc. directly; no unwrapping.
- **One type**, so `std::vector<Token>` is simple and cache-friendly; no allocation per variant.
- Cost: a `Token` is bigger than it needs to be (it carries every field), and the compiler can't stop you from reading a dead field for the wrong `kind` — that's a discipline you enforce by convention.

The type-safe alternative is `std::variant`, where each kind carries **only** its own payload:

```cpp
struct DirTok  { Direction dir; };
struct LinkTok { Stroke stroke; ArrowHead head; bool bidir; int length;
                 std::string label; };
struct IdTok   { std::string text; };
struct ShapeTok{ std::string text; ShapeKind shape; };
// ... plus payload-less kinds
using Token = std::variant<DirTok, LinkTok, IdTok, ShapeTok, /* … */>;
```

That makes it *impossible* to read `dir` off a `Link` — the compiler enforces it — but every access becomes a `std::visit` or `std::get_if`, which is heavier to write against in a table-driven parser loop.

For a small, fixed token set like this, the flat struct is the conventional trade (it's essentially what hand-written lexers and flex-generated ones do). But if you'd prefer the compiler to enforce "this field only exists for this kind," I can switch `Token` to the `variant` form before we build the lexer. Want me to?

## ▸ Prompt — 2026-07-03 14:14

> Yes please switch to the variant form, it keeps things simple and the std::visit/get_if shouldn't be much of an overhead. Then you can proceed to generate the Lexer

Switching to the variant form and generating the lexer. First, add `Invisible` to the `Stroke` enum (F-17 `~~~` needs it), then rewrite `Token.hpp` as a variant.

Now the lexer header:

Now the lexer implementation — the substantive piece with the delimiter sub-scanners and greedy edge matching:

Now wire the `mermaid` library into CMake and add a lexer test:

I used `trim_copy` before defining it — let me fix that ordering:

Now let me build and run the lexer tests:

Compiles cleanly. Now run the tests:

All 10 lexer tests pass, covering the constructs from F-01…F-10, F-14, F-16, F-17. The lexer builds as the standalone `mermaid` library and compiles clean.

## What landed this turn

- **`Token.hpp`** → variant form: each terminal is a distinct `TokenValue` alternative, so payload fields only exist on the token that carries them (compiler-enforced). Payload-less terminals share `PunctTok`. Accessors `tok.as<T>()` / `tok.is(Punct::…)`.
- **`Lexer.hpp/.cpp`** — the full stateful lexer with exactly the delimiter sub-scanners we discussed:
  - greedy edge matching (`match_compact_op`) for all heads/strokes/lengths + the `-- text -->` labeled form with the "closing operator" scan;
  - bracket-shape longest-open / scan-to-close, with quoted content skipped and lean-vs-trapezoid disambiguated by the actual closer;
  - reserved-keyword and direction classification (no contextual mode);
  - front-matter + `%%{init}%%` lifted out; `%%` comment lines dropped; trailing mid-line `%%` ignored per the F-14 rule.
- **CMake**: `add_library(mermaid …)` + `mermaid_tests`.
- **`to_string(Token)`** for readable test assertions.

**Deliberately deferred** (noted so they don't look like bugs): entity decoding inside labels (`#35;`, `&amp;`), YAML/init *config* parsing (raw text is captured, not interpreted), and `click call myFunc()` — the `()` currently lexes as an empty round shape. Click is low-priority (F-11) and I'll special-case it when we wire click semantics.

## Next: the lalrgen generator

Per the build order, the lexer's done and the grammar file exists. Next is **`tools/lalrgen`** — read [flowchart.grammar](src/mermaid/flowchart.grammar), build LR(1) item sets → LALR(1) merge → emit `flowchart_tables.inc`, wired via the host-tool ExternalProject. That unblocks the parser driver (#3), which ties the lexer + tables together into a `FlowDb`.

Want me to build the generator next, or would you rather review the lexer more closely first?

## ▸ Prompt — 2026-07-03 15:25

> Let's continue on the lalrgen generator as you suggested. I read the @src/mermaid/flowchart.grammar you wrote, you wrote that on which basis? like the syntax you used, please explictly say this choices. Since it can be complicated let's break it down:
>
> Let's define:
> - the syntax of the .grammar file 
> - the parser code production (we already defined when and how it is generated) let's focus on the details
> - where to place this code? I'd say it is another module like markdow_parser and mermaid, give your opinions

Good questions — let me be explicit about all of it. I'll take your three parts in order.

## 1a. What the current `.grammar` syntax is based on

I didn't invent it freehand — it's **Yacc/Bison notation** (POSIX yacc, the classic C parser generator), lightly adapted. Explicitly, per element:

| Element in the file | Provenance | Deliberate? |
|---|---|---|
| `%start document` | Yacc `%start` directive | yes — standard |
| `%%` section separator | Yacc (separates declarations from rules) | yes — standard |
| `lhs : sym sym \| alt ;` | Yacc rule syntax (colon, `\|` alternatives, `;` terminator) | yes — standard |
| UPPERCASE = terminal, lowercase = nonterminal | Dragon-book / Bison typographic convention | yes — convention |
| `/* empty */` for ε | Yacc idiom (empty RHS = epsilon) | yes, but cosmetic |
| `%terminals` | **my rename** of Bison's `%token` | my deviation |
| `#` line comments | **my addition** (YAML/shell style) — yacc actually uses `/* */` | my deviation, and *inconsistent* with the `/* empty */` above |

So: **Yacc/Bison base, with two personal deviations** (`%terminals`, `#` comments) and one inconsistency (mixing `#` and `/* */`). Since we're writing the generator ourselves, we get to fix these. My recommendation is to move *toward* Bison (less surprise, self-documenting if you've seen yacc) rather than away.

## 1b. Proposed formal syntax for the `.grammar` file

Here's the meta-grammar I propose we commit to — deliberately minimal so `lalrgen`'s own front-end stays tiny:

```
file    := decl* '%%' rule*
decl    := '%start' IDENT
         | '%token' IDENT+           # renamed from %terminals, matches Bison
rule    := IDENT ':' alt ('|' alt)* ';'
alt     := symbol* tag?              # empty symbol list OR '%empty' = ε
symbol  := IDENT                     # terminal iff declared in %token, else nonterminal
tag     := '=>' IDENT                # names the production for the action switch
comment := '#' … EOL                 # the ONE comment style; drop /* */
IDENT   := [A-Za-z_][A-Za-z0-9_]*
```

Explicit choices baked in, each with a reason:

- **`%token` not `%terminals`** — align with Bison; zero cost.
- **One comment style (`#`)** — kills the inconsistency; `#` is trivial to strip line-wise.
- **`%empty` for ε** — explicit beats the invisible empty-RHS-with-a-`/* empty */`-comment. Bison 3.0+ has exactly this.
- **Terminal-ness = "declared in `%token`"**, not casing — casing stays as *documentation*, but the generator resolves it authoritatively from the token list and errors on an undeclared symbol. Prevents silent typos becoming phantom nonterminals.
- **`=> name` production tags** — this is the important one, see the question below.

Applied to a slice of our grammar:

```
node : NODEID                          => node_bare
     | NODEID SHAPE                    => node_shape
     | NODEID TRIPLECOLON NODEID       => node_class
     | NODEID SHAPE TRIPLECOLON NODEID => node_shape_class
     ;

stmts : %empty                         => stmts_empty
      | stmts stmt NEWLINE             => stmts_push
      | stmts NEWLINE                  => stmts_blank
      ;
```

## 2. The codegen contract + parser driver

The generator emits **one header** (`flowchart_tables.inc`) that is a pure data interface — no logic. Concretely:

```cpp
enum class Term  { NEWLINE, AMP, /* … */, END };        // ACTION column indices
enum class NTerm { document, header, stmts, /* … */ };  // GOTO column indices
enum class Prod  { node_bare, node_shape, /* … from => tags */ };

struct Action { enum K : uint8_t { Error, Shift, Reduce, Accept } kind; uint16_t arg; };
struct ProdInfo { NTerm lhs; uint8_t rhs_len; Prod tag; };

constexpr int kNumStates = …;
constexpr Action  kAction[kNumStates][/*terms*/] = { … };  // arg = target state | production id
constexpr int16_t kGoto  [kNumStates][/*nterms*/] = { … }; // -1 = error
constexpr ProdInfo kProd[/*num productions*/] = { … };
```

The **runtime driver** (hand-written once, generic table interpreter) consumes it:

```cpp
// stacks
std::vector<int>   states{0};
std::vector<Value> values;                 // semantic value stack

for (;;) {
  Action a = kAction[states.back()][col(lookahead)];   // col(): Token -> Term
  switch (a.kind) {
    case Shift:  states.push_back(a.arg); values.push_back(valueOf(lookahead)); advance(); break;
    case Reduce: {
      const ProdInfo& p = kProd[a.arg];
      Value v = act(p.tag, &values[values.size() - p.rhs_len], builder);  // the semantic switch
      states.resize(states.size() - p.rhs_len);
      values.resize(values.size() - p.rhs_len);
      values.push_back(std::move(v));
      states.push_back(kGoto[states.back()][int(p.lhs)]);
      break;
    }
    case Accept: return builder.finish();
    case Error:  /* diagnostic */ return …;
  }
}
```

Two pieces the driver needs, both **hand-written in the mermaid module** (not generated):
- **`col(const Token&) → Term`** — the lexer→table bridge; a `std::visit` mapping each `TokenValue` alternative (and `Punct`) to a `Term`.
- **`act(Prod, Value* rhs, FlowDbBuilder&) → Value`** — the semantic-action `switch(prod)`. Shifted terminals push a `Value` carrying their token payload (the `IdTok` string, the `LinkTok` attrs…), so reductions read them by position. `Value` is a `std::variant` of partial-AST pieces (`Vertex`, `std::vector<Vertex>` for a nodeGroup, edge attrs, strings). A **`FlowDbBuilder`** owns the messy semantics we listed earlier — last-label-wins, the `&` cross-product, subgraph scoping via a stack, edge-length carry — keeping the `switch` thin.

So the division is clean: **generator → dumb tables**; **mermaid module → `col()`, `act()`, `FlowDbBuilder`, and the generic driver.**

## 3. Where the code lives — my opinion (I'd push back slightly on "another module")

I'd *not* make `lalrgen` a peer module of `markdown_parser`/`mermaid`, because it's a categorically different thing: those two are **runtime libraries linked into the product**; `lalrgen` is a **build-time host executable that emits source and is never shipped**. Lumping them together blurs that line. Concretely:

```
tools/lalrgen/            ← host tool: its own main.cpp + CMake, host toolchain,
  main.cpp                  built via ExternalProject. GENERIC (any .grammar).
  ...
src/mermaid/
  flowchart.grammar       ← input (already here)
  Parser.cpp / Parser.hpp ← generic driver + col() + act()  (the runtime)
  FlowDbBuilder.cpp/.hpp  ← semantic accumulation
build/…/flowchart_tables.inc  ← generated, never committed, #included by Parser.cpp
```

Rationale, explicitly:
- **`tools/lalrgen` is generic and reusable** — it knows nothing about mermaid; it turns *a* grammar into *tables*. Keeping it grammar-agnostic means it could later generate tables for anything (even a future markdown sub-grammar). A "mermaid module" home would wrongly imply mermaid-specificity.
- **The generated tables *are* mermaid's** — so they live in mermaid's build output and are owned by the mermaid module, not by the tool.
- **The driver stays in mermaid (for now)** — it's generic in principle, but we have exactly one consumer, so extracting a standalone "parser-runtime" library is premature (YAGNI). Easy to lift out later if a second consumer appears.

Net: `tools/` for the *tool*, `src/mermaid/` for the *runtime that uses its output*. That mirrors how real projects treat Bison — the tool sits in the toolchain, the generated `.c` and the driver code sit in the module.

## One decision before I build it

The production-tagging scheme shapes both the grammar file and the generated `Prod` enum, so let me pin it:

Sure — let me make it concrete, because it's the whole reason I'd avoid it.

## The setup

With **auto-numbering**, the generator walks the grammar file top to bottom and assigns each *alternative* (not each rule — each `|` branch is its own production) an integer in the order it sees them. So a grammar like:

```
node : NODEID              # production 0
     | NODEID SHAPE        # production 1
     ;
edge : LINK                # production 2
     | LINK PIPE STR PIPE  # production 3
     ;
```

The generated table says "production 1 has LHS `node`, pops 2 symbols," etc. Your semantic-action switch in `Parser.cpp` is written against those numbers:

```cpp
Value act(int prod_id, Value* rhs, FlowDbBuilder& b) {
  switch (prod_id) {
    case 0: return b.bareNode(rhs[0]);          // node : NODEID
    case 1: return b.shapedNode(rhs[0], rhs[1]);// node : NODEID SHAPE
    case 2: return b.plainEdge(rhs[0]);         // edge : LINK
    case 3: return b.labeledEdge(rhs[0], rhs[2]);// edge : LINK PIPE STR PIPE
    ...
  }
}
```

The numbers `0,1,2,3` exist **only in the generated table**. Nothing connects `case 2:` in your C++ to the `edge : LINK` line in the grammar except your memory that "2 is the plain-edge one." That coupling is invisible and unchecked.

## The break

Now suppose weeks later you add a new shape alternative to `node` — a totally reasonable edit:

```
node : NODEID                          # production 0
     | NODEID SHAPE                    # production 1
     | NODEID TRIPLECOLON NODEID       # production 2  ← NEW, inserted here
     ;
edge : LINK                            # production 3  ← was 2
     | LINK PIPE STR PIPE              # production 4  ← was 3
     ;
```

Inserting one line **shifted every subsequent production number by one**. The generator happily regenerates the table. But your switch is untouched:

```cpp
case 2: return b.plainEdge(rhs[0]);   // you MEANT edge:LINK, but 2 is now node:NODEID TRIPLECOLON NODEID
```

Now `case 2` fires on the wrong production. `b.plainEdge` gets handed the RHS of a `node` reduction (`NODEID ::: NODEID`) instead of an `edge`. And `case 3` (the new number for `edge:LINK`) has no handler, so it silently hits your `default`. 

The insidious part: **it still compiles, and it may still run** — it just builds the wrong `FlowDb`. `A:::primary` might get quietly turned into an edge, or an edge might vanish. You find out only when an AST-comparison test fails with a baffling diff, and you have no compiler error pointing at the cause. The same happens if you merely *reorder* two alternatives, or a teammate does.

## Why the alternatives don't have this problem

- **`=> tags`**: your switch says `case Prod::plain_edge:`. The tag `plain_edge` travels *with* the `edge : LINK` line in the grammar. Insert, delete, reorder anything — `plain_edge` still means that exact production. If you delete the rule but leave the `case`, the generated enum no longer has `Prod::plain_edge` and you get a **compile error** — the coupling is now checked by the compiler. That's the win: the binding is by *name*, and names are stable and verified.

- **Auto names from LHS+index** (`node_0`, `edge_0`): better than raw integers because inserting a *rule* elsewhere doesn't renumber `edge_0` — the name is scoped to its LHS. But reordering the `|` branches *within* `node` still swaps `node_0`/`node_1`, and the names don't say what they mean (`node_1` vs `node_shape`).

So the ranking is: **tags** (stable + readable + compiler-checked) > **LHS+index** (mostly stable, unreadable) > **raw auto-number** (fragile, silent breakage).

My recommendation stays **explicit `=> tags`**. The only cost is typing a name per production you care about (and untagged ones still get an auto name for the pass-through cases). Want me to go with tags and start building `tools/lalrgen`?

## ▸ Prompt — 2026-07-03 15:50

> Which is the approach taken by biison for this matter? because menhir let's you specify the actual types you want by importing them in the .mly file so you don't have this type of problems of naming the production. Is it the same in biison if no how do they resolve this (give the intuitive explaination)

Great question, and it exposes a real fork. Let me answer precisely, because the premise needs one correction.

## What Bison actually does

Bison does **not** do what I proposed. Bison — like Menhir and classic yacc — puts the semantic action **inline, glued directly to each production**:

```bison
expr : expr '+' expr   { $$ = $1 + $3; }
     | NUM             { $$ = $1; }
     ;
```

That `{ … }` block is physically attached to the production. So there is **no separate switch, and no production names or numbers that you ever write or maintain.** Internally Bison *does* number the rules and generates a giant `switch (yyn) { case 2: yyval = yyvsp[-2] + yyvsp[0]; … }` inside `yyparse` — but **Bison writes that switch itself** from your inline actions. You never see or touch the numbers. Insert a rule, reorder alternatives — Bison renumbers *and* regenerates the switch together, in one pass, from one source. They can't drift out of sync because they're generated from the same glued-together text.

So: **the renumbering problem cannot arise in Bison — not because it names productions, but because it never separates the action from the production in the first place.** There's nothing to keep in sync.

## The typing part — Bison has it too

Your intuition about Menhir is right (`%token <int> INT`, `%type <expr> expr`, real OCaml types, checked by the OCaml compiler). But Bison offers the analogous mechanism — it's just C and weaker:

```bison
%union { int ival; Node* node; }   /* the set of possible value types  */
%token <ival> NUM                  /* NUM's value lives in .ival        */
%type  <node> expr                 /* expr's value lives in .node       */
```

Then `$1`, `$$` are macros that expand to the correct union member. So **both** Bison and Menhir let you "specify the actual types." That typing is *not* what saves them from the naming problem — **inline actions** are. Typing and the binding-stability question are orthogonal.

## So there are two schools, and I picked the unusual one

| | Where actions live | Binding problem? | Cost |
|---|---|---|---|
| **Inline** (yacc/Bison/Menhir/ANTLR) | in the grammar file, `{…}` per production, `$$`/`$1` | none — co-located | grammar file contains host-language code; generator must parse & pass through those `{…}` bodies + a `%union` typing story; debugger steps into generated code |
| **Separated + tags** (what I proposed) | in `Parser.cpp`, `switch(Prod)` | reintroduced by the separation → solved by `=> tag` | grammar stays pure data; generator stays tiny; full IDE/debugger on real `.cpp` |

The `=> tag` I proposed exists **only because I separated the actions** — it re-creates, by hand, the stable binding that inline actions get for free.

## Why I still lean separated + tags *for us* specifically

The catch: Bison/Menhir can afford inline actions because they're mature tools that already solved the hard machinery — tokenizing and splicing arbitrary host-language `{…}` blocks, and the `%union`/`$$` typed-value plumbing. **We are writing the generator ourselves.** If we go inline, `lalrgen` must:
1. lex and pass through verbatim C++ action bodies (brace-matching, string/char literals inside them…),
2. implement a `%union`-equivalent and expand `$1/$$` into typed value-stack accesses,
3. and the generated file becomes C++-with-embedded-C++, so compiler errors and the debugger land in generated code.

Separated + tags lets `lalrgen` emit **pure tables and nothing else** — no C++ passthrough, no `$` macros, no union typing in the DSL — while all our semantic logic stays in a normal `.cpp` with real tooling. For a hand-rolled generator in a course project, that's a big simplification, and the only price is writing a tag name per production.

So my honest read: **Bison's inline model is the canonical, arguably nicer design — but it's nicer partly *because Bison already paid for the machinery*. For a generator we build ourselves, separated + tags gets 90% of the benefit for 20% of the generator complexity.**

Excellent instinct — what you've described is essentially **Menhir-lite**, and it's a real, clean design. Let me answer the direct question ("how is `Sum` actually created?") and then name the one ingredient you're missing, because it's the crux.

## How the generated code creates `Sum($1,$3)`

The parser keeps a **stack of semantic values**. The problem: values on that stack have *different* C++ types — a `node` reduction yields a `Vertex`, a `LINK` terminal yields a `LinkTok`, a `nodeGroup` yields a `vector<Vertex>`. A stack can only hold one type, so each slot is a **`std::variant` of every semantic type in the grammar**:

```cpp
using Value = std::variant<std::string, ShapeTok, LinkTok, Vertex, Sum, /* … */>;
std::vector<Value> stack;
```

Now, when `expr : expr '+' expr { Sum($1,$3) }` reduces, the generator emits roughly:

```cpp
case Prod::sum: {                       // the 3 RHS values are the top 3 slots
  Value out = Sum{ std::get<Expr>(rhs[0]),   // $1  — 'Expr' is expr's declared type
                   std::get<Expr>(rhs[2]) };  // $3  — rhs[1] is '+', punctuation, unused
  reduce(/*pop*/ 3, std::move(out));
  break;
}
```

The key line is `std::get<Expr>(rhs[0])`: to pull `$1` **out** of the variant, the generated code must name its static type, `Expr`. Once it has the values at their real types, `Sum{ …, … }` is an **ordinary C++ construction** — so the compiler checks that `Sum` has a matching constructor/field list. Wrong arity or wrong types → compile error, exactly the "spotted easily" property you want. The result goes back on the stack as a `Sum`.

## The missing ingredient: a declared type per symbol

Notice the generator had to know that `expr`'s type is `Expr` to write `std::get<Expr>`. **That's the piece you're missing:** every grammar symbol needs a declared semantic type, so the generator knows what to extract from the variant for each `$k`. This is exactly Menhir's `%token <int> INT` + `%type <expr> expr`. Two sources:

- **Terminals** — fixed mapping from the `Token` payload, declared once:
  `NODEID → std::string`, `SHAPE → ShapeTok`, `LINK → LinkTok`, `STR → std::string`, `DIR → Direction`, `NUM → std::string`. Punctuation (`PIPE`, `TRIPLECOLON`, `NEWLINE`…) has **no** value → can't be referenced by `$k`.
- **Nonterminals** — you declare them: `%type node Vertex`, `%type nodeGroup std::vector<Vertex>`, etc.

The generator collects every declared type → emits `using Value = std::variant<…all of them…>;`, plus an `%include "mermaid/…hpp"` directive at the top of the grammar so the generated file can see those types (Menhir's `%{ open Ast %}`). That's the whole machinery: **per-symbol types + a variant value stack + `std::get<T>` extraction + your constructor call.**

## The consequence for our architecture (and the one real choice)

Here's the catch your model creates, and it's actually a *good* thing. A pure constructor call like `Sum($1,$3)` can only **build a node from its children** — it can't run the *stateful* semantics we listed: `&` cross-product, last-label-wins, subgraph scoping, edge-length carry. Those aren't "construct a value," they're "walk the whole diagram." So the constructor-only model naturally splits parsing into two stages:

1. **Parse → typed AST.** Every action is a compiler-checked constructor building a small **Flow AST** (`NodeNode`, `EdgeStmtNode`, `SubgraphNode`, `Document`…). Trivial, no logic.
2. **Lower AST → `FlowDb`.** A separate, hand-written pass in the mermaid module — plain, testable, debuggable C++ — does *all* the stateful semantics.

This is strictly nicer than semantics-in-actions: actions stay pure and checked; the gnarly logic lives in normal `.cpp` with full tooling. It matches your "no embedded C++ logic in the grammar" preference exactly. The only cost is a thin new artifact: a `FlowAst.hpp` (a dozen small structs) and a `lower()` function.

So the fork your choice creates:

Locked: **two-stage** (constructor actions build a typed AST → a hand-written `lower()` produces `FlowDb`). Let me now nail down **(1) the finalized `.grammar` syntax** with everything we decided, since that's the contract the generator implements. One simplification first, because it makes the generator trivial:

## The action is an opaque expression with `$k` holes — the generator never parses C++

We don't need `lalrgen` to understand C++ at all. It treats each `{ … }` action as **opaque text**, and does one mechanical substitution: `$k` → `std::get<Tk>(slot_k)`, where `Tk` is the *declared type* of RHS symbol `k`. Then it emits `Value{ <that text> }`. The C++ compiler does all the checking. Rule: **no `{`/`}` inside an action** (so finding the action's end is trivial), and every action is a single expression — which in practice is always a call like `node($1,$2)`. That's your Menhir-lite model, with zero embedded logic.

This means the three ingredients are: **per-symbol types** (so the generator knows each `Tk`), a **`std::variant` value stack**, and the **verbatim expression**. I've written the finalized grammar accordingly:

That's the finalized syntax — every element is now either standard Bison (`%start`, `%token`, `%type`, `%%`, `:|;`) or a small, documented directive (`%include`, `%namespace`, `%empty`, `#` comments), and each action is a compiler-checked call.

## The AST contract the actions imply

The actions reference a fixed set of **factory functions** (all type-checked at compile time). This is the `FlowAst.hpp` I'll write alongside the generator:

```cpp
namespace mermaid::ast {
  // node types
  struct Node { std::string id; std::optional<ShapeTok> shape;
                std::optional<std::string> cls; };
  using  NodeGroup = std::vector<Node>;
  struct EdgeOp   { LinkTok link; std::optional<std::string> pipe_label; };
  struct EdgeStmt { std::vector<NodeGroup> groups; std::vector<EdgeOp> ops; };
  // …StyleStmt, ClassDefStmt, ClassStmt, LinkStyleStmt, ClickStmt, DirectionStmt…
  struct SubgraphNode; using SubgraphPtr = std::unique_ptr<SubgraphNode>;  // breaks the recursion
  using  Stmt = std::variant<EdgeStmt, SubgraphPtr, DirectionStmt, /*…*/>;
  struct Document { std::optional<Direction> direction; std::vector<Stmt> stmts; };

  // factories the grammar calls (overloaded on arity/type ⇒ compiler catches mismatches)
  Node node(std::string);            Node node(std::string, ShapeTok);
  Node node_cls(std::string, std::string);
  Node node_cls(std::string, ShapeTok, std::string);
  EdgeStmt chain(NodeGroup);         EdgeStmt chain(EdgeStmt, EdgeOp, NodeGroup);
  template<class T> std::vector<T> one(T);            // list start
  template<class T> std::vector<T> push(std::vector<T>, T);  // list grow
  template<class T> Stmt stmt(T);    // wrap any alternative into Stmt
  // … document(), some_dir(), style_stmt(), subgraph(), etc.
}
```

Note how `one`/`push` (generic) handle every left-recursive list — `nodeGroup`, `stmts`, `idList`, `numList` — so the "constructor-only" model covers list accumulation without any embedded logic. All the *stateful* semantics (`&` cross-product, last-label-wins, subgraph scoping, edge length) live nowhere in here — they go in the separate `lower(const Document&) → FlowDb` pass.

## Placement (part 3), settled

```
tools/lalrgen/            host tool, generic, host toolchain via ExternalProject
src/mermaid/
  flowchart.grammar       ✅ (input, done)
  ast/FlowAst.hpp         AST structs + factories (next)
  Parser.cpp / .hpp       generic table-driven driver + col() + Value variant
  Lower.cpp / .hpp        lower(Document) -> FlowDb  (all semantics, plain C++)
build/…/flowchart_tables.inc   generated, never committed, #included by Parser.cpp
```

`lalrgen` is a **build tool**, not a runtime module — it's generic (grammar→tables) and its output belongs to the mermaid module. That's why it lives in `tools/`, not beside `markdown_parser`/`mermaid`.

## Next step

The three artifacts are interdependent and only prove out when they compile together, so I'd build them in one pass:
1. **`FlowAst.hpp`** — the structs + factories above (so the grammar's actions have real targets).
2. **`tools/lalrgen`** — grammar front-end → LR(1)/LALR(1) construction → emit `flowchart_tables.inc` (+ the `Value` variant, `col()`, and the reduce routine with `$k` substitution). Fails loudly on any conflict.
3. **`Parser.cpp`** — the generic driver consuming the tables, plus CMake ExternalProject wiring.

That gets us tokens → `Document` AST, end to end, verifiable against the fixture. `lower()` → `FlowDb` and the AST-comparison test come right after.

Want me to start building — `FlowAst.hpp` first, then `lalrgen`?

## ▸ Prompt — 2026-07-03 16:45

> Before implementing the generator explain to me the steps you are going to do and the describe the generator structure plus the final structure of the generated parser

Good discipline — let me lay out three things before writing a line: **(A)** the algorithm/steps the generator runs, **(B)** the generator's own internal structure, and **(C)** the exact shape of what it emits + how the runtime consumes it.

---

## A. The steps — what `lalrgen` does, in order

This is the classic LALR(1) construction pipeline. Each phase feeds the next:

**1. Parse the `.grammar` → `GrammarSpec`.**
A small front-end lexes the meta-syntax (the directives, `:`/`|`/`;`, `%empty`, and the opaque `{ … }` action bodies) into an in-memory grammar: the symbol table, the production list (each = LHS, RHS symbols, action text, source line), and the directives (`%include`, `%namespace`, `%start`, per-symbol types). The action body is stored as a raw string with `$k` markers — never interpreted.

**2. Validate + assign ids.**
- Every RHS symbol is resolved to a terminal (if in `%token`) or nonterminal; an undeclared symbol is an error (catches typos).
- Each `$k` is checked: `k ≤ |RHS|`, and symbol `k` must be value-carrying (referencing a `$k` on `NEWLINE` etc. is an error).
- Assign integer ids: terminals `0..T-1` (with `$`/END last), nonterminals `0..N-1`, productions `0..P-1`.
- **Augment** the grammar with `S' → start $` so acceptance is a normal reduction.

**3. Compute `NULLABLE`, `FIRST` (and `FOLLOW`).**
Fixed-point passes over the productions. `FIRST` is needed to compute LR(1) item lookaheads; `NULLABLE` handles the `%empty` productions (`stmts`, `subgraphHead`). These are the standard worklist algorithms.

**4. Build the LR(1) automaton (canonical item sets).**
Start from `closure({ S' → · start $ })`, then repeatedly apply `GOTO(state, X)` for every symbol `X` to discover new states, using `closure` to expand. Each item is `(production, dot position, lookahead terminal)`. This yields the canonical collection of LR(1) states + the transition function.

I'm choosing **canonical-LR(1)-then-merge** (not DeRemer–Pennello lookahead propagation): it's the version that's easiest to implement *correctly*, and since our grammar is tiny, the transient extra states cost nothing.

**5. Merge to LALR(1).**
Fuse states whose **item cores** (the production+dot pairs, ignoring lookaheads) are identical, unioning their lookahead sets. This is what makes it LALR rather than canonical LR — smaller tables, and the only place "mysterious" LALR reduce/reduce conflicts could appear (I'll report them clearly if they do).

**6. Build ACTION/GOTO + detect conflicts.**
- Transition on a **terminal** → `shift` entry. Transition on a **nonterminal** → `GOTO` entry.
- A **complete item** `A → α ·` with lookahead `a` → `reduce` by that production on `a`. `S' → start · $` → `accept`.
- If a cell already holds a different action → **shift/reduce or reduce/reduce conflict** → **fail the build** with the state, the competing items, and the lookahead. This is the guardrail that keeps the grammar honest; a green build *proves* the grammar is LALR(1).

**7. Emit the generated file.**
Serialize the tables, the enums, the `Value` variant, the token→`Value`/`Term` mappings, and the reduce routine (with `$k` substituted). Details in section C.

**My incremental implementation order** (each step independently checkable): front-end + dump → FIRST/FOLLOW dump → automaton dump → table build + conflict check on our grammar → emitter → driver + parse the fixture. I won't move on from a phase until its dump looks right.

---

## B. The generator's internal structure (`tools/lalrgen/`)

Generic (knows nothing about mermaid) — grammar in, tables out:

```
tools/lalrgen/
  main.cpp            CLI: lalrgen <grammar> <out.inc>; orchestrates the phases
  GrammarSpec.hpp     data model: Symbol, Production{lhs,rhs,action,line}, directives
  GrammarParser.cpp   phase 1+2: meta-syntax front-end -> GrammarSpec, validation, ids
  Analysis.cpp        phase 3: NULLABLE / FIRST / FOLLOW
  Automaton.cpp       phase 4+5: Item, ItemSet, closure/goto, canonical collection, LALR merge
  Tables.cpp          phase 6: ACTION/GOTO build + conflict detection (throws with context)
  Emitter.cpp         phase 7: writes the generated C++ text
  CMakeLists.txt      host-tool target (built via ExternalProject in the main build)
```

Key internal data:
- `struct Item { int prod; int dot; std::set<int> lookahead; };`
- `struct State { std::vector<Item> items; std::map<int,int> trans; };` (symbol id → state id)
- `struct Action { enum{Error,Shift,Reduce,Accept} kind; int arg; };`
- The whole thing is a straight-line `main()` calling the phases; no cleverness, because the grammar is small and correctness > speed.

---

## C. The final structure of the generated parser

There's a deliberate **split**: the generator emits the *grammar-specific* pieces; a small **hand-written generic driver** in the mermaid module consumes them.

### C1. Generated file — `flowchart_tables.inc` (data + grammar-specific glue)

```cpp
// ==== GENERATED by lalrgen from flowchart.grammar — DO NOT EDIT ====
#include "mermaid/Token.hpp"          // from %include
#include "mermaid/ast/FlowAst.hpp"    // from %include
#include <variant>
namespace mermaid {                   // from %namespace
using namespace mermaid::ast;

// 1. the heterogeneous value-stack cell: union of every declared symbol type
using Value = std::variant<std::monostate, std::string, ShapeTok, LinkTok,
                           Direction, Node, NodeGroup, EdgeOp, EdgeStmt,
                           Stmt, std::vector<Stmt>, /*…*/ Document>;

// 2. column/row indices and production ids
enum class Term  : int { NODEID, STR, /*…*/ END };
enum class NTerm : int { document, header, stmts, /*…*/ };
enum : int { kNumStates = /*N*/, kNumTerms = /*T*/, kNumNTerms = /*M*/ };

// 3. the tables
struct Action  { enum K:uint8_t{Error,Shift,Reduce,Accept} kind; uint16_t arg; };
struct ProdInfo{ int16_t lhs; uint8_t rhs_len; };
constexpr Action   kAction[kNumStates][kNumTerms] = { /*…*/ };
constexpr int16_t  kGoto  [kNumStates][kNumNTerms] = { /*…*/ };   // -1 = error
constexpr ProdInfo kProd  [/*P*/] = { /*…*/ };

// 4. lexer -> parser bridge (generated because it depends on %token types)
inline Term  term_col  (const Token& t);   // Token variant -> Term index
inline Value term_value(const Token& t);   // Token payload -> Value cell

// 5. the reduce routine — one case per production, action text with $k
//    substituted to std::get<Tk>(std::move(rhs[k-1]))
inline Value reduce_action(int prod, Value* rhs) {
  switch (prod) {
    /* node: NODEID SHAPE {node($1,$2)} */
    case 12: return Value{ node(std::get<std::string>(std::move(rhs[0])),
                                std::get<ShapeTok >(std::move(rhs[1]))) };
    /* stmts: stmts NEWLINE {$1} */
    case  4: return Value{ std::get<std::vector<Stmt>>(std::move(rhs[0])) };
    /* … */
  }
}

// 6. diagnostics
constexpr const char* kTermName [kNumTerms]  = { /*…*/ };
constexpr const char* kSymName  [/*…*/]      = { /*…*/ };
} // namespace mermaid
```

Everything grammar-specific — the variant, the `std::get<Tk>` type choices, the switch — is generated, so the C++ compiler type-checks every action against `FlowAst.hpp`.

### C2. Hand-written generic driver — `src/mermaid/Parser.cpp` (grammar-agnostic)

```cpp
#include "flowchart_tables.inc"   // the generated file
namespace mermaid {

ParseResult parse(const std::vector<Token>& toks) {
  std::vector<int>   states{0};
  std::vector<Value> values;
  size_t ip = 0;

  for (;;) {
    Action a = kAction[states.back()][int(term_col(toks[ip]))];
    switch (a.kind) {
      case Action::Shift:
        states.push_back(a.arg);
        values.push_back(term_value(toks[ip]));   // inject the terminal's value
        ++ip;
        break;
      case Action::Reduce: {
        const ProdInfo& p = kProd[a.arg];
        Value v = reduce_action(a.arg, &values[values.size() - p.rhs_len]);
        states.resize(states.size() - p.rhs_len);
        values.resize(values.size() - p.rhs_len);
        values.push_back(std::move(v));
        states.push_back(kGoto[states.back()][p.lhs]);
        break;
      }
      case Action::Accept:
        return { std::get<Document>(std::move(values.back())), /*errors*/ {} };
      case Action::Error:
        return error_at(toks[ip], states.back());   // uses kTermName/kSymName
    }
  }
}
} // namespace mermaid
```

This loop is **~40 lines, written once, and never changes** — it's pure table interpretation. All the variability lives in the generated `.inc`. `parse()` returns a `Document` (the AST); `lower(Document) → FlowDb` is the separate next step.

### The generated-vs-handwritten split, summarized

| Piece | Where | Why |
|---|---|---|
| `Value` variant, enums, `kAction`/`kGoto`/`kProd`, `term_col`/`term_value`, `reduce_action`, name tables | **generated** (`.inc`) | all depend on the grammar/`%token`/`%type`/actions |
| shift/reduce loop, error reporting, public `parse()` API | **hand-written** (`Parser.cpp`) | grammar-agnostic; one consumer |
| `lalrgen` itself | **host tool** (`tools/lalrgen`) | generic grammar→tables compiler |

That's the whole shape. If this matches your mental model, I'll start implementing in the incremental order from section A — beginning with `FlowAst.hpp` (so actions have targets) and the `lalrgen` front-end (phases 1–2), each with a dump I can show you before moving on.

