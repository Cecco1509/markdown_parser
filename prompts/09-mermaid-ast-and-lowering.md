# AST types, token adapter, and the lowering stage

**Theme:** Mermaid engine

**Source transcript:** `7316746a-768b-435e-8961-a44e9b8886c9.jsonl`  
**Records:** 393–602

**Dates:** 2026-07-10 13:54 → 2026-07-10 15:04  
**User turns:** 9

**Related commits:**

- `c4473cb Add mermaid parser & layout engine`

---

## ▸ Prompt — 2026-07-10 13:54

> Describe the AST types you want to implemet

Before writing it, here's the full shape of `FlowAst.hpp`, derived directly from what the grammar's actions call. Every type and factory below is *required* by an action in [flowchart.grammar](src/mermaid/flowchart.grammar), so this is the exact contract.

## Design principles

- **It's a parse tree, not `FlowDb`.** The AST mirrors the grammar's structure faithfully and does *no* semantics. All the stateful work — `&` cross-product, last-label-wins, subgraph node membership, class resolution, direction defaulting, edge-length effects — is deferred to `lower(Document) → FlowDb`. Keeping the AST "dumb" is what lets the actions be pure compiler-checked constructors.
- **Reuse the lexer payloads.** `SHAPE` and `LINK` terminals already carry `ShapeTok` (`{label, ShapeKind}`) and `LinkTok` (`{stroke, head_end, head_start, length, label}`). The AST stores those directly rather than re-modeling them — DRY, and `lower()` just reads the fields. (The AST is independent of `FlowDb`, but it does depend on `Token.hpp` for these two structs.)
- **Recursion broken with a pointer.** `Stmt` can be a subgraph, and a subgraph contains `Stmt`s — a cycle. `SubgraphPtr = std::unique_ptr<SubgraphNode>` breaks it (and makes `Value` move-only, which the generated driver already handles by moving).

## Leaf / node structs

```cpp
namespace mermaid::ast {

struct Node {                    // a styledNode:  A / A[..] / A:::c / A[..]:::c
  std::string id;
  std::optional<ShapeTok> shape; // label + ShapeKind, or none (bare id)
  std::optional<std::string> cls;// :::className, or none
};
using NodeGroup = std::vector<Node>;      // the `A & B & C` group

struct EdgeOp {                  // one edge operator between two groups
  LinkTok link;                  // stroke/head/length/inline-label
  std::optional<std::string> pipe_label; // the -->|text| form
};

struct EdgeStmt {                // a chain:  g0 op0 g1 op1 g2 ...
  std::vector<NodeGroup> groups; // size N
  std::vector<EdgeOp> ops;       // size N-1
};

struct DirectionStmt { Direction dir; };
struct StyleStmt     { std::string target; std::string body; };
struct ClassDefStmt  { std::string name; std::string body; bool is_default = false; };
struct ClassStmt     { std::vector<std::string> ids; std::string cls; };
struct LinkStyleStmt { std::vector<std::string> indices; std::string body; bool is_default = false; };
struct ClickStmt     { std::string id; std::vector<std::string> args; };

struct SubgraphHead  { std::optional<std::string> id; std::optional<std::string> title; };
struct SubgraphNode;                              // fwd (recursion)
using  SubgraphPtr = std::unique_ptr<SubgraphNode>;

using Stmt = std::variant<EdgeStmt, SubgraphPtr, DirectionStmt, StyleStmt,
                          ClassDefStmt, ClassStmt, LinkStyleStmt, ClickStmt>;

struct SubgraphNode { SubgraphHead head; std::vector<Stmt> body; };

struct Document { std::optional<Direction> direction; std::vector<Stmt> stmts; };
```

A couple of field choices worth flagging:
- **`Node` carries both `shape` and `cls` as optionals** — the grammar's four `node` forms map to the four combinations, so one struct with two optionals covers all of them.
- **`EdgeOp` keeps `link` *and* `pipe_label`** — an inline label (`-- x -->`) rides on `link.label`, while the pipe form (`-->|x|`) lands in `pipe_label`. `lower()` picks whichever is present.
- **`ClassDefStmt` / `LinkStyleStmt` use an `is_default` flag** rather than a separate type, since `classDef default …` and `linkStyle default …` differ only in the target.
- **`SubgraphHead` splits `id` vs `title`** — `subgraph sg1[Title]` gives both; `subgraph "Title"` gives only a title (anonymous, `lower()` will synthesize an id).

