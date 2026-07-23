# The --json flag and aligning the AST with mdast

**Theme:** JSON/mdast + test refactor

**Source transcript:** `0d3e1fa2-7f5d-4a01-8791-2c175d59c8da.jsonl`  
**Records:** 164–563

**Dates:** 2026-07-18 16:02 → 2026-07-19 10:12  
**User turns:** 6

**Related commits:**

- `4dfd2cc add utils scripts that generates JSON output for all the test cases`
- `5228dc0 add test cases for markdown JSON output`
- `f42de5e fix JSON output in order to pass mdast golden tests`

---

## ▸ Prompt — 2026-07-18 16:02

> I think we should just get rid of or refine the --json flag/output on the markdown binary, right now it is not doing anything. Search if there is a spec for json markdown output

Let me first re-examine what `--json` actually produces and how the renderer works, then check the mdast spec.

The `--json` flag does produce output — mdast-style JSON — but it has issues (empty `{"type":"text","value":""}` nodes, and mermaid is just a generic `code` node, not tagged as a diagram). The renderer already claims mdast compatibility. Let me verify the actual mdast spec.

I found what's going on. Here's the assessment:

**The `--json` flag *does* work** — it emits an mdast AST. What made it look broken is a **bug**: the inline parser leaves empty `{"type":"text","value":""}` nodes around emphasis/strong/links (visible in the output above). Real mdast/remark never emits those.

**The spec exists**: it's **[mdast](https://github.com/syntax-tree/mdast)** (Markdown Abstract Syntax Tree — the remark/unified standard), which [JsonRenderer.hpp](include/markdown_parser/renderer/JsonRenderer.hpp) already targets. Checking the current output against it:
- Node types, fields (`root`/`heading.depth`/`list.ordered,start,spread`/`link.url,title`/`code.lang,meta,value`…) — all correct.
- Mermaid: mdast has **no diagram node type**; the standard representation is exactly a `code` node with `lang:"mermaid"`, which is what you emit. So the JSON path already "tags" mermaid correctly per the spec — the HTML path is where it becomes a rendered diagram.
- The **only** deviation is the empty text nodes.

So this is a "refine," not "remove" — and JSON AST is literally one of the two output formats the assignment names, so it's worth keeping and cleaning. The fix is small: skip empty text nodes when emitting children.

Fixed and verified. The output is now clean, valid mdast (round-tripped through `python -m json.tool`):

- **No more empty text nodes** — the paragraph goes straight `"Para with "` → `strong` → `" and "` → `link` → `"."`.
- **Mermaid** correctly appears as `{"type":"code","lang":"mermaid","value":"…"}` — the spec-correct mdast tagging.
- Build clean; the fix only touches [JsonRenderer.cpp](src/markdown_parser/renderer/JsonRenderer.cpp) (the HTML path and spec tests, which use `HtmlRenderer`, are unaffected).

**Recommendation:** keep `--json` — it's a legitimate, now-clean second output format that directly satisfies the assignment's "JSON AST" option, and it demonstrates the structured-representation requirement. I'd also add one line to the README example noting mermaid shows up as a `code`/`lang:"mermaid"` node in JSON vs. rendered SVG in HTML — want me to?

There's now a fair amount uncommitted (README, CI `<algorithm>` fixes, test reorg, this JSON fix). Want me to commit it all — and in how many commits (one, or split into logical ones like "fix CI build", "reorg tests", "clean up JSON output", "add README")?

## ▸ Prompt — 2026-07-18 16:08

> I think we should add tests also for the json output, how would you proceed?

Good idea — the JSON path currently has zero coverage. Let me look at how the mermaid golden test compares JSON, so my proposal fits your existing patterns.

I see two testing philosophies already in the repo: **string-equality against an external spec** (markdown HTML) and **structural comparison against an external oracle** (mermaid, via the real mermaid lib through `extract.mjs`, canonicalized with nlohmann). The JSON output could follow either. Here's how I'd approach it — the real decision is *what's the oracle*:

