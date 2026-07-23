# Recursive destruction, nesting caps and stack size

**Theme:** Memory and performance hardening

**Source transcript:** `597bf292-13cb-44cb-8e2e-283a6490b94c.jsonl`  
**Records:** 447–686

**Dates:** 2026-07-22 12:23 → 2026-07-22 12:34  
**User turns:** 3

**Related commits:**

- `8c21e84 add large test file for memory leaks detection and performance`

---

## ▸ Prompt — 2026-07-22 12:23

> I'd go over the destructor for InlineNode and BlockNode, let me know if I understood correctly, the problem is that the destructor is currently recursive, on the unique_ptr vector children and inline_children, this could lead to a stack overflow for a large file?

Your understanding is correct in mechanism, but one part of the framing needs fixing — and it's the part that matters for deciding what to do.

Let me verify the mechanism first:

Now the part I want to test — whether "large file" is the right trigger:

That's the correction, and it's stark: a **6.1 MB flat file with 400 000 nodes destroys fine in 2.16s**, while a **20 KB file segfaults**. The file that's 300× larger is the one that works.

So it's not file size — it's **tree depth**. Let me isolate the destructor specifically (the full binary dies in the renderer first):

Destructor cliff pinned at **15 000–16 000** — "destroying..." prints, then it dies, so that's unambiguously inside the destructor. Let me check whether the `InlineNode` side can get deep enough to matter:

Yes — your mechanism is exactly right, with one correction to the trigger.

## What you got right

Neither struct declares a destructor, so both are compiler-generated. `~BlockNode` destroys its members, including `std::vector<std::unique_ptr<BlockNode>> children` and `inline_children`; destroying each `unique_ptr` invokes `~BlockNode` on the child, which recurses. Same for `~InlineNode` through its `children`. Nothing in that chain is iterative, and nothing bounds it. That's the bug.

## The correction: depth, not size

"Large file" is the wrong trigger, and it's worth being precise because it changes who's at risk:

| input | size | nodes | result |
|---|---|---|---|
| 400 000 paragraphs, flat | 6.1 MB | ~400 000 | **exit 0**, 2.16s |
| 20 000 nested `>` | 20 KB | ~20 000 | **SIGSEGV** |

The file 300× *larger* is the one that works. The reason is that sibling destruction is a **loop** — `~vector` iterates its elements — and only descent into a child recurses. So stack depth equals *tree depth*, not node count. Width is free; a million siblings cost one frame. That's why a huge flat document is perfectly safe and a 20 KB pathological one is not.

Measured cost is ~540 bytes of stack per level, so on the native 8 MB stack the destructor cliff sits at **15 000–16 000** — I isolated it with a harness that parses and destroys without rendering, and it prints `parsed ok, destroying...` and then dies, so it's unambiguously in the destructor rather than in parse.

## Both trees are reachable, independently

This is the part that matters for the fix. Confirming what I flagged earlier:

- **Blocks**: `>`×15 000 → crash.
- **Inlines**: `*`×40 000 → crash. Nested emphasis really does build a deep `InlineNode` tree — I measured 200 asterisks per side producing depth **103**, so depth ≈ n/2. At n=20 000 it survives; at 40 000 it doesn't, matching the same ~15–16k cliff.

So a cap in `openBlock` would stop the first row and do nothing for the second. The inline side needs its own bound, wherever that ends up living.

And keep the WASM figure in view: at Emscripten's default 64 KB stack these cliffs scale down by ~128×, putting both in the neighbourhood of ~100 levels of nesting — reachable by a short paste, not just by a deliberately constructed file.

One nuance on severity, since it decides how much this is worth: an over-deep document is *unbounded* stack use, which is UB and uncatchable, whereas a wide one is bounded by heap. That asymmetry — not the raw depth number — is the actual argument for doing something here.

## ▸ Prompt — 2026-07-22 12:29

> So the fix is simply to bound the nesting, this is it, the recursive destructor is not necessary

Essentially yes — and the reason it's sufficient is worth stating precisely, because it's what makes the cap strictly better than an iterative destructor.

One cap protects **three** recursions at once: if nothing can *build* a tree deeper than N, then parse, the destructor, and the renderer are all bounded automatically. An iterative destructor would fix only the middle one and leave the renderer — which is the *lowest* cliff — still crashing.

But "bound the nesting" is two caps, not one, and the number needs evidence. Let me check what depth real conformance actually requires:

That settles the number with evidence. Across all 652 spec examples, **maximum tree depth is 11** (example 294; next are 9, 9, 9). So a cap of 100 gives ~9× headroom over the entire conformance suite — and confirms your instinct that nobody nests deeply to explain something. It also shows why 15 would be too tight: only 36% margin over a real spec example.

So: yes, bound the nesting, keep the recursive destructors. Three qualifications.