## Factory functions (what the actions call)

**Two generics** handle every list and the statement wrapper:
```cpp
template<class T> std::vector<T> one(T x);                 // list start
template<class T> std::vector<T> push(std::vector<T> v, T x); // list grow
template<class T> Stmt           stmt(T x);                // wrap into Stmt
```
These cover `nodeGroup`, `stmts`, `idList`, `numList`, `clickBody`, and all eight `stmt` alternatives.

**Per-nonterminal factories** (name → return type), overloaded on arity/type so the compiler catches any mismatch:
```cpp
Document                document(std::optional<Direction>, std::vector<Stmt>);
std::optional<Direction> no_dir();      some_dir(Direction);
std::vector<Stmt>       nil_stmts();
EdgeStmt                chain(NodeGroup);   chain(EdgeStmt, EdgeOp, NodeGroup);
EdgeOp                  edge_op(LinkTok);   edge_op(LinkTok, std::string);
Node                    node(std::string);  node(std::string, ShapeTok);
Node                    node_cls(std::string, std::string);
Node                    node_cls(std::string, ShapeTok, std::string);
SubgraphPtr             subgraph(SubgraphHead, std::vector<Stmt>);
SubgraphHead            sg_head();  sg_head_id(std::string);
SubgraphHead            sg_head_id(std::string, ShapeTok);  sg_head_title(std::string);
DirectionStmt           direction_stmt(Direction);
StyleStmt               style_stmt(std::string, std::string);
ClassDefStmt            classdef_stmt(std::string, std::string);  classdef_default(std::string);
ClassStmt               class_stmt(std::vector<std::string>, std::string);
LinkStyleStmt           linkstyle_stmt(std::vector<std::string>, std::string);
LinkStyleStmt           linkstyle_default(std::string);
ClickStmt               click_stmt(std::string, std::vector<std::string>);
std::vector<std::string> pair(std::string, std::string);  triple(std::string, std::string, std::string);
```

## What this deliberately does *not* do (left for `lower()`)

To justify the "dumb tree": `lower(Document) → FlowDb` will be where we expand `A & B --> C & D` into four edges, apply last-label-wins on repeated node ids, compute each subgraph's node membership, resolve `:::`/`class`/`classDef`/`default` into per-vertex class lists, map `ShapeKind`/`Direction`/edge attributes into the final `FlowDb`, and default the direction when the header omits it.

If this shape looks right, I'll implement `FlowAst.hpp` exactly as above (structs + factories), then we can compile the generated parser against it.

## ▸ Prompt — 2026-07-10 14:01

> It looks right, proceed with this implementation

Implementing `FlowAst.hpp` exactly as described:

Now the real validation — regenerate the parser and compile it against `FlowAst.hpp` to prove every action type-checks:

Compiles clean against the real AST — every one of the 46 grammar actions type-checks. Just two unused-constant warnings from the emitter; let me drop those emitted constants (the array dims already encode the sizes, and only `kEndCol` is used):

These IDE diagnostics are the same stale/unconfigured IntelliSense (it claims `Symbol` has no `type` member, which it clearly does). Ground truth is the compiler — let me rebuild `lrgen` and confirm the warnings are gone:

Both clean now — `lrgen` builds with no warnings, and the generated parser compiles against `FlowAst.hpp` with zero errors/warnings.

## Milestone: the parser pipeline type-checks end to end

- ✅ [FlowAst.hpp](include/mermaid/ast/FlowAst.hpp) — all AST structs + the 30-odd factory functions, exactly as described.
- ✅ Every one of the 46 grammar actions compiles against those factories — the constructor-only, `$k`-substitution model works: if a factory signature didn't match a production's RHS, this compile would have failed. It didn't.
- ✅ `lrgen` still clean, grammar still conflict-free LR(1) (79 states).

So `tokens → parse() → Document` is now a fully-typed, compiling path (modulo the two glue pieces below). The constructor-checking design paid off — the compiler is now our guarantee that grammar and AST agree.

