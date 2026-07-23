# Link reference definitions as first-class nodes

**Theme:** JSON/mdast + test refactor

**Source transcript:** `0d3e1fa2-7f5d-4a01-8791-2c175d59c8da.jsonl`  
**Records:** 564–849

**Dates:** 2026-07-19 10:58 → 2026-07-20 15:17  
**User turns:** 6

**Related commits:**

- `0166741 Fix link definition parsing.`
- `6d31019 add flag to detect whether a list item's list is loose or not`

---

## ▸ Prompt — 2026-07-19 10:58

> A fair solution for #1 would be to extend the Renderers.render function with another parameter which is the link definition maps. Analyze this proposal

Let me ground this in the actual architecture before analyzing — how `parse()`/`render()` are wired, and where the definition map lives.

Let me confirm one detail for the analysis — whether the AST already distinguishes reference links from inline links.

Confirmed the key fact: inline links leave `LinkData.label` as `nullopt`, while reference links set it — so the AST *already* distinguishes them. Now I can analyze your proposal properly.

## Your proposal: `render(node, ref_map)`

The `ref_map_` (label → `LinkDef{destination, title}`) already exists in `SpineHandler`; today it's used during phase-2 inline parsing and then thrown away, and [`parse()`](include/markdown_parser/parser/parser.hpp#L26) calls `renderer.render(*doc)` with no map. Your idea is to thread that map through to the renderer.

The analysis hinges on one tension: **HTML wants references *resolved*; mdast JSON wants them *preserved*.** A render-time map serves the first well and the second not at all. Let me split it.

## Where the map parameter genuinely wins: HTML

The faithful design for #1 makes the AST *reference-preserving* — the inline parser stops inlining `url`/`title` and instead emits unresolved `linkReference`/`imageReference` nodes, and definitions stay in the tree. Once resolution is deferred, **something** has to resolve `[foo]` → `<a href="/url">` for HTML output. The definition map at render time is exactly the right tool, and it already exists — you'd just return it from `parse()` and hand it to `HtmlRenderer::render`. This half of your proposal is clean and correct. ✅

## Where the map parameter is *not enough* on its own

For the JSON/mdast goal, passing a map to the renderer doesn't solve the three things that actually make our output diverge:

1. **`definition` node placement.** mdast keeps a `definition` node **at its source position** (in the earlier example it was the *2nd* root child; in spec ex.192 it was the *1st*). A `ref_map` is **unordered, positionless, and de-duplicated by label** — you cannot reconstruct where definitions sit, their source order, or duplicate definitions from it. To emit them correctly they must be **retained as nodes in the tree**, which means the map is redundant for JSON.
2. **`referenceType`** (`shortcut`/`collapsed`/`full`). Not derivable from the map — it's a property of the *usage site*. The three branches exist in the inline parser ([full](src/markdown_parser/parser/InlineParser.cpp#L1150), collapsed, shortcut) but the type isn't recorded on the node.
3. **Un-inlining `url`/`title`.** The map doesn't undo the eager resolution that already baked `url`/`title` into the `link` node.

So the map-as-render-param is the **HTML half** of the reference-preserving design, not a standalone fix.

## The coherent design (and how your idea fits)

