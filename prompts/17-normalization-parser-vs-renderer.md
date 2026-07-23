# Who normalizes: parser or renderer?

**Theme:** JSON/mdast + test refactor

**Source transcript:** `0d3e1fa2-7f5d-4a01-8791-2c175d59c8da.jsonl`  
**Records:** 850–978

**Dates:** 2026-07-21 07:30 → 2026-07-21 09:20  
**User turns:** 5

**Related commits:**

- `f2e0cf3 move code blocks endline normalization, from parse-time to render time`
- `60c84d7 add matching end to html blocks`

---

## ▸ Prompt — 2026-07-21 07:30

> Explain better case 541, why it is an error in the JSON output and not in the HTML output?

Let me pull the concrete outputs for 541 so the explanation is grounded, and trace where the whitespace is lost.

The outputs make it precise. Every field matches remark **except one**:

```
definition.label:   ours "Foo\nbar"      remark "Foo\n  bar"
                              ↑ the two leading spaces of line 2 are gone
```

Both `identifier` fields are `"foo bar"`, the `linkReference` matches, and the **HTML is byte-identical to the spec** (`<p><a href="/url">Baz</a></p>`). So why does the same missing whitespace break JSON but not HTML?

## Where the spaces are lost

The input label spans two lines:
```
[Foo
  bar]: /url      ← two leading spaces
```
When the spine parser accumulates a paragraph's raw text, it **strips leading indentation from continuation lines** — that's required CommonMark behavior (paragraph continuation lines skip leading whitespace). So by the time `maybeScanLinkRefDefs` scans the paragraph content, it's already `[Foo\nbar]: /url` — the `  ` was consumed as block structure, upstream of the definition scanner. The raw bytes are simply gone before we ever look at the label.

## Why JSON needs those bytes but HTML doesn't

mdast stores a definition with **two forms of the label**, for two different purposes:

| field | value here | purpose |
|---|---|---|
| `identifier` | `"foo bar"` | the **normalized lookup key** — whitespace-collapsed + case-folded |
| `label` | `"Foo\n  bar"` | the **raw authored text**, preserved for fidelity / round-tripping back to Markdown |

**HTML only ever consumes the `identifier`.** Rendering the reference `[Baz][Foo bar]` requires two things, and neither touches the raw label:

1. **Resolution** — match the usage to the definition. This compares *normalized* labels. `Foo\n  bar` and the usage's `Foo bar` **both normalize to `foo bar`**, so the match succeeds. Whitespace collapsing is the whole point of normalization — the lost spaces are exactly the kind of difference it's designed to erase. Our `identifier` is computed correctly, so HTML resolves fine.
2. **Emission** — output `<a href="/url">Baz</a>`. The `href` comes from the definition's **URL**; the link text comes from the **usage** bracket (`Baz`). The definition's label text is never printed.

So the definition's raw label is **semantically invisible to HTML** — it survives only in normalized form as a hash key, and normalization is deliberately lossy about whitespace.

**JSON is different because mdast exposes the raw `label` as a first-class field.** The AST's contract is to faithfully record the source (so a tool can regenerate the original Markdown), which means `label` must reproduce the author's bytes — indentation included. We can't, because our block parser normalized that whitespace away as a necessary step of parsing, and there's no way to recover it at render time.

## The one-line version

HTML consumes only the **normalized identifier** (a lossy lookup key + the URL), so collapsed whitespace is irrelevant — the link still resolves and renders. JSON additionally surfaces the **raw label** for round-trip fidelity, and that's the one place the upstream whitespace-stripping becomes observable. It's the same category as the code-span newline case earlier: information the parser legitimately discards for rendering, which only a source-faithful AST would still need.

## ▸ Prompt — 2026-07-21 07:37

> We could skip the normalization of these labels and hand it to the renderer, what would be the changes?

Good question — but let me check something first, because the answer hinges on *where* those spaces actually disappear. The `identifier` normalization already happens in the parser and isn't the culprit (both identifiers are correct). Let me trace where the label whitespace is really lost.

Found the exact spot — and it reframes the answer. The label whitespace doesn't die in any "label normalization" step. It dies here, in paragraph text accumulation ([SpineHandler.cpp:357](src/markdown_parser/parser/SpineHandler.cpp#L357)):