## What's left to actually *run* it

1. **Token adapter** (`Token → Lexeme`) — the small `yylex`-equivalent: map each lexer `Token` to a `(Terminal, Value)` pair. ~one line per terminal.
2. **CMake ExternalProject wiring** — build `lrgen` as a host tool, run it at build time to emit `flowchart_parser.{hpp,cpp}` into the build tree, and add the generated `.cpp` to the `mermaid` library.
3. Then **`lower(Document) → FlowDb`** and the **AST-comparison test** against the extract.mjs fixtures.

Natural next step is #1 + #2 together — they turn the compiling-but-inert parser into a callable `lex → parse → Document` you can exercise on `simple-mermaid.mmd`. Want me to do the token adapter and the CMake wiring next?

## ▸ Prompt — 2026-07-10 14:10

> Explain how you want to implement the Token adapter

Here's how I'll build the Token adapter — it's the small `yylex`-equivalent bridge, the one piece of hand-written glue between the lexer and the generated parser.

## What it does

The generated `parse()` consumes `std::vector<Lexeme>`, where `Lexeme { Terminal sym; Value value; }`. The lexer produces `std::vector<Token>`, where `Token` wraps a `TokenValue` variant. The adapter is a **total, mechanical map** `Token → Lexeme`: for each token, pick (a) the grammar **`Terminal`** it represents, and (b) the semantic **`Value`** the parser will push on its stack when it shifts that terminal.

Two observations make it trivial:
- The `Value` payloads *are* the lexer payloads — `ShapeTok`/`LinkTok` in the generated `Value` variant are the very same structs from `Token.hpp`, and `IdTok`/`StrTok`/`NumTok`/`StyleBodyTok` all carry `std::string`. So there's no conversion, just re-wrapping.
- Value-less terminals (punctuation, keywords) get `std::monostate` — the parser shifts it but never reads it (those symbols have no `$k`, which `lrgen` already validated).

## The mapping

| Token alternative | `Terminal` | `Value` |
|---|---|---|
| `IdTok` | `NODEID` | `x.text` (string) |
| `StrTok` | `STR` | `x.text` |
| `NumTok` | `NUM` | `x.text` |
| `StyleBodyTok` | `STYLEBODY` | `x.text` |
| `ShapeTok` | `SHAPE` | `x` (ShapeTok) |
| `LinkTok` | `LINK` | `x` (LinkTok) |
| `DirTok` | `DIR` | `x.dir` (Direction) |
| `PunctTok{p}` | *(switch on `p`)* | `monostate` |

…where the `PunctTok` switch is the obvious 1:1 — `Newline→NEWLINE`, `KwGraph→GRAPH`, `KwEnd→END`, …, and the lexer's trailing `PunctTok{End}` → `Terminal::End` (the `$end` sentinel).

## Implementation sketch

Two-level dispatch: `std::visit` over the `TokenValue`, with a nested `switch` for punctuation.

```cpp
namespace mermaid {

static Terminal punct_terminal(Punct p) {
  switch (p) {
    case Punct::Newline:     return Terminal::NEWLINE;
    case Punct::Ampersand:   return Terminal::AMP;
    /* … Comma, Pipe, TripleColon … */
    case Punct::KwGraph:     return Terminal::GRAPH;
    /* … all keywords … */
    case Punct::End:         return Terminal::End;
  }                          // no default -> -Wswitch flags a missing case
}

std::vector<Lexeme> to_lexemes(const std::vector<Token>& toks) {
  std::vector<Lexeme> out;
  out.reserve(toks.size());
  for (const Token& t : toks)
    out.push_back(std::visit(overloaded{
      [](const IdTok& x)        { return Lexeme{Terminal::NODEID,    Value{x.text}}; },
      [](const StrTok& x)       { return Lexeme{Terminal::STR,       Value{x.text}}; },
      [](const NumTok& x)       { return Lexeme{Terminal::NUM,       Value{x.text}}; },
      [](const StyleBodyTok& x) { return Lexeme{Terminal::STYLEBODY, Value{x.text}}; },
      [](const ShapeTok& x)     { return Lexeme{Terminal::SHAPE,     Value{x}}; },
      [](const LinkTok& x)      { return Lexeme{Terminal::LINK,      Value{x}}; },
      [](const DirTok& x)       { return Lexeme{Terminal::DIR,       Value{x.dir}}; },
      [](const PunctTok& x)     { return Lexeme{punct_terminal(x.punct), Value{}}; },
    }, t.value));
  return out;
}
}
```