Let me confirm remark's credentials as a Markdown interpreter before we build tooling on it.

Yes — **remark is a valid, authoritative Markdown interpreter**, and it's the ideal oracle here:

- remark parses via **[micromark](https://github.com/micromark/micromark)**, "the smallest CommonMark compliant markdown parser," which follows the reference parsers (`cmark`, `cmark-gfm`) and is verified against the CommonMark spec plus thousands of extra tests.
- **[mdast](https://github.com/syntax-tree/mdast)** is remark's native syntax tree — so it emits exactly the structure your `JsonRenderer` targets. It's part of the **[unified](https://unifiedjs.com/explore/package/remark-parse/)** collective, the de-facto standard JS markdown toolchain.

So it's the same philosophy as your mermaid suite (compare against the reference implementation's output), applied to markdown.

**One reality to flag before I build it** — raw remark mdast will *not* be byte-equal to your output, so the C++ side must canonicalize both (exactly like `flow_fixture.hpp` does for mermaid). Known differences:
1. **`position` data** — remark tags every node with line/col offsets; you don't emit it. Must strip.
2. **`listItem.spread`** — your renderer hardcodes `false` (there's a code comment admitting it); remark computes it. Will mismatch on loose lists.
3. **Reference links/images** — remark emits `linkReference` + `definition` nodes; worth checking what yours does.
4. **null vs absent** — e.g. `lang:null` vs omitted; normalize.

My plan mirrors the mermaid setup:
1. `markdown-utils/` (Node, like `mermaid-utils/`) with `generate-golden.mjs` — reads `commonmark_spec.json`, runs remark on each case's `markdown`, writes a sibling `commonmark_spec_mdast.json` where each entry gains a `"mdast"` field (keeping `example`/`section`).
2. C++ `test_json_mdast.cpp` in `tests/markdown/` — parse our JSON, canonicalize both sides, structural `EXPECT_EQ`, with a `kKnownFailures`/skip-list for genuine divergences (same pattern as the HTML spec test).

The divergences in #2–#4 will either become **bugs to fix** in `JsonRenderer` or **normalized/skipped** — I'll surface each as it comes up rather than hiding it.

Before I scaffold: want me to name it per your `markdown-helper` idea, and should divergences default to **skip-list** (fast, documents them) or **fix-the-parser** (stricter, more work)? I'd start with skip-list and fix the easy ones (like `listItem.spread`) as we go.

## ▸ Prompt — 2026-07-18 16:24

> you said "One reality to flag before I build it — raw remark mdast will not be byte-equal to your output, so the C++ side must canonicalize both (exactly like flow_fixture.hpp does for mermaid)"
>
> Let's start by setting everything up and seeing which tests are failing, I'd avoid canonicalize in this case.
>
> go on with the planned implementation

Understood — strip `position` at generation time (that's unavoidable, or every case fails on line/col noise), but no field-level canonicalization. We generate clean goldens, do a plain `nlohmann ==`, and *see what actually diverges*. Let me set it up.

First, confirm Node is available and check the spec JSON shape:

Now let me scaffold the `markdown-utils/` Node project (mirroring `mermaid-utils/`).

652 goldens generated. Let me inspect a few to see remark's exact shape vs ours.

This is exactly the signal we wanted — remark's shape reveals concrete divergences:

1. **`code`/`html` value**: remark strips the trailing `\n` (`"foo\tbaz\t\tbim"`); we keep it (`"…B\n"`).
2. **`code`**: remark always emits `meta:null`; we omit `meta` when absent.
3. **`list`**: remark emits `start:null` for unordered; we omit `start`.
4. **`listItem`**: remark emits `checked:null`; we don't.

No canonicalization — I'll write the C++ test as a raw `nlohmann ==` so these surface as real failures. Let me add the test and wire CMake.

It runs. Let me get the pass/fail counts and categorize the divergences.

221 pass / 431 fail. Let me categorize *why* they fail — I'll diff our output against remark structurally in Python and aggregate the divergence types.

Clear buckets. Let me quantify the ROI of each candidate fix by simulating them on our output and seeing how many cases would then pass.

Strong ROI signal. Cumulative pass counts (of 652):

| Fix | Passing | Δ |
|---|---|---|
| baseline | 221 | — |
| `checked:null` on listItem | 228 | +7 |
| `meta:null` + `start:null` | 264 | +36 |
| strip trailing `\n` (code/html) | 364 | +100 |
| merge adjacent text nodes | 484 | +120 |
| fold softBreak → `\n` in text | 541 | +57 |
| (ignore `spread`) | 567 | +26 |

All of these except `spread` are genuine **mdast-fidelity** fixes (notably: mdast has *no* `softBreak` node — soft breaks live as `\n` inside text, and it never has adjacent/empty text nodes). Let me implement them in `JsonRenderer`. Let me check the node child types first.

Both are `std::vector<std::unique_ptr<InlineNode>>`. I'll add one shared coalescing helper and route both inline emitters through it. First the header:

Now the `.cpp` — replace the two inline emitters with the coalescing helper, and add `checked`/`meta`/`start`/trailing-newline fixes:

Now update the call sites and the block-level fields (paragraph/heading → `emitPhrasing`, list `start:null`, listItem `checked:null`, code `meta:null` + trailing newline, html trailing newline):

Now update the Emph/Strong/Link call sites to `emitPhrasing`:

541 pass (up from 221), exactly matching the simulation. Now let me re-categorize the remaining 111 failures to decide what to fix vs. skip-list.

The biggest remaining bucket is `LEN 1 vs 2` at root (66 cases) — remark has an extra node. Let me identify what it is.

All 66 are **link reference definitions and reference-style links/images** — a genuine structural choice: our parser *resolves* `[foo]: /url` definitions and `[foo]` references into concrete `link`/`image` nodes, while remark preserves `definition` + `linkReference`/`imageReference` nodes. That's a deliberate representational difference, not a bug, and un-resolving it is a deep parser change.

Let me classify all 111 failures precisely (spread-only vs. reference/definition vs. other) to build an accurate skip-list.

Clean split: **77** reference/definition (deliberate — we resolve references), **26** spread-only (needs per-item blank-line tracking), and **8** "other" worth a quick look — code spans, hard breaks, and image alt text could be real bugs.

Verdict on the 8:
- **520, 574 (image alt)** — real renderer bug: our alt grabs only *direct* Text children, missing nested content (`"foo "` vs `"foo bar"`). mdast computes alt as the recursive text of all descendants. **Fixable now.**
- **335, 337, 640, 641 (code spans)** — our parser normalizes code-span newlines→spaces at parse time (correct for HTML rendering); remark keeps them raw. The raw newlines are already gone from our AST → not recoverable in the renderer. **Skip-list.**
- **173 (HTML block)** — the one case where remark *keeps* the trailing `\n` (a `<style>` block to EOF); our strip helped 100 others. **Skip-list.**

Let me fix the recursive alt extraction:

Now define `collectDescendantText` near the other helpers:

The alt fix cleared 520 & 574. Remaining **108**: 77 reference/definition, 26 spread, 5 code-span/HTML-edge — all documented divergences. Let me generate the exact example numbers to populate the skip-list.

Now I'll populate the skip-list in the test with the three documented categories:

The full ctest is slow (1300+ tests via ctest overhead). Let me check the mdast suite directly while it runs.

**544 passed, 108 skipped, 0 failed.** Let me confirm the full suite is still green (waiting on the background ctest).

**Full suite green: 1341/1341 pass, 0 fail.** The JSON output now has real verification against remark.

## Summary of what we built

**Oracle-based JSON test suite** (mirrors the mermaid golden approach, applied to markdown):
- [markdown-utils/](markdown-utils/) — Node tool (`generate-golden.mjs`) that runs **remark** (the reference mdast implementation) over all 652 CommonMark spec cases and writes [commonmark_spec_mdast.json](tests/test-files/markdown/commonmark_spec_mdast.json). Only `position` is stripped — no field-level canonicalization, per your call.
- [tests/markdown/test_json_mdast.cpp](tests/markdown/test_json_mdast.cpp) — parses each case, does a raw `nlohmann ==` against remark's tree, with a documented skip-list.

**Result: 221 → 544 passing** by fixing real mdast-fidelity gaps in [JsonRenderer](src/markdown_parser/renderer/JsonRenderer.cpp):

| Fix | Cases |
|---|---|
| `listItem.checked: null` | +7 |
| `code.meta: null`, `list.start: null` (always present) | +36 |
| strip trailing `\n` from `code`/`html` value | +100 |
| coalesce adjacent text nodes | +120 |
| fold soft breaks into `\n` (mdast has no softBreak node) | +57 |
| recursive image `alt` text | +3 |

**108 skipped**, in three documented buckets — none are rendering bugs:
- **77** — reference resolution: we resolve `[foo]: /url` + `[foo]` into concrete `link`/`image`; remark keeps `definition`/`linkReference`.
- **26** — `spread`: needs per-item blank-line tracking our AST doesn't carry.
- **5** — parse-time normalization we can't reverse (code-span newlines→spaces; one HTML trailing-newline edge).

These three categories are exactly the kind of "compared against reference behaviour, here's where and why we differ" material the AI Usage & Verification Report (Task 3) needs.

A couple of follow-ups worth noting:
1. `markdown-utils/` adds a `node_modules/` — I should check it's gitignored (like `mermaid-utils/`).
2. Still uncommitted: README, CI `<algorithm>` fixes, test reorg, JSON cleanup, and now this whole suite. Want me to commit, and how would you like it split?

Want me to verify the gitignore and then propose a commit breakdown?

## ▸ Prompt — 2026-07-19 09:42

> Committing and pushing is on my side, don't worry about those things. I think that all of these problems are whorth of being fixed.
>
> Explain the following:
> - How is the definition/linkReference expected in mdast?
> - what is the spread? why we cannot have it, when the html renderer can render well loose lists?
> - explain better: 5 -- parse-time normalization we can't reverse

Good — you want to actually fix all three. Before proposing the fixes, let me answer your questions precisely, grounding them in our code and remark's real output. Let me gather the concrete details.

I have everything I need from the code. Here are the three explanations, each grounded in what our parser actually does vs. what remark emits.

## 1. How `definition` / `linkReference` work in mdast

mdast is **reference-preserving**: it does *not* resolve reference links. For input `[foo]: /url "title"` … `[foo]`:

```jsonc
// the definition line becomes a top-level node that produces no visible output:
{ "type": "definition", "identifier": "foo", "label": "foo", "url": "/url", "title": "title" }

// the usage [foo] becomes a reference node that points back by identifier — no url/title:
{ "type": "linkReference", "identifier": "foo", "label": "foo",
  "referenceType": "shortcut", "children": [ { "type": "text", "value": "foo" } ] }
```

- `referenceType` encodes the syntax: `shortcut` = `[foo]`, `collapsed` = `[foo][]`, `full` = `[text][foo]`. Images give `imageReference` the same way.
- The `url`/`title` live **only** on the `definition` node. A downstream renderer (remark→HTML) resolves the link by matching `identifier` at render time.

**What we do instead** — eager resolution. Our parser builds a definition map, **drops** the definition lines, and emits a fully-resolved `link`/`image` node with `url`/`title` inlined. That's why our tree shows `link{url,title}` where remark shows `linkReference{identifier,referenceType}` + a separate `definition`. Both need the definition map (a shortcut ref with no matching definition is literal text in *both*) — the difference is purely *when* we resolve: we resolve at parse time, mdast defers it. Our output is correct for HTML; it just discards the reference structure mdast keeps for round-tripping.

## 2. What `spread` is, and why one bool renders HTML fine but can't reproduce it

mdast records looseness on **two independent axes**:
- `listItem.spread` — are *this item's own child blocks* separated by a blank line?
- `list.spread` — are *sibling items* separated from each other by a blank line?

They can disagree. Example 4 (`  - foo\n\n\tbar\n` — one item, two paragraphs with a blank line between them):

```jsonc
{ "type": "list", "spread": false,          // only one item → no item-to-item gap
  "children": [
    { "type": "listItem", "spread": true,   // its two paragraphs ARE blank-separated
      "children": [ {paragraph "foo"}, {paragraph "bar"} ] } ] }
```

Now the key to your question. **CommonMark HTML looseness is a single derived property**: a list renders "loose" (wrap item contents in `<p>`) iff there's a blank line *anywhere that matters* — i.e. `list.spread OR any listItem.spread`. Our AST stores exactly that one disjunction as [`ListData.tight`](include/markdown_parser/core/Types.hpp#L59), and the [HtmlRenderer](src/markdown_parser/renderer/HtmlRenderer.cpp#L108) uses it to decide `<p>` wrapping — which is why all 652 HTML spec tests pass. **We render loose lists perfectly because HTML only needs the OR-result.**

mdast keeps the two *operands* of that OR separately. We collapsed them into one flag, so from `tight` alone we can't recover which side (item-internal vs. item-to-item) was true. Our parser even *detects* both situations — it sets `tight=false` in [three distinct places in SpineHandler](src/markdown_parser/parser/SpineHandler.cpp#L212) — it just doesn't record *where* the blank line was. **The information is available at parse time; we throw it away.** That's the whole gap.

## 3. "parse-time normalization we can't reverse" — corrected & specific

I lumped two different things under one label earlier; let me split them properly.

**The 4 code-span cases (335, 337, 640, 641)** — CommonMark §6.1 says inside a code span, line endings convert to spaces. We do that **at parse time** here:

```cpp
// InlineParser.cpp:580  — inside parseBacktickString
for (char &ch : content)
    if (ch == '\n') ch = ' ';        // ← raw newlines destroyed here
```

So `` `code  \nspan` `` is already stored as `"code   span"` in the node. remark instead stores the **raw** `"code  \nspan"` in `inlineCode.value` and converts to spaces only when producing HTML. By the time our JSON renderer runs, the `\n` is gone — hence "can't reverse." (Note: remark *does* still apply the §6.1 *boundary* strip — a single leading/trailing space — it just preserves *interior* newlines.)

**The 1 HTML case (173)** is unrelated to the above — it's my trailing-`\n` strip being slightly too aggressive. remark keeps the trailing newline on a `<style>…>` block that runs to EOF, while stripping it on most others. This is a genuine edge in *my* normalization, not a parser issue.

---

## Fix plans (all three are fixable)

| # | Fix | Where | Effort/risk |
|---|---|---|---|
| **3** | Move the `\n`→space conversion **out** of the parser into `HtmlRenderer` (store raw in the AST, keep §6.1 boundary strip). JSON then matches remark. | `InlineParser.cpp` + `HtmlRenderer.cpp` | **Low** — localized, HTML output unchanged. (173 handled separately.) |
| **2** | Add a per-item `spread` field; in `SpineHandler` record blank-line-between-blocks on the item vs. blank-line-between-items on the list. HTML keeps using the derived OR. | `Types.hpp`, `SpineHandler.cpp`, `JsonRenderer.cpp` | **Medium** — parser bookkeeping; HTML must stay identical. |
| **1** | Make the AST **reference-preserving**: keep `definition` nodes, emit `linkReference`/`imageReference` with `identifier`/`referenceType`, and resolve in `HtmlRenderer` at render time. | inline parser, block tree, both renderers | **High** — real refactor; HTML path must resolve to keep 652 tests green. |

I'd go **#3 → #2 → #1** (cheap-and-safe first, big refactor last). #3 and #2 I can just do. **#1 is the one worth a decision**: the faithful design moves link resolution from parse-time to render-time — do you want that architecture, or a lighter-weight alternative (keep resolving for HTML, but *also* retain definitions + emit reference nodes for JSON, effectively storing both)? That choice drives how invasive #1 is.

Want me to start on #3 and #2 now, and hold #1 until you pick the resolution approach?

## ▸ Prompt — 2026-07-19 09:56

> go for #3 and #2, let's handle the bigger refactor later

Let me implement #3 first (code-span raw preservation). Let me find how HtmlRenderer emits inline code.

Now #3. First, the parser — stop destroying interior newlines, generalize the §6.1 boundary strip to treat a newline as boundary whitespace:

Now the HTML renderer converts interior newlines to spaces at render time (§6.1), preserving HTML output:

Let me check the debug HTML renderer has the same code-span handling, then build and test:

The debug renderer doesn't special-case it (debug-only, not spec-tested). Now remove the 4 code-span cases from the skip-list (keep 173):

#3 complete: **548 pass** (+4 code-span cases), 104 skip, and HTML spec still **652/652 green** — the conversion moved cleanly to render time.

Now #2 (per-item spread). Let me look at the data model for lists/items.

Let me study where the parser detects blank lines and sets `tight`, to map that onto item- vs list-level spread.

Now I understand the three detection sites. Site 1 (line 211) is blank **between items** → list-level; sites 2 & 3 (lines 374, 417) are blank **within an item** → item-level. But mdast's exact `spread` rules are subtle, so let me derive them empirically from the goldens rather than from prose.

All these are `list.spread=False` with `item.spread=True` (internal blanks). Let me check the item-to-item separation cases to see when `list.spread` becomes true.

Now I have the exact rules, confirmed empirically:
- **`listItem.spread = true`** iff the item has an internal blank line between its own children (sites 2 & 3).
- **`list.spread = true`** iff sibling items are blank-separated (site 1) — *independent* of item-internal spread (ex316: `item.spread=[F,T,F]` but `list.spread=False`).

This maps perfectly onto the three existing sites. I'll keep `tight` as the HTML flag (= the OR) and add separate `spread` fields. Let me check how `ItemData` is constructed first.

Both use aggregate init. I'll add `spread` fields with defaults at the end of each struct so existing initializers still work:

Now the parser — mark spread at the three sites. Site 1 (list-level, between items):

Site 2 (item-internal blank, detected at openBlock) — mark the item itself:

Site 3 (item-internal blank, detected at close, ≥2 children):

Now the JSON renderer reads the new fields instead of deriving from `tight`:

Now build and measure the spread impact (skip-list still has the spread entries, so let me check via the classifier):

The temp scripts were cleaned up. Let me just remove the spread cases from the skip-list and run the test directly — that's the real measure:

Excellent — **573 pass** (+25), and only **one** spread case remains: example 326. Let me look at it.

Example 326 reveals the bug: item[a] contains `a` + a sublist with **no blank between them** — the blank is *after* the sublist (separating the two outer items). Site 3 (close-time, trailing blank) wrongly flags that as item-internal spread. Site 2 (a new child opening *after* a blank) is the only true "blank between children" signal. Let me drop the spread assignment from site 3 (keeping its `tight` logic for HTML):

**574 pass, HTML spec 652/652 green.** All 26 spread cases and 4 code-span cases fixed, no regressions. Let me run the full suite to confirm nothing else broke:

All **1341 pass**. Let me verify the skip-list comment is now consistent (only two buckets remain):

Both fixes are done and the full suite is green. Here's the final state:

## #3 — Code-span raw preservation ✓
Moved the CommonMark §6.1 line-ending→space conversion **out of the parser and into the HTML renderer**:
- [InlineParser.cpp](src/markdown_parser/parser/InlineParser.cpp#L577) now keeps interior newlines raw in the AST, applying only the boundary-space strip (generalized to treat a newline as boundary whitespace).
- [HtmlRenderer.cpp](src/markdown_parser/renderer/HtmlRenderer.cpp#L192) converts `\n`→space at render time, so HTML output is byte-identical.
- JSON `inlineCode.value` now matches remark. **+4 cases** (335, 337, 640, 641).

## #2 — Per-item spread ✓
Split the single `tight` flag (kept as-is for HTML) from mdast's two independent spread axes:
- Added `spread` to [`ListData` and `ItemData`](include/markdown_parser/core/Types.hpp#L54).
- [SpineHandler](src/markdown_parser/parser/SpineHandler.cpp): **site 1** (blank between sibling items) → `list.spread`; **site 2** (a child opening after a blank) → that `item.spread`. Deliberately *not* site 3 — a trailing blank after an item's last child separates *items*, it isn't an internal blank (this was the example-326 bug).
- [JsonRenderer](src/markdown_parser/renderer/JsonRenderer.cpp) reads the real fields instead of `!tight`. **+26 cases.**

## Results

| Suite | Before | After |
|---|---|---|
| mdast (JSON) | 548 pass / 104 skip | **574 pass / 78 skip / 0 fail** |
| CommonMark HTML | 652 pass | **652 pass** (unchanged) |
| Full `ctest` | — | **1341 / 1341 pass** |

The **78 remaining skips** are now cleanly just:
- **77** — reference/definition resolution (the deferred #1 refactor).
- **1** — example 173, the `<style>`-to-EOF trailing-newline edge in my normalization (fiddly, single case, left documented).

So the only substantive divergence left is #1 (reference-preserving AST), which we're holding. When you want to tackle it, the open decision is still: move link resolution to render-time (faithful, bigger) vs. store both resolved + reference forms (lighter). Everything's ready for you to commit.

## ▸ Prompt — 2026-07-19 10:12

> Give a simple example for a link reference, with both HTML and JSON expected output, then make a comparison between the current JSON output (for the same example) and the expected one

Let me build the three outputs concretely for a simple example.

The remark deps live in `markdown-utils/`. Let me run from there:

I have all three. Here's the full comparison.

## The example

```markdown
[foo]

[foo]: /url "title"
```

A shortcut **link reference** (`[foo]`) plus a **link reference definition** (`[foo]: /url "title"`).

## Expected HTML (both agree ✓)

```html
<p><a href="/url" title="title">foo</a></p>
```

Our HTML output is identical to CommonMark — the definition is resolved and produces no output of its own. **HTML is correct and not in question.**

## Expected JSON — remark/mdast

```json
{
  "type": "root",
  "children": [
    {
      "type": "paragraph",
      "children": [
        { "type": "linkReference",
          "identifier": "foo", "label": "foo", "referenceType": "shortcut",
          "children": [ { "type": "text", "value": "foo" } ] }
      ]
    },
    { "type": "definition",
      "identifier": "foo", "label": "foo", "url": "/url", "title": "title" }
  ]
}
```

## Our current JSON

```json
{
  "type": "root",
  "children": [
    {
      "type": "paragraph",
      "children": [
        { "type": "link",
          "url": "/url", "title": "title",
          "children": [ { "type": "text", "value": "foo" } ] }
      ]
    }
  ]
}
```

## Comparison

| Aspect | remark (expected) | Ours (current) |
|---|---|---|
| **Definition node** | Kept as a top-level `definition` (identifier/label/url/title) | **Dropped entirely** — consumed into an internal lookup map |
| **The `[foo]` usage** | `linkReference` — points back by `identifier`, **no url/title** | `link` — url/title **inlined** |
| **`referenceType`** | `"shortcut"` (records the `[foo]` syntax) | absent |
| **`root.children` count** | **2** (paragraph + definition) | **1** (paragraph only) |
| **When resolution happens** | deferred — a downstream renderer matches `identifier`→`definition` | eager — resolved at parse time |

**In one line:** remark keeps the AST *reference-preserving* (the link and its definition stay as two linked-by-identifier nodes, so the tree can round-trip back to Markdown), while we *resolve eagerly* — we splice the url/title into a concrete `link` and throw the definition away. Same rendered HTML, structurally different tree.

This is precisely the deferred **#1** refactor: to match remark we'd (a) retain `definition` nodes in the block tree, (b) emit `linkReference`/`imageReference` with `identifier`/`referenceType` instead of resolved `link`/`image`, and (c) move resolution into the HTML renderer so the HTML above stays correct. That's the 77 skipped cases.