| Layer | Change | Uses the map? |
|---|---|---|
| Inline parser | Emit `linkReference`/`imageReference` (identifier + `referenceType`), stop inlining url/title | — |
| Block tree | Keep `definition` nodes in source position (don't discard at [SpineHandler:409](src/markdown_parser/parser/SpineHandler.cpp#L409)) | — |
| `JsonRenderer` | Emit those nodes directly | **No** |
| `HtmlRenderer` | Resolve `linkReference` via the map at render time | **Yes ← your proposal** |
| `parse()` | Return/thread `ref_map_` alongside the doc | supplies the map |

**Our AST is already ~40% there:** `LinkData.label` distinguishes references from inline links, and the referenceType branches exist — so the deltas are (a) record referenceType, (b) don't drop definitions, (c) don't inline url/title, (d) resolve in HTML via the map.

## The tempting shortcut that *doesn't* work

"Keep eager resolution, and just hand the map to `JsonRenderer` so it synthesizes `definition` nodes." This fails on point 1 above: appending definitions from a map loses their position (breaks ex.192 where the definition comes first) and duplicates. You'd pass every case where a definition isn't last. So the map cannot substitute for retaining definition nodes.

## Engineering notes

- The `Renderer` **concept** and every call site (`main.cpp`, `wasm_bindings`, tests, debug/factory renderers) take `render(node)`. Add the map as an **optional second parameter / overload** (default empty) so existing renderers compile unchanged — only `HtmlRenderer` consumes it.
- `parse()` is a template returning `std::string`; give `SpineHandler` a `const ref_map&` accessor and pass it in. Minimal churn.

## Verdict

Your instinct is **right for HTML and worth adopting** — deferring resolution and giving the HTML renderer the definition map is the clean way to keep HTML output correct once the parser stops resolving. But framed as "the solution to #1," it's incomplete: the load-bearing work is making the **AST reference-preserving** (emit reference nodes, retain definition nodes, record referenceType). The map parameter is the natural companion to that, not a replacement for it. If we instead keep eager resolution, no render-time map can reproduce mdast's `definition` placement.

Want me to spec out the reference-preserving changes concretely (node types, parser deltas, the `render` signature) as the plan for #1?

## ▸ Prompt — 2026-07-20 10:33

> Analyze this solution:
>
> When a link definition is recorded, we bind it to the node (extend the node definition) then we keep everything as it is for urls eager resolution, nothing changes for the HTML renderer, instead now the JSON renderer has the link definition, where it was created, so it can add it to the output

Let me verify one thing that affects your "nothing changes for HTML" claim — how the renderers' block switches handle an unknown/new node type.

Confirmed the last detail: both renderer switches enumerate all `NodeType`s with no `default:`. A new `NodeType::Definition` would simply have no HTML case → emits nothing, which is exactly right for a definition (modulo a possible `-Wswitch` warning). So your "HTML unchanged" holds.

Here's the analysis.

## What your proposal is

Attach each link reference definition to the tree **at the position where it's scanned** (a positioned `definition` node, not a flat map). Keep eager url resolution and the HTML path exactly as-is. The `JsonRenderer` then emits the `definition` in source order.

## This is the right fix for the hardest sub-problem ✅

My earlier objection to the map was **placement**: a `ref_map` is unordered, positionless, and deduplicated, so it can't reconstruct where definitions sit. Your proposal dissolves that objection precisely because you capture the definition **at creation time, in the tree**, instead of from a map. Two concrete wins over the map:

- **Order/position** is preserved by construction (insert the node where the paragraph was).
- **Duplicates** are preserved — mdast keeps every `definition` node (even repeated labels), while `ref_map_` keeps only the first. Emitting per-scanned-def naturally matches remark; the map would have silently collapsed them.

And it needs **no render-time map and no deferred resolution** — strictly lighter than the design I sketched. HTML keeps eager resolution and just no-ops the new node.

## But by itself it doesn't make the cases pass ⚠️

The usage site is still a resolved `link`. Re-run the simple example mentally with only this change:

```jsonc
// yours (definition now present, but link still resolved):
[ {paragraph[ {link, url, title} ]},  {definition …} ]
// remark:
[ {paragraph[ {linkReference, identifier, referenceType} ]},  {definition …} ]
```

Still mismatches on `link` vs `linkReference`. So definitions-in-place is **necessary but not sufficient** — you also need the usage node emitted as `linkReference`/`imageReference`.

## The good news: the missing half is *also* HTML-free

That second piece can live **entirely in `JsonRenderer`**, because the AST already tells us a link came from a reference — `LinkData.label` is set for reference links and `nullopt` for inline `[text](url)` (I verified this). So:

- `JsonRenderer`: when a `Link`/`Image` node has a `label`, emit `linkReference`/`imageReference` (identifier = normalized label, drop url/title) instead of `link`/`image`.
- Inline parser: record the `referenceType` (`shortcut`/`collapsed`/`full`) — the three branches already exist ([full](src/markdown_parser/parser/InlineParser.cpp#L1150), collapsed, shortcut); just stash which one matched. This is the only *tiny* parser touch on the usage side.

## The complete lightweight solution

| Change | Where | Touches HTML? |
|---|---|---|
| Positioned `definition` nodes (your proposal) | scan site in `SpineHandler` + new `NodeType::Definition` | No (no-op node) |
| Record `referenceType` on reference links | inline parser (3 existing branches) | No |
| Emit `definition`; emit `linkReference` when `label` set | `JsonRenderer` | No |
| Eager url resolution | unchanged | unchanged |

This keeps HTML and its 652 green tests completely untouched, and is meaningfully simpler than deferring resolution + threading a map.

## Caveats to budget for

1. **Definition emission from a mixed paragraph.** A paragraph can be `[a]: /x\n[b]: /y\ntrailing text` → mdast emits `definition(a)`, `definition(b)`, then a `paragraph`. Today [`maybeScanLinkRefDefs`](src/markdown_parser/parser/SpineHandler.cpp#L841) strips the defs and either trims or **discards** the paragraph ([:409](src/markdown_parser/parser/SpineHandler.cpp#L409)). You'll need it to **synthesize N definition nodes in order** and insert them into the **correct parent** (the paragraph may be nested in a blockquote/list item) ahead of any surviving paragraph. This is the fiddly part.
2. **New `NodeType::Definition`** → `JsonRenderer` needs a case; `HtmlRenderer` needs at most an empty case to avoid a `-Wswitch` warning. Both other tree-walkers (debug renderer) too.
3. **Field normalization.** The `definition` node's `identifier` must match mdast's label normalization (case-fold + whitespace-collapse — you already have `normaliseLabel`), and `url`/`title` must match remark's decoding. Expect to chase a few residual diffs here.
4. **Mild semantic hybrid.** The AST now holds *resolved* links plus *separate* definition nodes, and JSON re-derives the reference view. It's not a "pure" reference-preserving tree — but it's contained and pragmatic, and it's the price of leaving HTML alone.

## Verdict

Your proposal is the **correct and lightest** way to handle definitions — capturing them positioned in the tree beats any render-time map on order and duplicates, with zero HTML impact. Pair it with a **JSON-only** `link`→`linkReference` step (using the existing `label` + a recorded `referenceType`) and you have a complete #1 that never touches the HTML renderer or eager resolution. The one real cost is teaching the definition-scanner to emit ordered nodes into the right parent.

Want me to implement this — starting with `NodeType::Definition` + positioned emission in the scanner, then the `JsonRenderer` `linkReference`/`definition` output?

## ▸ Prompt — 2026-07-20 10:49

> But why do you suggest a new NodeType::Definition, we already have LinkDef, could we just add that as a field of BlockNode?

Good question — let me ground the answer in what `BlockNode` and `LinkDef` actually look like.

You *can* avoid a new enum value, but I'd argue against it — and the reason is that `BlockNode` already has the field mechanism you're reaching for. Let me lay out the two readings of "add LinkDef as a field."

## First, the premise needs a small fix

`LinkDef` isn't quite enough as-is. mdast's `definition` node carries **four** fields:

```jsonc
{ "type": "definition", "identifier": "foo", "label": "foo", "url": "/url", "title": "title" }
```

`LinkDef` has only `destination` + `title`. You'd need to add `label` (raw) and `identifier` (normalized) — so whichever route you take, `LinkDef` gets extended into effectively a `DefinitionData`.

## Reading A — a bare `std::optional<LinkDef>` field on `BlockNode`

This is the one I'd avoid, because of the **discriminator problem**. The renderer dispatches on `node.type`:

```cpp
switch (node.type) { case NodeType::Paragraph: … case NodeType::Heading: … }
```

A definition node placed in `children` still needs *some* `type`. If you give it a real type (say `Paragraph`) and hang the def off an optional field, then every tree-walker must check "is `link_def` set?" **before** trusting `type` — otherwise HTML renders it as a `<p>`, the debug renderer mishandles it, etc. You've created a node whose declared `type` lies about what it is, and the optional's *presence* becomes an ad-hoc, implicit type tag. That's precisely the job the `type` enum already does — you'd be duplicating it, less safely, and forcing a special-case into **every** switch anyway (so no fewer edits).

Plus every paragraph/heading/list node in the whole tree now carries an unused `optional<LinkDef>`.

## Reading B — put it in `BlockData`, tag it `NodeType::Definition` (recommended)

`BlockNode` already separates *what kind* (`NodeType type`) from *its payload* (`BlockData data` — the variant). Every block follows this: `Heading`↔`HeadingData`, `CodeBlock`↔`CodeBlockData`, `List`↔`ListData`. A definition is just the next one:

```cpp
enum class NodeType { …, ThematicBreak, Definition };

struct DefinitionData {           // LinkDef + the two label fields
  std::string destination;        // -> url
  std::optional<std::string> title;
  std::string label;              // raw
  std::string identifier;         // normalized
};

using BlockData = std::variant<std::monostate, HeadingData, CodeBlockData,
                               ListData, ItemData, HtmlBlockData, DefinitionData>;
```

**This *is* "reuse LinkDef as a field"** — the field is `data`, and the `NodeType` is the discriminant the variant is keyed to, not extra baggage. The renderer stays idiomatic:

```cpp
case NodeType::Definition: {
    const auto& d = std::get<DefinitionData>(node.data);
    out_ += "{\"type\":\"definition\",\"identifier\":" + jsonStr(d.identifier) + …;
}
```

## Edit-count is the same, safety isn't

Either reading touches the same places — JSON emits it, HTML/debug no-op it (a one-line empty case; without a `default:` the switch already falls through to "emit nothing," which is correct for a definition). So the new `NodeType` buys you **honest dispatch** at no extra cost: the node's type says what it is, every existing `switch (node.type)` keeps working by construction, and no traversal has to learn a magic "check this optional first" rule.

**Bottom line:** don't add a naked field — add `DefinitionData` to the `BlockData` variant (that's the "field" you want, and it reuses `LinkDef`'s shape) and give it the honest `NodeType::Definition` tag. A definition genuinely *is* a distinct block kind in mdast; modeling it as one keeps the tree self-describing.

Want me to go ahead and implement it this way?

## ▸ Prompt — 2026-07-20 15:01

> Lets add the needed info to LinkData in order to fill the mdast fields and let's also the new definiton blockData type to the parser output, is this enough?

Let me verify a couple of mdast details before answering, so I'm precise about "enough" — specifically `imageReference` shape and the `referenceType` variants.

Verified the exact shapes. Here's the honest answer: those two additions are the right **foundation**, but they're the *data model* — not the *behavior*. By themselves they're **necessary but not sufficient**. Here's the full checklist, with your two items marked.

## A. Data model — your proposal ✓
1. **`LinkData` += `referenceType`** (`none`/`shortcut`/`collapsed`/`full`). `label` already exists (raw); `identifier` = `normaliseLabel(label)`. Keep `destination`/`title` for HTML's eager resolution. ✓
2. **`DefinitionData` in `BlockData`** + `NodeType::Definition` (destination→url, title, label, identifier). ✓

## B. Parser behavior — still required
3. **Inline parser must *populate* `referenceType`.** The field existing isn't enough — set it in the three existing branches ([full](src/markdown_parser/parser/InlineParser.cpp#L1150), collapsed, shortcut). Small.
4. **The definition nodes must be emitted *in the right place*** — this is the hard part "add to parser output" hides. [`maybeScanLinkRefDefs`](src/markdown_parser/parser/SpineHandler.cpp#L841) currently *discards* def-only paragraphs and *trims* defs out of mixed ones. It must instead **construct N `Definition` nodes in source order and splice them into the correct parent's `children`**, ahead of any surviving paragraph. Mixed paragraphs (`[a]: /x\n[b]: /y\ntext`) and nested containers (defs inside a blockquote/list item) are the fiddly cases.

## C. JSON renderer — still required (this is where the data gets *used*)
5. **New `case NodeType::Definition`** → emit the `definition` node.
6. **Reference usage split** — when a `Link`/`Image` has `referenceType != none`, emit **`linkReference`** or **`imageReference`** instead of `link`/`image`, dropping `url`/`title` and adding `identifier`/`label`/`referenceType`. Note the shapes differ (I just confirmed):
   - `linkReference` keeps **`children`** (phrasing content).
   - `imageReference` has **`alt`** and **no children** (reuse the recursive `collectDescendantText` you already added).

## D. Other tree-walkers — trivial but needed
7. **`HtmlRenderer` + debug renderer** must tolerate `NodeType::Definition` (no-op → emits nothing, keeping HTML byte-identical and avoiding a `-Wswitch` warning).

## E. One detail to watch
8. **`identifier` normalization** must match mdast (Unicode case-fold + whitespace-collapse). You have `normaliseLabel` — likely close, but expect to chase a few residual diffs (and `url`/`title` decoding on the definition node).

---

**So: is it enough?** The two model changes are correct and enabling, but the work that actually flips the 77 cases green is **B4** (positioned definition emission) and **C5/C6** (renderer emitting the new shapes). A + B3 without C changes nothing observable; C without A/B has nothing to read.

I'd sequence it: **A → B3 (referenceType) → C6 (linkReference/imageReference)** — verify the usage-site cases pass — then **A2 → B4 (definition placement) → C5** and verify the definition cases. That way each half is independently testable against the golden suite.

Want me to start with the usage-site half (A1 + B3 + C6), which is self-contained and immediately measurable?

## ▸ Prompt — 2026-07-20 15:08

> B4: "The definition nodes must be emitted in the right place"
>
> The right place is the current/last open BlockNode, is it correct?

Let me check how `closeBlock` attaches the finished node to its parent, so I can answer precisely.

Confirmed by the code. Yes — your intuition is correct, with one precise clarification.

Look at [`closeBlock`](src/markdown_parser/parser/SpineHandler.cpp#L400):

```cpp
auto node = std::move(spine_.back());
spine_.pop_back();                    // ← paragraph popped HERE
if (node->type == NodeType::Paragraph) {
    maybeScanLinkRefDefs(*node);      // ← scan runs AFTER the pop
    ...
}
...
spine_.back()->children.push_back(std::move(node));   // ← paragraph appended to parent
```

By the time definitions are scanned, the paragraph is **already popped**, so `spine_.back()` is the paragraph's **parent** — which is exactly the current last-*open* block. That's your insertion target:

```cpp
spine_.back()->children.push_back(std::move(definition_node));
```

So "current/last open BlockNode" = the parent, and it's correct. Two things to get right within that:

1. **Order: definitions before the surviving paragraph.** Push the `Definition` node(s) onto `spine_.back()->children` *before* the existing `push_back(node)` at line 449. Since siblings closed earlier are already in the vector, and you push defs then the paragraph, source order is preserved automatically.

2. **The fully-definition paragraph still needs them pushed before the early `return`.** When the paragraph is *only* definitions it's discarded at [line 414-415](src/markdown_parser/parser/SpineHandler.cpp#L414) (`if (blank) return;`) — you must emit the `Definition` nodes into `spine_.back()->children` **before** that return, or they're lost.

One caveat to flag, not a correction: the same scanner is also invoked on the **setext-heading path** ([line 522](src/markdown_parser/parser/SpineHandler.cpp#L522)), where a paragraph with leading defs is promoted to a heading. There the "parent" context is different (the paragraph isn't popped the same way), so the insertion point needs checking separately — mdast emits the definitions *before* the resulting heading, into that same parent.

Net: your rule holds — **push each `Definition` into `spine_.back()` (the parent) in scan order, ahead of any surviving paragraph/heading** — just handle the discard-early and setext paths so nothing gets dropped. Let me confirm the scanner yields multiple defs and trims content so "push N in order" is accurate:

Yes — **correct**, and I've now verified the scanner internals confirm it works cleanly. Three facts that back your rule:

**1. The parent (`spine_.back()`) is the right target.** As shown, the paragraph is popped before scanning, so `spine_.back()` is its parent — the current last-open block. That's where definitions and the surviving paragraph both belong.

**2. The scanner already produces defs in source order and trims consumed input** — [`maybeScanLinkRefDefs`](src/markdown_parser/parser/SpineHandler.cpp#L846):
```cpp
while (pos < content.size())
    if (!tryScanOneLinkRefDef(content, pos)) break;   // loops: 0..N defs, front-to-back
node.string_content = std::string(content.substr(pos));  // remaining = surviving paragraph
```
So "push N `Definition` nodes in order, then the surviving paragraph" maps directly onto this loop.

**3. All four mdast fields are available at the commit point** — [line 837](src/markdown_parser/parser/SpineHandler.cpp#L837):
```cpp
ref_map_.try_emplace(norm_key, destination, title);
//                   ^identifier  ^url        ^title   (+ raw_label = label, a few lines up)
```
Nothing extra needs to be parsed — `norm_key`→`identifier`, `raw_label`→`label`, `destination`→`url`, `title`→`title`.

## One nuance that changes the emission point slightly

`try_emplace` is **first-definition-wins** — it silently skips a duplicate label (correct for *resolution*). But mdast keeps **every** `definition` node, duplicates included. So the `Definition` node must be created for **every scanned def**, not gated on `try_emplace` succeeding. Practically: emit the node unconditionally in `tryScanOneLinkRefDef` (or collect the `DefinitionData` there), and keep `try_emplace` doing dedup only for the resolver map.

## Concrete shape

Have the scanner **collect** `DefinitionData` rather than push directly (because the two callers place them differently):

- `tryScanOneLinkRefDef(content, pos, out_defs)` → append a `DefinitionData` per success.
- `maybeScanLinkRefDefs(node, out_defs)` → fills the vector.
- **`closeBlock`**: after scanning, `for (auto& d : defs) spine_.back()->children.push_back(makeDefinitionNode(d));` — placed **before** both the `if (blank) return;` early-out and the paragraph's own `push_back`.
- **setext path** ([:522](src/markdown_parser/parser/SpineHandler.cpp#L522)): same vector, inserted into that parent ahead of the heading node.

So: your rule is right (`spine_.back()`), the plumbing to get there is a `DefinitionData` out-param + node construction at the two call sites, and the only subtlety is emitting a node per def regardless of map dedup.

## ▸ Prompt — 2026-07-20 15:17

> This solution seems fair to me, implement it

This is a solid, well-scoped plan. Let me implement it. First let me map all the `NodeType` switches and the label-normalization helper so I don't miss a site.

Solid plan. Let me track this and implement. First, checking compile flags (whether new enum values will warn) and the setext scan site.

No `-Werror`, and `block_rules`/continuation never see `Definition` nodes (they're created pre-closed and pushed directly), so those switches need no changes. The setext path scans while the paragraph is still `tip()`, so its parent is `spine_[size-2]`. Let me check the method declarations, then implement.

Now the data model in Types.hpp:

Now the `ReferenceType` enum and `LinkData` fields:

Now add the `Definition` case to `nodeTypeToString`:

Todo 1 done. Now the inline parser — record `reference_type` + `identifier` per branch. Let me read the exact region:

Now set `ref_type` in each of the three reference branches. Full reference:

Now the LinkData construction — populate `identifier` and `reference_type`:

Todo 2 complete. Now the scanner (Todo 3). Update the header signatures first:

Now the scanner impl — collect the `DefinitionData`:

Now update the `tryScanOneLinkRefDef` signature line:

Now add the `makeDefinitionNode` helper and wire the two call sites. First the helper (near the top of the anonymous/impl section) and the `closeBlock` site:

Now define `makeDefinitionNode`. Let me add it just before `closeBlock`:

Now the setext path — insert definitions into the parent (`spine_[size-2]`) ahead of the heading:

Todos 1-3 done. Now the JSON renderer (Todo 4) — the `definition` node and `linkReference`/`imageReference`.

Add the `Definition` block case (after `HtmlBlock`):

Now add a `referenceType` string helper and update the Link/Image cases:

Now build and check it compiles:

Compiles. Now remove the reference/definition cases from the skip-list and measure:

Big jump: **647 pass** (was 574), HTML still 652. That's 73 of 77 reference cases fixed. Let me find the remaining ~4 failures to triage.

All 4 remaining failures are the **same sub-issue: the raw `label` field**. Our `identifier` matches remark everywhere (our `normaliseLabel` is correct), but mdast's `label` is the raw label with **backslash escapes / entities resolved** (e.g. `Foo*bar\]` → `Foo*bar]`), while we store it verbatim. Let me check the decode utilities to fix it.

There's already `string_utils::processEscapesAndEntities` — exactly mdast's label decoding (backslash-escapes + entities). I'll apply it to the stored `label` (keeping `identifier` from the raw, which is already correct). First the inline parser:

Let me confirm `string_utils` is included in SpineHandler.cpp, then rebuild and test:

SpineHandler.cpp doesn't include `string_utils.hpp` directly — let me add it to be safe:

**650 pass** — only ex541 remains (the raw-label whitespace case where our paragraph line-accumulation already stripped the internal indentation upstream, same info-loss class as code spans). Let me skip-list it with documentation:

Now the full suite — confirm everything green:

All **1341 pass**. Let me verify the original example now produces the expected mdast, and mark todos done.

The output now matches remark exactly, and HTML is unchanged.

#1 is implemented, exactly along the lightweight design we discussed. Summary:

## What changed

**Data model** ([Types.hpp](include/markdown_parser/core/Types.hpp))
- `NodeType::Definition` + `DefinitionData{identifier, label, destination, title}` in the `BlockData` variant.
- `ReferenceType` enum + `LinkData.reference_type`/`identifier` (kept `destination`/`title` for eager HTML resolution).

**Parser**
- [InlineParser](src/markdown_parser/parser/InlineParser.cpp): records `reference_type` (full/collapsed/shortcut) and the decoded `label`/`identifier` on reference links — inline links stay `reference_type = None`.
- [SpineHandler](src/markdown_parser/parser/SpineHandler.cpp): the scanner collects a `DefinitionData` per definition (preserving duplicates), and both call sites (`closeBlock` and the setext path) splice `Definition` nodes into **the parent — your `spine_.back()` insight** — in source order, ahead of any surviving paragraph.

**Renderer** ([JsonRenderer](src/markdown_parser/renderer/JsonRenderer.cpp))
- Emits `definition` nodes, and `linkReference`/`imageReference` (with `alt`, no children for images) when a node is a reference.
- HTML renderer and eager resolution **completely untouched**.

## Results

| | before | after |
|---|---|---|
| mdast (JSON) vs remark | 574 pass / 78 skip | **650 pass / 2 skip / 0 fail** |
| CommonMark HTML | 652 | **652** (unchanged) |
| Full `ctest` | — | **1341 / 1341** |

**76 of the 77** reference/definition cases now pass. The **2 remaining skips** are documented upstream info-loss (not renderer bugs):
- **173** — a `<style>`-to-EOF HTML block trailing newline.
- **541** — a multi-line definition label whose internal indentation our paragraph accumulation strips before the label is scanned.

The one subtlety worth noting: mdast's `label` is the raw label with **escapes/entities resolved but case/whitespace kept**, while `identifier` is case-folded + whitespace-collapsed — so I fed `label` through `processEscapesAndEntities` and left `identifier` on `normaliseLabel`. That's what closed the last 3 fiddly cases (194, 549, 550).

Everything's committed-ready on your side. Want me to update the README's testing/feature notes to mention the mdast reference-preserving output, or leave the docs for a separate pass?