## Why it's hand-written, and why that's safe

It can't be generated: it references *both* the lexer's `Token`/`Punct` (from `Token.hpp`) *and* the grammar's `Terminal`/`Value` (generated) — it's exactly the seam between the two subsystems, like flex's `return NODEID;` statements. But the compiler keeps it honest on both sides:
- `std::visit` **requires every `TokenValue` alternative** to be handled — add a token kind to the lexer without updating the adapter → compile error.
- the `punct_terminal` switch with **no `default`** → `-Wswitch` warns on any unhandled `Punct`.
- every `Terminal::X` / `Value{…}` is name- and type-checked against the generated header.

So although it's the one bridge we maintain by hand, drift is caught at compile time, not runtime.

## Placement

It lives in the `mermaid` module alongside the parse composition — `src/mermaid/FlowParse.cpp` — because it needs the generated `flowchart_parser.hpp` (build-tree). I'll pair it with a convenience entry point that composes the pipeline: `lex(src) → to_lexemes → parse(...)`, returning the `Document` for now (and later, once `lower()` exists, a `FlowDb`). `to_lexemes` itself stays an internal detail of that TU.

If that matches your expectation, I'll implement the adapter + the CMake ExternalProject wiring together, so `lex → parse → Document` becomes a runnable path we can exercise on `simple-mermaid.mmd`.

## ▸ Prompt — 2026-07-10 14:14

> Okay I like it, implement it

Implementing the adapter + CMake wiring now. First the public header and the `FlowParse.cpp` (adapter + composition):

Now the CMake wiring — build `lrgen`, run it at build time, feed the generated parser + adapter into the `mermaid` library:

Now a parse test, wired into `mermaid_tests`:

Now configure and build — this exercises the full codegen pipeline (host tool builds → runs → generated parser + adapter compile into the lib):

Full codegen pipeline built cleanly end to end — `lrgen` compiled → ran → generated parser + adapter + lexer linked into `libmermaid.a`. Now run the parse tests:

All 3 pass. Let me confirm the whole `mermaid_tests` suite (lexer + parse) is green, no regressions:

All 13 green (10 lexer + 3 parse). 

## Done: the parser is runnable end to end

The token adapter and CMake wiring are implemented and working. What's now in place:

- **[FlowParse.cpp](src/mermaid/FlowParse.cpp)** — the `Token → Lexeme` adapter (exhaustive `std::visit` + `punct_terminal` switch) plus `parse_flowchart()` composing `lex → adapt → parse`. It's the one hand-written bridge, kept honest by the compiler on both sides.
- **[FlowParse.hpp](include/mermaid/FlowParse.hpp)** — clean public API returning the `Document` AST, depending only on committed headers (not the build-tree generated one).
- **CMake wiring** — `lrgen` builds as a host tool, runs at build time to emit `flowchart_parser.{hpp,cpp}` into `build/gen/`, and those feed the `mermaid` library. Nothing generated is committed; editing the grammar re-triggers generation automatically.

**Verified behaviors** (real parse, real AST):
- `flowchart LR / A --> B` → direction, edge chain with 2 groups + 1 op, arrowhead.
- The `simple-mermaid` fixture → 4 edge statements, node shapes (`Rect`/"Start"), inline edge label (`Yes`).
- `E & F --> G` → fan-out group of 2; `subgraph box[Group] … end` → id `box`, title `Group`, nested body.

The chain **source → lexer → adapter → generated LR(1) parser → typed AST** is proven working.

## Remaining for a full flowchart pipeline

1. **`lower(Document) → FlowDb`** — the semantic pass (the `&` cross-product into edges, last-label-wins, subgraph membership, class/style resolution, direction defaulting).
2. **AST-comparison test** binding `lower()`'s `FlowDb` to the `extract.mjs` fixtures — closing your original verification loop.
3. Then layout + SVG.