```cpp
} else if (tip().type == NodeType::Paragraph) {
    // Strip up to 3 leading spaces per CommonMark §4.4.
    if (cur.next_non_space() <= 3)
        text_start = cur.next_non_space();   // ← line 2 "  bar]" → skip the 2 spaces
}
appendText(cur, text_start);
```

Every paragraph continuation line has its leading indentation stripped before the text is even stored — this is correct, required CommonMark behavior (a paragraph `foo\n   bar` must render as `foo\nbar`, not with the spaces). By the time the *definition scanner* runs on the assembled `string_content`, it's already `[Foo\nbar]: /url` — the spaces are gone, upstream of anything label-related.

## So what does "move label normalization to the renderer" actually change?

Nothing observable — it's a **pure refactor**, and it does **not** fix 541. Here's why:

Today the parser does two label transforms:
- `identifier = normaliseLabel(raw)` (case-fold + whitespace-collapse)
- `label = processEscapesAndEntities(raw)` (resolve `\]`, entities)

If you instead store the untransformed `raw` on the node and call those same two functions in the `JsonRenderer`, you get **byte-identical output** — you've just relocated two function calls. And the `raw` you'd store is the *already-stripped* `Foo\nbar`, so 541 still mismatches. The escape cases (194/549/550) would keep passing either way. Net effect on the test suite: **zero**. Plus a downside — the renderer would now have to depend on parser utilities (`normaliseLabel`, `processEscapesAndEntities`), reintroducing the layering coupling we deliberately avoided.

The premise doesn't hold: there's no label-normalization step destroying the whitespace to hand off.

## What would *actually* fix 541

You'd need to preserve the label's original source bytes, which conflicts with the paragraph needing them stripped for rendering. Two real options:

1. **Keep a second, un-stripped copy of the paragraph text.** In `appendText`, also accumulate a raw variant (leading whitespace intact), and scan definitions from *that* while rendering the paragraph from the stripped one. Cost: an extra string on every paragraph node (needed only until definitions are scanned), plus branching in the hot append path — for one edge case.

2. **Source-span tracking (how micromark does it).** Retain the full original source and record byte offsets on nodes, then extract the definition label from the original source rather than from accumulated text. Cost: threading the source + positions through the whole parser — a large architectural change.

Both are disproportionate to a single spec example, and neither is the "hand normalization to the renderer" move — the fix has to happen in the **block phase**, where the bytes still exist, not the render phase.

## Recommendation

Leave 541 skip-listed. It's the same category as the code-span raw-newline case: information the block parser legitimately discards for correct rendering, which only a fully source-faithful AST would retain. We're at 650/652 with HTML untouched; chasing this one costs a raw-text copy on every paragraph or a positions refactor, for no rendering benefit. If you ever want *full* source fidelity (round-trip Markdown→AST→Markdown), option 1 is the pragmatic path — but that's a feature decision, not a bug fix.

## ▸ Prompt — 2026-07-21 07:53

> I think that we could have added more duties to the parser that would actually be in the hand of the renderer, like in this case the parser strips needed information from the text. What I'm trying to say is that we have a parser which is also a halfway renderer, and then the renderer just renders.
>
> I would like to decouple the parser and renderer, there should be no more code to write since the html is working fine, we would have only to move some code.
>
> First thing is to spot where the parser goes to far, stripping away needed information for a renderer.

This is a legitimate architectural critique, and worth taking seriously — we've already done exactly this refactor twice (code-span newlines → HtmlRenderer; reference resolution → preserving AST), so the direction is proven. Let me do a real audit of where the parser still bakes in render decisions, rather than theorize. Let me survey the text transformations in the parser.

Notably, the parser does **not** call `escapeHtml`/`escapeUrl` — those are already renderer-only, which is good. The parser's transforms are mostly entity/escape *decoding* into text/url/title, which is actually correct AST content (mdast stores decoded values). Let me verify the key question empirically: does remark itself strip continuation-line whitespace in text, or only preserve it for the label field?

