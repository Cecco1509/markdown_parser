# Golden-file verification against the official Mermaid library

**Theme:** Mermaid engine

**Source transcript:** `7316746a-768b-435e-8961-a44e9b8886c9.jsonl`  
**Records:** 603–770

**Dates:** 2026-07-10 15:16 → 2026-07-10 15:50  
**User turns:** 5

**Related commits:**

- `9dff979 add mermaid tests files`
- `106d944 add mermaid tests`
- `89daa4f add mermaid test`

---

## ▸ Prompt — 2026-07-10 15:16

> Describe the verification this last verification step

Here's the design of the AST-verification harness — the piece that closes your original loop by comparing `lower(parse(src))` against pre-generated `extract.mjs` output across the fixture corpus.

## Goal & shape of the check

For each `.mmd` fixture: build our `FlowDb`, load the committed `.ast.json`, **normalize both into one canonical form**, and deep-compare, reporting field-level diffs on mismatch. The AST comparison is the **gate**; the SVG check is separate and comes at the render stage (self-consistency, not a diff against mermaid's SVG — as we settled earlier).

## The two schemas

They don't line up 1:1, so a normalizer reconciles them:

| Concept | Ours (`FlowDb`) | extract `.ast.json` |
|---|---|---|
| direction | `Direction` enum | `"TB"` string |
| vertex | `{id,label,shape:ShapeKind, classes[], styles[]}` | `{id,label,shape:string, classes[], styles[], tooltip, link, domId}` |
| edge | `{start,end,label,stroke,head_end,head_start,length}` | `{index,id,start,end,label,stroke:string,arrowHead:string,length}` |
| subgraph | `{id,label,nodes[],dir:opt}` | `{id,label,nodes[],direction}` |
| classes | `map<name,{id,styles[]}>` | `{name:{id,styles[],classes[]}}` |

## Normalization rules

**Enum → mermaid string** (confirmed ones from fixtures, the rest pinned by probing F-02/F-04 exactly like I did for the four questions):
- shape: `Rect→"square"` ✓, `Rhombus→"diamond"` ✓; `RoundEdges→round`, `Circle→circle`, `Asymmetric→odd`, `Hexagon→hexagon`, `LeanRight→lean_right`, `Trapezoid→trapezoid`, `TrapezoidAlt→inv_trapezoid`, `DoubleCircle→doublecircle`, … *(to pin against F-02)*.
- arrowHead: `Arrow→"arrow_point"` ✓; `None(open)→arrow_open`, `Circle→arrow_circle`, `Cross→arrow_cross` *(to pin against F-04)*.
- stroke: `Normal→"normal"` ✓; `Thick→thick`, `Dotted→dotted`, `Invisible→invisible` *(to pin)*.
- direction: enum → `"TB"/"BT"/"LR"/"RL"`.

**Dropped** (non-portable, per our earlier decision): vertex `domId`, edge `id` and `index` (position implied by order).

**Ignored — deferred** (per [deferred.md](docs/mermaid/deferred.md)): vertex `tooltip`/`link` (click not lowered), and `title` compared only once front-matter parsing lands (fixtures in scope have `title:""`).

**Structural**: vertices compared as a **map keyed by id** (label, mapped shape, classes as a **set**, styles); edges compared as an **ordered list** (order = linkStyle index) on `start/end/label/stroke/arrowHead/length`; subgraphs keyed by id with `nodes` as a **set** (Q from before); classes keyed by name on styles.

## Empirical items to pin first (same probe method)

Before writing assertions, I'll generate F-02 and F-04 fixtures and read the exact strings, plus resolve one real unknown: **bidirectional edges** (`<-->`). Our model has `head_start`, but extract's `serializeEdges` emits *only* `arrowHead` (no start/bidir field) — so I need to see how mermaid encodes `<-->` (likely a distinct `type` string) and either map to it or exclude `head_start` from the comparison.

## Harness mechanics

- **Fixtures**: extract the in-scope F-01…F-10 blocks from [flowchart-spec.md](docs/mermaid/flowchart-spec.md) into `tests/test-files/*.mmd`, and pre-generate `.ast.json` via `extract.mjs` (committed). A documented/regen target re-runs extraction when fixtures change.
- **Path injection**: `target_compile_definitions(mermaid_tests PRIVATE FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/test-files")` so the test finds the files.
- **Parameterized test**: a `TEST_P` over the fixture list; each is one ctest case (e.g. `Fixture/F-04`). Deferred/out-of-scope fixtures (F-11 click, F-12/13 front-matter, F-15) are **skipped** with a documented reason, not silently omitted.
- **Reporting**: on mismatch, print the specific path — e.g. `edge[2].arrowHead: expected "arrow_circle", got "arrow_cross"` — so failures are debuggable.

## The one decision: how to read the extract JSON in the test

The comparator must parse `.ast.json`, and the repo has no JSON facility:

Let me look at how the existing commonmark spec test reads its JSON:

That settles the JSON question cleanly — **nlohmann/json is already a test dependency** (the commonmark `spec_tests` pull it via FetchContent). So I'll reuse it and mirror that harness's exact structure. Here's how the pieces map onto that established pattern, plus the one mermaid-specific addition.

## Mirroring the commonmark pattern

| commonmark `spec_tests` | mermaid AST harness |
|---|---|
| `commonmark_spec_case.hpp` — struct + `from_json` + `loadSpec()` | `flow_fixture.hpp` — `ExtractAst` structs + `from_json` to load a `.ast.json`, plus the fixture list (`.mmd`/`.ast.json` pairs) |
| `TEST_P` over `kAllCases` | `TEST_P` over the fixtures; each `.mmd` → `lower(parse())` → `FlowDb`, compared to its `ExtractAst` |
| `EXPECT_EQ(tc.html, actual)` with boxed diff | `EXPECT_EQ` on **canonical JSON** of both sides, with a boxed diff |
| `kKnownFailures` + `GTEST_SKIP` | skip set for deferred fixtures (F-11 click, F-12/13 front-matter, F-15) with documented reasons |
| `SPEC_FILE_PATH` compile-def | `FIXTURE_DIR` compile-def |
| links `nlohmann_json` | links `nlohmann_json` + `mermaid` |

## The comparison mechanism (the mermaid-specific part)

Since we compare a *structured* AST (not one HTML string), the elegant move — using nlohmann/json we already have — is to **canonicalize both sides into `nlohmann::json` and `EXPECT_EQ` them**. nlohmann's `json::operator==` and pretty-printer give the comparison and a readable diff for free. The canonicalizer is where all the normalization I described lives, with container choices that make `operator==` semantically correct:

- **`FlowDb → canonical json`**: enum→mermaid-string maps (`Rect→"square"`, `Arrow→"arrow_point"`, …), and:
  - `vertices` as a json **object keyed by id** (so key order doesn't matter), each `{label, shape, classes, styles}` with **classes sorted** (set semantics);
  - `edges` as a json **array** (order matters — linkStyle index);
  - `subgraphs` keyed by id, `nodes` **sorted** (set semantics);
  - `classes` as object keyed by name.
- **`.ast.json → same canonical json`**: load via `from_json`, then **drop** `domId`, edge `id`/`index`, and (deferred) `tooltip`/`link`; sort the same set-valued fields.

Both sides become the same shape, so `EXPECT_EQ(ours, theirs)` is a correct structural comparison, and a failure prints the two canonical JSON blobs for a clear field-level diff.

## Before writing assertions

Same probe method as the four questions: generate F-02 and F-04 fixtures via `extract.mjs` and read the exact **shape**/**arrowHead**/**stroke** strings to fill the enum maps, and resolve the **bidirectional** `<-->` encoding (our `head_start` vs extract's single `arrowHead` field).

That's the whole harness: the commonmark pattern + a `FlowDb`↔extract canonicalizer. If that's what you had in mind, I'll build it — starting by probing F-02/F-04 to pin the enum strings, then the `flow_fixture.hpp` + canonicalizer + `TEST_P`, with in-scope fixtures generated and deferred ones skipped. Proceed?

## ▸ Prompt — 2026-07-10 15:26

> Yes this is what I was thinking, please proceed implementing the verification

Proceeding. First, probe F-02 (shapes) and F-04 (edges) via `extract.mjs` to pin the exact strings and the bidirectional encoding:

All strings pinned. Key findings:

**Shapes** → `square, round, stadium, subroutine, cylinder, circle, odd, diamond, hexagon, lean_right, lean_left, trapezoid, inv_trapezoid, doublecircle`.

**arrowHead is a function of both head *and* bidirectionality** — and importantly, mermaid only doubles the *arrow* head:
- `-->`→`arrow_point`, `<-->`→`double_arrow_point`, `---`→`arrow_open`, `--o`→`arrow_circle`, `--x`→`arrow_cross`
- but `<--o`→`arrow_circle` and `<--x`→`arrow_cross` (**same as non-bidirectional** — no `double_` for circle/cross)

So the canonicalizer folds `head_start` into the arrowHead string only for the arrow case. Now let me create the fixture corpus and generate their `.ast.json`:

All `.ast.json` generated. Let me spot-check a few for normalization edge cases (bare-node default shape, subgraph direction default, null edge labels) before writing the canonicalizer:

Critical finding: **a bare node is shape `"rect"`, but `[text]` is shape `"square"`** — mermaid distinguishes "default/bare" from "explicit rectangle", but our model collapses both to `ShapeKind::Rect`. I need to preserve that distinction. Everything else normalizes cleanly (subgraph dir `null` when unset, edge labels are `""` not null, classes match). Let me add an `explicit_shape` flag:

Let me update `apply_node` to set the flag, then build to confirm (IDE noise aside):

Same unconfigured-IntelliSense noise. Now the harness — canonicalizer + fixture loader:

Now the parameterized test, mirroring the commonmark spec pattern:

Now wire nlohmann/json, the fixture dir, and the new test file into `mermaid_tests`:

Filter didn't match the parameterized suite name. Let me run with the right filter:

9/10 pass. The one failure is revealing: **mermaid does *not* deduplicate class assignments** — extract has `C: ["primary","primary"]` (from `:::primary` *and* `class C primary`), while I dedup to `["primary"]`. Let me match mermaid by not deduping classes:

`lower_class` needs the same fix (the second `primary` on `C` comes from the `class` statement):

`add_unique` is still used for subgraph membership, so no unused-function issue. Rebuild and run the full suite:

**31/31 pass** — including all 10 fixture comparisons against real mermaid output. The AST verification loop from your original strategy is now closed.

## Verification harness — done and green

The harness mirrors the commonmark `spec_tests` pattern exactly:
- **[flow_fixture.hpp](tests/mermaid/flow_fixture.hpp)** — canonicalizes both `FlowDb` and the extract `.ast.json` into the same `nlohmann::json` shape (enum→mermaid strings, drop `domId`/edge-`id`/`index`, vertices/subgraphs/classes keyed by id, edges ordered, set-fields sorted), so `json::operator==` is a correct structural compare with a readable diff.
- **[test_flow_ast.cpp](tests/mermaid/test_flow_ast.cpp)** — `TEST_P` over the fixture corpus; deferred/out-of-scope fixtures omitted per [deferred.md](docs/mermaid/deferred.md).
- CMake wires `nlohmann_json` + `FIXTURE_DIR` into `mermaid_tests`.

**10 fixtures pass against mermaid ground truth**: `f01_directions, f02_shapes, f04_edges, f05_length, f06_chaining, f07_subgraphs, f08_style, f09_class, f10_linkstyle, simple-mermaid` — covering directions, all 14 shapes, every edge head/stroke, lengths, `&` chaining, subgraphs (nested membership + per-subgraph direction), inline styles, classes, and (ignored) linkStyle.

## Ground-truth behaviors this pinned

Building the harness surfaced real mermaid quirks I matched by probing, not guessing:
- **arrowHead folds bidirectionality** — only `double_arrow_point` exists; `<--o`/`<--x` stay `arrow_circle`/`arrow_cross`.
- **bare node = `"rect"`, `[text]` = `"square"`** — needed a new `Vertex::explicit_shape` flag since both are `ShapeKind::Rect`.
- **classes are *not* deduplicated** — `:::primary` + `class C primary` yields `["primary","primary"]`; fixed `lower()` to match.
- last-wins labels, `A→C,A→D,B→C,B→D` cross-product order, `subGraph<N>` ids, subgraph-id-becomes-vertex (all from the earlier probes).

## Where we are

The mermaid pipeline is now **source → lexer → generated LR(1) parser → AST → `lower()` → `FlowDb`**, verified end-to-end against mermaid across the fixture corpus. Remaining from the original plan:

1. **Layout** — the minimal Sugiyama pass (`FlowDb` → positioned nodes/edges), respecting direction.
2. **SVG renderer** — emit mermaid-classed SVG; verify via self-consistency (not a pixel/structural diff against mermaid's SVG, per our earlier decision).
3. Wire `MermaidHandler` to replace the passthrough with real inline SVG.

The `.svg` files were also pre-generated alongside each `.ast.json`, ready for the render-stage checks. Want to move on to the layout engine next?

## ▸ Prompt — 2026-07-10 15:38

> Build more tests with @mermaid-utils/extract.mjs  and them to the test suite. then please use ctest to run the tests

Good idea — more fixtures means more of the subset verified against real mermaid. Let me create several new ones covering labels, comments, nested subgraphs, chained/pipe labels, and a composite, then generate their expected ASTs:

Now add them to the fixture list:

Now build and run via ctest:

All 15 fixtures pass — including the new **`f18_composite`** (stadium shapes + subgraph with direction + classes + cross-edge + edge-to-subgraph + inline style) and **`f07b_nested`** (nested subgraphs), both first try. Let me run the whole mermaid suite through ctest for the full picture:

**36/36 mermaid tests pass** via ctest (10 lexer + 3 parse + 8 lower + 15 fixture comparisons).

## Added 5 new fixtures, all verified against mermaid

| Fixture | Constructs exercised |
|---|---|
| `f03_labels` | bare node, plain/quoted/unicode labels, last-wins reuse |
| `f04b_chainlabels` | chained inline labels (`A -- one --> B -- two --> C`), pipe label, thick labeled edge |
| `f07b_nested` | nested subgraphs, inner-subgraph-as-member-node |
| `f14_comments` | `%%` comment lines between statements |
| `f18_composite` | stadium shapes, subgraph + `direction`, classDef/`:::`, cross edge (`--x`), edge→subgraph, inline `style` — all together |

The corpus now stands at **15 fixtures** covering directions, all 14 shapes, every edge head/stroke/length, `&` chaining, labels (inline + pipe + chained), nested subgraphs, comments, styles, classes, and a realistic composite — every one matching `extract.mjs` ground truth. The new ones passed on the first run, which is a good sign the semantics generalize rather than being fixture-fit.

Running them:

```bash
cmake --build build --target mermaid_tests -j4
cd build
ctest -R FlowAst              # just the fixture comparisons (15)
ctest -R 'Mermaid|FlowAst'    # all mermaid tests (36)
ctest --output-on-failure     # everything, verbose on failure
```

When you add a new `.mmd` to `tests/test-files/`, the flow is: generate its expected AST with `node mermaid-utils/extract.mjs tests/test-files/<name>.mmd`, add the basename to `kFixtures` in [test_flow_ast.cpp](tests/mermaid/test_flow_ast.cpp), rebuild, and `ctest`.

Next up whenever you're ready: the **layout engine** (`FlowDb` → positioned graph) and the **SVG renderer**. Want to proceed there?

## ▸ Prompt — 2026-07-10 15:42

> update README.md inside mermaid-utils explaining how to generate tests, also add comments in the test files that points to that readme

Let me read the current mermaid-utils README first:

The README documents usage well but doesn't describe the C++ verification workflow. Let me add that section:

Now add pointer comments in the test files. First the fixture list in `test_flow_ast.cpp`:

IDE noise as usual. Let me add a pointer in `flow_fixture.hpp` too, then verify the build still passes:

Comments-only changes, but let me confirm nothing broke by rebuilding and running the fixture tests:

Done — docs updated and comment pointers added, build still green (15/15).

## What changed

- **[mermaid-utils/README.md](mermaid-utils/README.md)** — new section **"Generating fixtures for the C++ verification suite"**: the 4-step workflow (create `.mmd` → `node extract.mjs` → register the basename in `kFixtures` → rebuild + `ctest -R FlowAst`), the bulk-regenerate command (`extract.mjs tests/test-files/*.mmd`), and a **"stay within the supported subset"** list of constructs to avoid (click, front-matter, entities, edge-IDs/`@{shape}`) with a pointer to `docs/mermaid/deferred.md`.
- **[test_flow_ast.cpp](tests/mermaid/test_flow_ast.cpp)** — comment above `kFixtures` pointing to that README section for adding fixtures.
- **[flow_fixture.hpp](tests/mermaid/flow_fixture.hpp)** — header comment noting the golden files come from `extract.mjs` and pointing to the README for regeneration.

So the fixture-generation workflow is now discoverable from both the tool's README and the test files that consume its output. (The IDE diagnostics remain the usual unconfigured-IntelliSense noise — the rebuild confirms the code is unchanged and passing.)

## ▸ Prompt — 2026-07-10 15:50

> So we now have a functioning parser for flowchart diagrams in mermaid, which is testable against official mermaid library, expecially we achieved structural correctness, now the last part, maybe the difficult part, the renderer. Please list which are the possibilities that we already explored, what we decided to do and why

Here's the consolidated recap of the render/layout thread — everything we weighed, what we settled on, and the reasoning. This is the design context to carry into building it.

## The problem, restated

`mermaid.js` renders by delegating layout to **dagre** (a Sugiyama-style layered graph-drawing library), then emitting SVG. To "render like mermaid" we need two things — a **layout engine** (positions for nodes/edges) and an **SVG emitter** — and we have to decide how faithful to be.

## 1. Layout engine — options explored

| Option | What it is | Verdict & why |
|---|---|---|
| **Port dagre to C++** | Reimplement mermaid's actual layout lib | Rejected — large, and *still* wouldn't match mermaid exactly (network-simplex tie-breaks, float arithmetic). Overkill. |
| **Link Graphviz `dot`** | Graphviz is also Sugiyama-style | Rejected — real layout for free, but different coordinates from mermaid **and** a heavy C dependency. |
| **OGDF** | Powerful graph-layout library | Rejected — huge, GPL; wrong fit for a course project. |
| **Shell out to real dagre (JS)** | Call the actual algorithm | Rejected — defeats the purpose of writing a parser/renderer. |
| **Write a minimal Sugiyama layout ourselves** | Hand-roll the 4 classic phases | ✅ **Chosen.** |

**Decision & why:** you stated the hard constraint — **no external dependencies** in the project (the same principle that led us to hand-write the LR generator instead of pulling Bison). Given that, and that the graphs are small, a **minimal hand-written Sugiyama layered layout** is the right scope. dagre/Graphviz/OGDF all either add a dependency or promise exact-match fidelity we explicitly decided we don't need (next point).

## 2. "Can we mimic mermaid.js?" — only approximately, on purpose

We concluded you **cannot pixel-match mermaid and shouldn't try** — its geometry is a product of dagre's specific algorithm and floating-point. So we split "mimic" into two layers and committed to one:

- **Geometry (coordinates):** *not* matched. Our layout will produce a recognizable layered drawing, but different exact positions.
- **Structure & style:** *matched.* We emit SVG using **mermaid's own CSS class names and shape templates** — the `class="node default"`, `flowchart-link`, `<g class="nodes">`, `edgePaths`, etc. that we saw when we inspected the real `simple-mermaid.svg`. So it renders as recognizably "mermaid-style," and a human diffing the two in a browser sees the same diagram.

**Decision & why:** exact geometric parity is infeasible and not worth it; matching *style and structure* is achievable and gradeable. "Mimic mermaid's structure and style, not its geometry."

## 3. SVG verification — self-consistency, not a diff against mermaid

This resolves the ambiguity in your original 3-step strategy ("check the SVG output structurally, not pixel by pixel"):

- We **rejected diffing our SVG against mermaid's SVG.** Even a "structural" diff would fail on geometry, and stripping geometry down to topology just re-derives the AST — redundant with the AST gate we already built.
- We **chose a self-consistency check** on *our own* SVG: well-formed XML; every `FlowDb` vertex → exactly one node group; every edge → one path; labels present; (optionally) no node-box overlaps.

**Decision & why:** the **AST comparison is the correctness gate** (now done — 15 fixtures green against mermaid). The SVG check verifies *our* output is internally valid and complete, not that it equals mermaid's pixels. mermaid's `.svg` files (pre-generated alongside each `.ast.json`) stay as a **human visual reference**, not a machine assertion.

## 4. The concrete plan we sketched (proposed, not yet locked in detail)

A **minimal 4-phase Sugiyama** consuming `FlowDb`:

1. **Rank assignment** — longest-path layering; respect `direction` (TB/BT/LR/RL by swapping/flipping axes), plus per-subgraph direction.
2. **Ordering** — median/barycenter heuristic for crossing reduction (a few sweeps suffice at this scale).
3. **Coordinate assignment** — simple centered positioning (not full Brandes–Köpf).
4. **Edge routing** — straight or simple béziers, with **dummy nodes** for rank-spanning edges.

Then an **SVG emitter** using mermaid's CSS classes/shape paths, wired into `MermaidHandler` to replace today's passthrough `<div class="mermaid">` with real inline SVG.

**Still open** (deliberately deferred until we start): the exact coordinate-assignment algorithm, curve style, subgraph *cluster* rendering (bounding boxes), and how much of `linkStyle`/`style`/`classDef` visual styling we apply in the SVG (recall these are parsed but not yet lowered — `docs/mermaid/deferred.md`).

---

**In one line:** no-dependency constraint → hand-written minimal Sugiyama layout; mimic mermaid's **style/structure, not geometry**; verify the SVG by **self-consistency**, keeping the **AST comparison as the real correctness gate**.

Want to start on the layout engine — beginning with phase 1 (ranking) over `FlowDb`, or would you prefer I lay out the module/type design for the whole layout→SVG stage first?