`lower()` is the natural next step — it turns the parse tree into the `FlowDb` your verification strategy compares against. Want me to move on to it?

## ▸ Prompt — 2026-07-10 14:29

> How can I test with ctest?

`ctest` runs from the build directory and picks up every test registered via `gtest_discover_tests` (each `TEST(...)` becomes its own ctest case). You need the test binaries built first. Let me show it working:

`ctest` works — each `TEST(...)` is discovered as an individual case. From the `build/` directory:

## Common commands

```bash
# 1. Build the test binaries first (ctest only runs, it doesn't build)
cmake --build build -j4

cd build

ctest                       # run every registered test
ctest --output-on-failure   # run all, print output only for failures
ctest -j4                   # run in parallel
ctest -N                    # list tests without running (dry run)
```

## Selecting tests

```bash
ctest -R MermaidParse           # regex on test name — only MermaidParse.*
ctest -R 'MermaidLexer|Mermaid' # multiple, alternation
ctest -E spec                   # exclude tests matching "spec"
ctest -R SimpleEdge -V          # verbose (full gtest output for that case)
ctest --rerun-failed            # re-run only what failed last time
```

## Notes for this repo

- Tests come from three binaries (`md_tests`, `mermaid_tests`, `spec_tests`), all registered via `gtest_discover_tests`, so ctest sees the individual `TEST(...)` cases (e.g. `MermaidParse.SimpleEdge`), not just the executables.
- A freshly configured build hasn't compiled the test binaries yet — if `ctest` reports "Unable to find executable" or a test errors, run `cmake --build build -j4` (or the specific target, e.g. `--target mermaid_tests`) first. I only built `mermaid_tests` above; `cmake --build build` builds them all.
- The whole mermaid suite (10 lexer + 3 parse) passes under ctest: **13/13**.

One convenience worth adding if you want `make test`-style running: the top-level `CMakeLists.txt` already calls `enable_testing()`, so `ctest` is live; you could also add a `check` custom target that builds-then-runs, if you'd like. Want me to add that?

## ▸ Prompt — 2026-07-10 14:32

> How can I run only the mermaid parser? I want to visualize the generated AST

There's no CLI entry point for that yet — let me add one: an AST pretty-printer plus a small `mermaid_ast` executable that reads a `.mmd` (file arg or stdin) and prints the parsed tree.

Now wire the dumper into the lib and add the `mermaid_ast` executable:

You now have a CLI to run just the parser and visualize the AST.

## `mermaid_ast` — parse a diagram and print its AST

```bash
cmake --build build --target mermaid_ast    # build once

# from a file:
./build/mermaid_ast tests/test-files/simple-mermaid.mmd

# or from stdin (paste a diagram, Ctrl-D to end):
printf 'flowchart LR\nA & B --> C\n' | ./build/mermaid_ast
```

Output for the fixture (shown above): it prints the `Document` direction, then each statement — edge chains with their groups, node shapes (`Rect`/"Start", `Rhombus`), and edge attributes (`normal/arrow len=1`, inline `label="Yes"`). Errors (lexer + parser) go to stderr; exit code is nonzero on failure.

What was added:
- **[AstDump.cpp](src/mermaid/AstDump.cpp)** / **[AstDump.hpp](include/mermaid/AstDump.hpp)** — `dump_ast(Document)`, an indented tree printer covering every `Stmt` kind (edges, subgraphs, direction, style/classDef/class/linkStyle/click) with shapes, classes, and edge attributes.
- **[cli_ast.cpp](src/mermaid/cli_ast.cpp)** — the `mermaid_ast` executable (file arg or stdin), linking the `mermaid` library.

Try it on the fuller fixtures in [docs/mermaid/flowchart-spec.md](docs/mermaid/flowchart-spec.md) to eyeball how each construct parses. Quick way to test one:

```bash
printf 'flowchart TD\nA[x]:::hot --> B\nclassDef hot fill:#f00\nstyle A stroke:#333\n' | ./build/mermaid_ast
```

Want me to move on to `lower(Document) → FlowDb` next, or would you like a similar dump for a few more spec fixtures first to sanity-check the parser coverage?