This is the decisive test, and it flips the conclusion. remark strips continuation-line whitespace in `text.value` **identically to us** (`foo\n   bar` → `"foo\nbar"`), and our output matches byte-for-byte. So that stripping isn't render overreach — it's exactly what mdast bakes into the AST too.

That reframes your whole thesis, and I want to show you the evidence rather than just assert it.

## The strongest evidence: the mdast suite itself

Here's the thing — **if the parser were a "halfway HTML renderer," the JSON output could not match remark's mdast.** The whole point of the 652-case comparison against remark is that it detects *any* HTML-flavored decision baked into the tree. We're at **650/652**. That pass rate is essentially a proof that the AST is render-neutral: every place the parser once leaned toward HTML (code-span newlines, reference resolution) showed up as a *JSON test failure*, and we moved it out. There isn't a hidden reservoir of misplaced render code — the golden suite would be red if there were.

## The audit

Every text transform the parser does, classified against what mdast stores:

| Parser transform | Location | mdast does it too? | Verdict |
|---|---|---|---|
| Entity decode → text/url/title | InlineParser [535](src/markdown_parser/parser/InlineParser.cpp#L535), 974, 1009, 1060 | Yes — `text.value`/`url` are decoded | **AST-canonical, keep** |
| Backslash-escape → char | InlineParser [450](src/markdown_parser/parser/InlineParser.cpp#L450) | Yes — `\*`→`*` in `value` | **keep** |
| Code-span §6.1 boundary strip | InlineParser [584](src/markdown_parser/parser/InlineParser.cpp#L584) | Yes | **keep** |
| Code-span interior `\n`→space | ~~InlineParser~~ | No (render-only) | **already moved to HtmlRenderer** ✅ |
| Paragraph continuation-ws strip | SpineHandler [357](src/markdown_parser/parser/SpineHandler.cpp#L357) | **Yes — just proved it** | **keep** |
| `identifier`/`label` normalization | InlineParser [1234](src/markdown_parser/parser/InlineParser.cpp#L1234), scanner | Yes — both are mdast fields | **keep** |
| Trailing-space / hard-break trim | InlineParser 401, 422, 434 | Yes — inline rules | **keep** |
| HTML-escaping, URL percent-encoding | **not in the parser** | — | **already renderer-only** ✅ |

Everything is either mdast-canonical or already sitting in the renderer. There's no "move this pile of code" waiting to happen — the split you're describing is, empirically, mostly already in place.

## What 541 actually is (and why "just move code" doesn't apply)

541 is **not** over-stripping — remark strips the same bytes we do. remark still shows `"Foo\n  bar"` in `definition.label` because micromark keeps the **original source + byte offsets on every node** and re-slices the label straight from the source for that one field, *bypassing* the stripped text. That's a different axis entirely: **source provenance**, not normalization. Reproducing it is **new code** (thread the source string + positions through the parser), not relocated code — so the "no new code, just move it" promise specifically doesn't hold for the one case that motivated this.

## Honest conclusion

Your instinct is sound and we *acted* on it — twice — but the audit says the job is essentially done. The parser is not a halfway renderer; the AST is render-neutral, and the golden suite proves it. The two remaining skips are:
- **541** — source-provenance fidelity (an *addition*: position tracking), not a misplaced-code *move*.
- **173** — a single HTML-block trailing-newline edge.

Neither is "parser doing the renderer's job." If you want, I can run the **reverse** audit — check whether `HtmlRenderer` does any *parsing* that belongs upstream — but on the parser→renderer direction, the evidence says it's already clean. Want me to do that reverse pass, or spec out the source-position feature if full round-trip fidelity is a goal you care about?

## ▸ Prompt — 2026-07-21 08:18

> I want to fix the last two errors and add the positions, I'm referring to the ones we stripped away in the golden generation, so that we have a full compliant working parser, for both html and json
>
> Can you evaluate the effort on making these three changes?
> - fix 541
> - fix 173
> - add positions in the validation specs and also in our json output

Good — a real "full mdast compliance" goal. Let me investigate the actual scope of each before estimating, especially positions (the hard one). Let me check what position data our nodes carry and how the inline parser tracks source offsets.

Critical finding for positions: **`InlineNode` has zero position fields**, and the inline parser runs on `input_` — the block's *assembled/stripped* `string_content` — with `pos_` offsets that have **no mapping back to source** line/column/offset. Let me check block-level position accuracy and the 173 rule.

Now I have what I need to evaluate. 173's rule is clear (type-1 `<style>` runs to **EOF**, so its trailing `\n` is block content; blank-line-terminated blocks like `<div>` exclude it — our uniform strip can't tell them apart). And remark positions **every** node with `{line, column, offset}`. Let me do one last check on our block-level position data.

I now have a complete picture of the scope. Here's the honest effort evaluation for all three.

## Current position-data reality (the key constraint)
- Blocks populate **only `start_line` / `end_line`**. `start_col` exists but is never set; there's **no `end_col`, no byte offset** anywhere.
- **`InlineNode` has no position fields at all**, and the inline parser runs on the block's *assembled/stripped* `string_content` with offsets that don't map to source.
- `ScannedLine` tracks columns for indentation logic but exposes **no absolute source offset**.

remark, by contrast, positions **every** node with `{line, column, offset}` for start and end.

---

## #2 — Fix 173 (HTML-block trailing newline) — **SMALL** (~half a day)
**Root cause:** the uniform `stripTrailingNewline` in `JsonRenderer` can't distinguish a type-1 `<style>` block that runs to **EOF** (trailing `\n` *is* block content → keep) from a blank-line-terminated `<div>` block (trailing `\n` is the terminator → strip).

**Work:** record how the HTML block terminated (EOF/tag-close vs blank line) at close time, store the exact content, and drop the uniform strip for that node. Contained to HTML-block handling + the renderer. HTML output already correct, so no risk there.
**Risk:** low; bounded to the 7 HTML-block types. Independent of the others.

---

## #1 — Fix 541 (raw label whitespace) — **SMALL standalone, ~FREE if #3 is done**
**Standalone:** keep an un-stripped copy of the paragraph's source lines so the definition scanner slices the label with original whitespace (touches the `appendText` hot path + scanner; +1 string per paragraph).
**If #3 lands:** re-slice the label directly from source by offset — essentially free.
**Risk:** low–moderate. Recommendation: **don't do it standalone — fold it into #3**, since #3 builds exactly the source access it needs.

---

## #3 — Positions (goldens + our JSON) — **LARGE, high-risk. Dominates the other two combined.**

| Sub-task | Effort | Notes |
|---|---|---|
| (a) Source offset/line/col tracking in `ScannedLine`/spine | Moderate | new global offset counter, per-line start offset |
| (b) Block positions: start/end `{line,column,offset}` | Moderate | line numbers exist; add columns+offsets, match remark's conventions |
| (c) **Inline positions** | **Large / risky** | the crux — see below |
| (d) Golden regen (stop stripping `position`) | Trivial | but flips comparison to strict full-tree matching |
| (e) Emit `position` in JSON | Easy | mechanical once data exists |

**Why (c) is the hard part:**
- `InlineNode` needs new start/end fields, and we must build a **source-map** from each block's stripped/joined `string_content` back to source `(line, column, offset)`. That buffer drops leading whitespace, joins continuation lines, and the inline parser decodes escapes/entities — all **length-changing**, so the map must survive them.
- Positions must be threaded through delimiter/emphasis/bracket processing, which **splits text runs and synthesizes span nodes** (emphasis/strong/link) from delimiter locations — the algorithm reorders and erases nodes, so getting spans exactly right is error-prone.
- The killer is **exact parity with micromark on all 652 cases, every node** (tabs expanding columns, multi-line spans, reference splitting). Expect a long iterative diff-grind; a few edge cases may resist exact matching.

**Realistic size:** comparable to, or larger than, the *entire* mdast effort so far — multiple days, with genuine risk on the last few percent of inline parity.

---

## Recommendation

**Sequence:** #2 (cheap, independent) → #3's source infrastructure → #1 falls out for free → then block positions → then grind inline positions.

**Strong suggestion — a de-risked middle option:** do **block-level positions only**, and in the golden generator strip `position` from *inline* nodes only (keep it on blocks). That delivers real position support at **Moderate** effort and sidesteps the inline source-mapping/micromark-parity nightmare. You'd have ~90% of the practical value (block spans are what most tools use) without the open-ended tail.

**My honest take:** #2 and #1 are clearly worth doing — small, bounded, and they close the last two real gaps. Full **inline** positions are a research-grade parser feature; for this deliverable the ROI is questionable versus the risk. I'd do #2 + #1 + **block-only** positions and call the parser fully spec-compliant on structure/content, with positions as a documented partial (blocks yes, inline offsets are the one deferred item).

Want me to start with #2 and #1 (both small and high-value), then scope the block-only position pass in detail before committing to the inline tail?

## ▸ Prompt — 2026-07-21 09:20

> First of all, fix #2, the solution you described is fine.
>
> After that we'll think about how we are going to fix #3

Understood — I'll fix #2 only and leave the goldens untouched (I see the generator now keeps positions, but regenerating is for #3, so I won't run it now). Let me first nail down remark's exact HTML-block trailing-newline rule with a few probes.

Clear rule now: **only a type-1 (`<script>`/`<style>`/`<pre>`) block that runs to EOF without matching its closing tag keeps its trailing `\n`** — everything else strips one. Let me see how our HTML blocks are stored and closed.

The signal is clean: `checkHtmlBlockEnd` is the *only* place a type-1..5 block's end condition is matched. So "keep the trailing newline" = **type ∈ 1–5 AND end condition never matched (closed at EOF)**. Let me check how `HtmlBlockData` is constructed, then add an `end_matched` flag.

Wait — the golden **now has 3187 `position` keys**. The generator was re-run, so the golden already contains positions. Let me check the current test state before touching anything.

The golden was regenerated **with** positions, so the mdast suite is fully red (0 pass) — everything now mismatches on the `position` key that our output lacks. That's the #3 work surfacing early. The #2 fix is correct regardless of positions, so I'll implement it and verify it **position-independently** (comparing the html value with positions stripped from both sides). Let me implement.

Now the JSON renderer — keep the trailing newline for a type-1..5 block that ran to EOF:

Now build and verify position-independently (the gtest is position-gated right now):

**651/652 position-independent, 173 now passes, 0 regressions.** #2 is fixed. Let me confirm the HTML spec and other suites are still green (the mdast gtest is expectedly red from positions, which is #3):

#2 is fully fixed. Let me update the skip-list to reflect that 173 is resolved (only 541 remains):

#2 is done. Summary and an important heads-up.

## #2 fixed (HTML-block trailing newline)

The rule, verified against remark: **only a type-1..5 block that runs to EOF (its end condition never matched) keeps its trailing `\n`**; everything else strips one.

- [Types.hpp](include/markdown_parser/core/Types.hpp) — added `HtmlBlockData.end_matched`.
- [SpineHandler.cpp](src/markdown_parser/parser/SpineHandler.cpp#L573) — sets `end_matched = true` in `checkHtmlBlockEnd` (the only place a type-1..5 end condition is matched).
- [JsonRenderer.cpp](src/markdown_parser/renderer/JsonRenderer.cpp) — keeps the trailing `\n` for a type-1..5 block with `!end_matched`, strips otherwise.

Verified:
- `<style>`→EOF keeps `\n`; `<div>` strips — both match remark.
- HTML spec **652/652**, mermaid **37/37** — no regressions.
- Position-independent mdast: **651/652** (only 541 remains).

## ⚠️ Heads-up: the golden was regenerated *with* positions

The `generate-golden.mjs` edit (commenting out `stripPosition`) was run, so `commonmark_spec_mdast.json` now contains **3187 `position` keys**. That means the **`JsonMdastTest` gtest is currently fully red (0 pass)** — every case mismatches on the `position` field our output doesn't emit yet. That's #3 surfacing early, not a regression from my change (I verified #2 with positions stripped from both sides).

So right now:
- HTML spec, mermaid, unit tests: **green**.
- `JsonMdastTest`: **red until #3 lands** (positions).

That's the natural segue to planning **#3**. Before we start, one decision I'd flag: do you want to keep the golden position-full now (committing to full inline+block positions), or regenerate stripping only *inline* positions for the block-only middle option I described? Either way, want me to lay out the concrete #3 implementation plan next?