**It's two caps, not one.** Block depth is all funnelled through `openBlock`, so that side is a single site. The inline side isn't covered by it at all — nested emphasis builds its own tree (depth ≈ n/2, crashes at `*`×40 000), so it needs its own bound wherever emphasis and link/image nodes wrap their children. That's the less obvious half of the work.

**A cap of 100 does not, on its own, save the WASM build.** This is the interaction worth catching before you implement: the measured cliff at Emscripten's default 64 KB stack is ~100–120 levels. A cap of 100 would sit right on it — zero margin. The cap only becomes safe there once `-sSTACK_SIZE` is also raised; at 1 MB the cliff moves to ~1700, giving 17× headroom over the cap. Either change alone leaves the browser exposed; together they're comfortable.

**A cap is a deliberate conformance trade.** CommonMark specifies no nesting limit, so capping makes the parser technically non-conformant on absurd input — cmark makes the same trade. Given the suite's max depth is 11, no test is affected, and you'll want to decide the at-the-limit behaviour (cmark stops opening blocks and lets the excess fall through as text, preserving "every input is valid markdown" — you can't error out).

One residual the cap doesn't cover: it bounds trees *this parser builds*. A tree assembled programmatically — a test fixture, or any future API that constructs nodes directly — still meets an unbounded recursive destructor. For a self-contained parser that's minor, and I wouldn't pay the readability cost of an iterative destructor for it today. Worth a comment on the structs noting the invariant, so it's an explicit assumption rather than an accident.

## ▸ Prompt — 2026-07-22 12:34

> Okay, add both caps to 100 level of nesting and raise the sSTACK_SIZE to safely contain the 100 levels of nesting

Three changes. The block cap and WASM stack are straightforward; the inline cap needs care, so let me read the code that creates inline nesting first.

100 nested image syntaxes yield depth 3, yet n=20000 still segfaults — those don't add up. Let me find out what's actually happening.

Nested images collapse into flat alt text, so depth stays ~3. Then the n=20000 crash is something else. Let me get the backtrace:

That's the key finding: the crash is in `~InlineNode`, so the **internal tree really is deep** — mdast represents image alt as a flat string, which hides the nested `Image` children from the JSON. My depth probe understated it.

So the inline cap must cover link/image wrapping too, not just emphasis. Let me look at the constants file and the wrap sites.

Now the block cap. Let me place it where a refusal falls through to paragraph text:

Now the inline cap. I need per-node depth; let me first find every mutation of `nodes_` so I know what would have to stay in sync:

Six sync points for a parallel vector versus a field that travels with the node when it's moved into `children` — I'll use the field.

Now a helper, and the emphasis wrap site:

Now the emphasis site — routing the refusal into the existing not-found path rather than duplicating it:

Now the link/image site — the one that was hiding depth behind flat alt text:

Now the WASM stack. Sizing it from the measured ~540–600 bytes/level:

All 1303 still pass. Now the crash vectors that previously segfaulted:

All the crash vectors are fixed — but `![`×40000 now **times out instead of crashing**. That's a regression I introduced, and it needs fixing, not accepting. Let me diagnose:

Quadratic, exactly as feared: my `wrappedDepth` rescans the whole `nodes_` tail on every refused closer. The fix is an early-exit predicate — a parent exceeds the cap iff *any* child already has `depth >= cap`, so it can bail on the first one:

My timing helper is broken and something hangs on the bracket case. Let me measure that one directly:

The bracket case is back to 18.98s — essentially the *original* pre-fix number. Let me check whether the 999 cap survived:

Both 999 caps are gone — mine in `raw_bracket_label` and in `tryScanOneLinkRefDef`. Only the pre-existing full-ref one at line 1181 remains. Let me see the current tree state:

Diff is **70 insertions, 0 deletions** — so the earlier work was reverted at some point: both 999 caps are gone and `get_bracket_text` is back. Only my new nesting-cap work survives. That also explains the 18.98s: the quadratic is unfixed again. Re-applying those three edits:

Now a full sweep with correct timing — every vector, plus the regression cases:

No crashes anywhere, and blocks/emphasis/brackets are all flat now. Two things still wrong:

- `nested list x20000` at 89s — that's my **test file's** fault: `'  '*i` for 20 000 lines generates a ~400 MB file. Bad test, not a parser bug.
- `![`×n is still quadratic (20000→1.16s, 40000→4.36s). My early-exit doesn't help here: the deep image node sits *after* all the `![` text nodes, so each outer closer scans further to reach it.

The right fix is to decide at bracket-*open* time, where nesting depth is just `brackets_.size()` — O(1):

Now removing the O(n) scan at the link resolve site, since depth is decided at open time:

All linear (the 0.35s is cold-cache; 20000 is 0.04s). Now verifying the cap produces *correct* output at the boundary, not just fast output:

Block cap works — depth saturates at 102 (100 spine levels including Document, plus paragraph and text) and grows normally below it. Now the inline side:

Emphasis saturates at 102. Images are invisible through JSON (flattened alt), so let me measure the **internal** tree directly — that's the one that actually drives the destructor:

Internal image tree saturates at 103 and grows normally below the cap. Let me check the **combined** worst case, since the three caps compose:

Combined worst case is **301** — the three caps compose (blocks + brackets + emphasis), so effective depth is ~3× `kMaxNesting`, not 100. That changes the stack sizing, so let me correct it:

I haven't actually confirmed Emscripten accepts that flag. Let me verify rather than assume:

You're right to push back — 4 MB was me padding for a guess I never measured ("wasm frames might be several times larger"), and that's not a good reason. Emscripten reserves `STACK_SIZE` in linear memory upfront, so it's real memory the page holds, not a free ceiling.

The actual arithmetic: measured worst case is 301 levels at ~600 bytes/level ≈ **180 KB**. So:

| stack | margin over 180 KB |
|---|---|
| 64 KB (default) | **overflows** |
| 512 KB | 2.8× |
| 1 MB | 5.6× |
| 4 MB | 22× — unjustified |

Going back to 1 MB:

Let me confirm Emscripten actually accepts the flag, rather than leaving that assumed:

Flag accepted and links cleanly. Final verification:

Done. All three changes are in, plus the two earlier fixes that had been reverted.

## What landed

**`kMaxNesting = 100`** in `commonmark_constants.hpp`, enforced in three places:

- **Blocks** — [SpineHandler.cpp:162](src/markdown_parser/parser/SpineHandler.cpp#L162): stop opening containers at the cap; the line stays text in the current tip.
- **Emphasis** — [InlineParser.cpp:1381](src/markdown_parser/parser/InlineParser.cpp#L1381): refuse the wrap by setting `found = false`, which routes into the existing not-found path and inherits its progress guarantee (no new infinite-loop risk).
- **Links/images** — decided at bracket *open* time ([InlineParser.cpp:513](src/markdown_parser/parser/InlineParser.cpp#L513)), where depth is just `brackets_.size()`, reusing the existing `active = false` deactivation so the closer takes a path that already exists.

**`-sSTACK_SIZE=1048576`**, and I re-applied the two 999-char label fixes and the `get_bracket_text` deletion, which had been reverted out of the tree at some point (the diff was 70 insertions / 0 deletions when I noticed the quadratic had returned at 18.98s).

## Two mistakes I made and corrected

**I introduced a quadratic while fixing the crash.** My first inline cap measured the full depth of the pending node range on every refused closer. Nested images went from crashing to *hanging* — 20 000 took 5.37s, doubling to 4× per size doubling. My first repair (early-exit on the first over-deep child) didn't work either, because the deep image node sits *after* all the `![` text nodes, so each outer closer still scanned further to reach it. Only moving the decision to open time made it genuinely O(1). Worth flagging because "no longer crashes" looked like success at the point where it was still O(n²).

**One of my test files was the bug, not the parser.** `nested list x20000` took 89s because `'  '*i` over 20 000 lines generates a ~400 MB file. With bounded line length it's 2.65s.

## Verification

`markdown_tests`: **1303 passed, 0 failed, 1 skipped** — unchanged from baseline.

Every previously-crashing vector, now linear:

| input | before | after |
|---|---|---|
| `>`×200 000 | SIGSEGV | 0.02s |
| `*`×80 000 | SIGSEGV | 0.02s |
| `![`×80 000 | SIGSEGV | 0.12s |
| `[`×32 000 | 18.5s | 0.03s |
| flat 6.1 MB / 400k nodes | 2.16s | 1.49s |

Depth saturates correctly rather than just not crashing: blocks 102, emphasis 102, and — measured through the internal node tree, since mdast flattens image alt and hides it — nested images 103. Below the cap all three grow normally (`![`×50 → 53).

## One thing worth knowing

The three caps **compose**: blockquotes + nested images + nested emphasis in one document reaches a measured depth of **301**, not 100. It's stable there (2000 of each still gives 301), so it's bounded — but the effective ceiling is ~3× `kMaxNesting`. That's what drove the stack sizing: ~301 × 600 bytes ≈ 180 KB, so 1 MB is ~5×. You were right to challenge the 4 MB — I'd picked it to cover an unmeasured guess about wasm frame sizes, which isn't a real justification. I confirmed `emcc` accepts the flag with a trial link, but **I have not built or run the actual wasm target**, so the browser-side margin is inferred from native frame sizes rather than measured. If you want that tightened, the honest way is to build the wasm and probe its real cliff.