## ▸ Prompt — 2026-07-10 14:48

> No its okay, let's move to lower, again let's define what are its duties

Good — here's what `lower(const ast::Document&) → FlowDb` is responsible for. Its job is to turn the "dumb" parse tree into the semantic diagram model, doing all the stateful work the AST deliberately skipped. Grouped by duty, grounded in the fixtures and the target `FlowDb`.

## Core duties

**1. Direction defaulting.** `FlowDb.direction = doc.direction.value_or(TB)`. The header may omit a direction; mermaid defaults to `TB` (the lexer already folds `TD`→`TB`).

**2. Vertex table — create-on-reference + label/shape merge.** Every node id mentioned anywhere (edge groups, subgraph bodies) must appear once in `FlowDb.vertices`, in **first-appearance order** (extract emits `A,B,C,D` in declaration order). A bare mention (`A`) just ensures the vertex exists with `label=id`; a mention carrying a shape/label (`A[Start]`) sets those. Repeated definitions trigger the **last-wins vs first-wins** question (see below). Backing store: a `map<id → index>` so re-references update in place.

**3. Edge generation — `&` cross-product + chaining.** For each `EdgeStmt` (groups `g0 op0 g1 op1 …`), each adjacent `(g_i, op_i, g_{i+1})` emits the **cross product** `g_i × g_{i+1}`, every edge carrying `op_i`'s attributes (`stroke`, `head_end`, `head_start`, `length`). So `A & B --> C & D` → 4 edges (F-06), and `A --> B --> C` → 2 edges. Edge **label** = `pipe_label` if present, else `link.label`. **Source order preserved** — the 0-based edge index matters for `linkStyle` and for fixture comparison, so the cross-product nesting order must match mermaid's (left-group outer, right-group inner).

**4. Subgraph lowering — recursive, with membership.** Each `SubgraphNode` becomes a `FlowDb.Subgraph`, but its body's edges/nodes are **also lowered into the global vertices/edges** (a subgraph's edges are real edges). So this is a recursive walk over the body with a "current subgraph" context that collects **node membership** (`Subgraph.nodes` = ids referenced in the body). Also: synthesize an `id` when anonymous (`subgraph "Title"`), take `label` from the title, pull the per-subgraph `direction` from a `DirectionStmt` in the body (F-07), and add a **nested subgraph's id as a member node** of its parent.

**5. Class resolution.** `classDef name …` → `FlowDb.classes[name]` with styles split from the body; `classDef default …` → `classes["default"]`. `:::cls` on a node and `class A,B name` statements → append to each target vertex's `classes` list (F-09).

**6. Inline styles.** `style A fill:…` → split the body into style entries and append to vertex `A`'s `styles` (F-08).

## Deferred (parsed, but not into `FlowDb` yet)

These don't appear in the extract.mjs AST we compare against, or need model extensions — so `lower()` will ignore or stash them for later, not block on them:
- **`linkStyle`** (F-10) — per-edge render styling; `FlowDb.Edge` has no style field, and extract's edges don't carry it. Deferred to the layout/render stage.
- **`click`** (F-11) — sets `link`/`tooltip` on a vertex; our `Vertex` lacks those fields. Low priority.
- **Front-matter `title`** (F-12) — the lexer captures front-matter raw but doesn't parse YAML; `title` stays empty (matches the `simple-mermaid` fixture). Minimal `title:` extraction later.

## To pin empirically against extract.mjs

A few behaviors the spec is ambiguous or silent on — I'll confirm each by running `extract.mjs` on a targeted fixture and matching:
1. **Label reuse: last-wins or first-wins?** The spec *contradicts itself* — F-03's inline comment says the second definition is "silently dropped" (first-wins), but the Warnings section says "last label wins." One-line policy switch; I'll match whatever mermaid actually emits.
2. **Cross-product edge order** for `A & B --> C & D` (which of the 4 edges comes first).
3. **Anonymous subgraph id** scheme — mermaid names them `subGraph0`, `subGraph1`, …; the `id` must match for comparison.
4. **Subgraph id used in an edge** (`sg1 --> sg2`, F-07) — does it also become a vertex?

## Proposed shape

A stateful `Lowering` builder: `FlowDb lower(const ast::Document&)`, with a private `lowerStmts(const std::vector<Stmt>&, Subgraph* current)` reused for the top level (`current = nullptr`) and recursively for subgraph bodies. It owns the vertex index map, the edge vector (for order), the subgraph counter, and the classes map. Files: `include/mermaid/Lower.hpp`, `src/mermaid/Lower.cpp`.

If that lines up, I'll implement it — and since the AST-comparison test is what resolves the four empirical questions, I'd build `lower()` and the comparator/fixture test together so we can immediately verify. Want me to proceed that way?

## ▸ Prompt — 2026-07-10 14:58

> Why linkStyle, click and Front-matter title are not in FlowDb?

Fair challenge — and re-reading my own answer, I conflated three genuinely different situations. Let me separate them precisely against what `extract.mjs` actually serializes (which is our comparison target), because I was sloppy.

Here's what the extractor captures ([extract.mjs:119-166](mermaid-utils/extract.mjs#L119)):
- **vertices**: `id, label, shape, classes, styles, tooltip, link, domId`
- **edges**: `index, id, start, end, label, stroke, arrowHead, length` — **no style**
- top level: **`title`**, direction, vertices, edges, subgraphs, classes

So, item by item, the honest picture:

**Title — I was wrong; it *is* in `FlowDb`.** `FlowDb` already has `std::string title` ([FlowDb.hpp](include/mermaid/FlowDb.hpp)), and extract captures it via `getDiagramTitle()`. So nothing is missing here. What's deferred is only *populating* it — the lexer captures front-matter raw but doesn't parse the YAML `title:` yet. I mislabeled "not populated yet" as "not in FlowDb." Correction: it's in the model; only the fill-in is deferred.

**click (tooltip/link) — genuinely missing from `FlowDb`, and that's a scope choice, not a necessity.** Extract *does* serialize `tooltip` and `link` per vertex, but my `Vertex` is minimal (`id, label, shape, classes, styles`) — no `tooltip`/`link`. So to verify F-11 at all, `Vertex` needs those fields. I framed this as "extract doesn't carry click," which is **false** — it does. The real reason they're absent is that I kept `Vertex` lean earlier. Cheap to fix.

**linkStyle — genuinely not in the comparison target, but it *will* be needed.** Extract's edges carry no style field, so `linkStyle` is invisible to AST verification — omitting it from `FlowDb.Edge` doesn't hurt the comparison. *But* it's real rendering information (per-edge stroke/color by index, F-10), so it has to live somewhere before the SVG stage. So it's not "doesn't belong" — it's "not needed for the AST gate, needed later for render."

So the accurate summary:

| | In extract output? | In `FlowDb` now? | Verdict |
|---|---|---|---|
| `title` | ✅ | ✅ (field exists) | only *populating* deferred (needs YAML parse) |
| click `tooltip`/`link` | ✅ | ❌ | **gap** — add to `Vertex` if we want F-11 fidelity |
| `linkStyle` | ❌ | ❌ | fine for AST gate; needed at render time |

My recommendation: **add `tooltip`/`link` (and `link_target`) to `Vertex` now** — it's a few fields, it matches what extract compares, and it lets F-11 be verified rather than silently skipped. Keep `title` as-is (populate when we parse front-matter). Defer the `linkStyle`/edge-style representation to the render stage, since neither the AST comparison nor any current consumer needs it.

Got it — deferring both, and I'll write a deferral log so it's not lost. Creating that now:

Deferral log written to [docs/mermaid/deferred.md](docs/mermaid/deferred.md) — it records click, linkStyle, front-matter title, and entity decoding, each with *why* it's deferred, *what extract.mjs does/doesn't capture*, and the concrete steps to un-defer, plus the out-of-scope (`@{shape}`, edge IDs) items.

## `lower()` duties — settled

With both deferred, `lower(const ast::Document&) → FlowDb` is responsible for exactly these six:

1. **Direction defaulting** — `doc.direction.value_or(TB)`.
2. **Vertex table** — create-on-reference, first-appearance order, label/shape merge on repeats.
3. **Edge generation** — adjacent-group `&` cross-product + chaining, attributes copied, source order preserved.
4. **Subgraph lowering** — recursive; global edges/vertices + membership collection, id synthesis, per-subgraph direction, nested-as-node.
5. **Class resolution** — `classDef`/`default` → `classes`; `:::`/`class` → vertex `classes`.
6. **Inline styles** — `style A …` → vertex `styles`.

It will **skip** `ClickStmt` and `LinkStyleStmt` entirely (per the log), leave `title` empty, and keep labels raw.

Four behaviors get pinned empirically against `extract.mjs` when we build the comparator: label reuse (last vs first-wins), cross-product edge order, anonymous subgraph id naming, and whether a subgraph id in an edge becomes a vertex.

I'll implement `lower()` together with the AST-comparison test + fixture harness, so those four questions get resolved by real comparison rather than guesswork. Ready to proceed?

## ▸ Prompt — 2026-07-10 15:04

> yes proceed with the implementation of lower()

Before coding `lower()`, let me resolve the four empirical questions definitively by running `extract.mjs` on probe fixtures — that way I implement to match mermaid, not guess.

All four questions resolved by ground truth:

| Question | Answer |
|---|---|
| **Q1 label reuse** | **last-wins** — `id1` → "second label" (confirms the Warnings note, not the inline comment) |
| **Q2 cross-product order** | **A→C, A→D, B→C, B→D** — left-group outer, right-group inner |
| **Q3 anon subgraph id** | `subGraph<N>` where N counts **all** subgraphs (named `named` = #0, anon = `subGraph1`) |
| **Q4 subgraph id in edge** | **becomes a vertex** — `sg1` appears in `vertices` |

Also learned: subgraph `nodes` come back **unordered-ish** (`[B,A]` for `A-->B`), so the comparator should treat membership as a **set**. Now implementing `lower()` to match:

`split_styles` uses `std::isspace` — needs `<cctype>`:

Same unconfigured-IntelliSense noise (it claims `ast::Node` has no `shape`, etc. — all real members). Compiler is truth. Let me wire `Lower.cpp` into the library, add tests, and build:

All 8 `lower()` tests pass — each asserting the exact behavior confirmed from `extract.mjs`. Full suite check:

21/21 green across lexer, parse, and lower.

## `lower()` implemented and verified

[Lower.cpp](src/mermaid/Lower.cpp) / [Lower.hpp](include/mermaid/Lower.hpp) turn the parse tree into `FlowDb`, doing all six duties — and crucially, each ambiguous behavior is implemented to **match mermaid's ground truth**, which I pinned by running `extract.mjs` on probe fixtures first:

- **Vertex table** — create-on-reference, first-appearance order, **last-label-wins** on repeats (Q1 confirmed).
- **Edges** — `&` cross-product in **left-outer/right-inner order** (Q2 confirmed), chaining, attributes + label copied.
- **Subgraphs** — recursive, set-based membership, **`subGraph<N>` id synthesis with a counter over all subgraphs** (Q3 confirmed), per-subgraph `direction`, nested-as-node; a subgraph id used in an edge **becomes a vertex** (Q4 confirmed).
- **Classes/styles** — `classDef`/`default` → `classes`; `:::`/`class` → vertex `classes`; `style` → vertex `styles`.
- **Direction** defaulting to `TB`.
- **Deferred** (per [deferred.md](docs/mermaid/deferred.md)): `click`, `linkStyle` are no-ops; `title` empty.

The 8 unit tests assert exactly the probe results, so `lower()` is behaviorally locked to mermaid for the constructs covered.

## Next: close the verification loop

The natural next step is the **AST-comparison harness** — the piece from your original strategy: for each `.mmd` fixture, compare `lower(parse(src))` against the `extract.mjs` `.ast.json`, via a normalizing comparator that:
- maps our `ShapeKind`/`Stroke`/`ArrowHead` → mermaid's string names,
- drops non-portable fields (`domId`, mermaid's `L_A_B_0` edge ids),
- compares subgraph `nodes` as **sets**,
- and covers the F-01…F-10 fixtures (skipping the deferred/out-of-scope ones).

That turns "8 hand-written unit tests" into "verified against real mermaid output across the whole fixture corpus." Want me to build that comparator + fixture harness next?

