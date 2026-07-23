# Profiling and fixing the O(n^2) bracket/label path

**Theme:** Memory and performance hardening

**Source transcript:** `597bf292-13cb-44cb-8e2e-283a6490b94c.jsonl`  
**Records:** 249–446

**Dates:** 2026-07-22 12:00 → 2026-07-22 12:10  
**User turns:** 2

**Related commits:**

- `8c21e84 add large test file for memory leaks detection and performance`

---

## ▸ Prompt — 2026-07-22 12:00

> I disconnected for a while, resume the analysis

Resuming. State: the experimental 999-cap patch is still applied to [InlineParser.cpp:1190](src/markdown_parser/parser/InlineParser.cpp#L1190), and I have a test run with many failures but no baseline to compare against. Let me get the counts.

Output was truncated by `tail`. Let me do a full run, patched, capturing everything.

Careful — session cwd is the scratchpad, so that `--test-dir build` is wrong. Let me redo with an absolute path.

Meanwhile, let me establish what the failures actually are — the mdast suite depends on a goldens file that's untracked in git status.

`JsonRenderer.cpp` is also modified — that's the user's own in-progress work, not mine. That likely explains the mass mdast failures. Let me check.

The JsonRenderer diff looks like a pure reformat. Let me verify that it's whitespace-only:

Not whitespace-only — 72 real insertions. The user is mid-work. Let me see the substance:

Understood — that discarded my experimental patch too. Let me confirm the tree state.

The user discarded their JsonRenderer work; my patch survived. That's actually the clean state I need. Let me save the patch, get the true baseline, then compare.

Tree is clean now. Let me build and run the baseline.

While that runs — the spec's own wording on the 999 limit, to confirm the cap is conformance-correct rather than a hack:

Confirmed spec-mandated — and it exposes an off-by-one in my experimental patch: I used `>= 999`, but "at most 999" means only `> 999` is invalid. Let me check the baseline.

<br>

While the baseline finishes, here's where the investigation stands.

## The quadratic is confirmed and localized

It's **quadratic, not exponential** — which is why it was findable. `sample` put **2428 of 2430** stack samples on one line, [InlineParser.cpp:1215](src/markdown_parser/parser/InlineParser.cpp#L1215), the shortcut-reference path in `handleBracketCloser`:

```cpp
if (!resolved && !tried_full_ref) {
    std::string raw = raw_bracket_label();
    auto it = ref_map.find(normaliseLabel(raw));   // ← 2428/2430 samples
```

The mechanism: `bracket.src_pos` is the position just after the opening `[`, so for `[[[[a]]]]` the k-th closer from the inside builds a label spanning ~2k characters. Each of the n closers therefore allocates an O(n) string and runs `normaliseLabel` over it — two passes plus `unicode_fold::foldString`, which decodes UTF-8 per codepoint and allocates again. Summed: **O(n²) with a very heavy constant.**

Worth stressing: in my test `ref_map` is *empty*, so every one of those lookups is guaranteed to miss. The full cost is paid to build and case-fold a key that cannot possibly match.

## The fix is a spec rule, not a heuristic

The 999-character cap already exists in this file — at [line 1158](src/markdown_parser/parser/InlineParser.cpp#L1158), on the full-reference path only. The spec ([spec.txt:7996](docs/commonmark/spec.txt#L7996)) says:

> A link label can have at most 999 characters inside the square brackets.

So a bracket span longer than 999 chars **is not a valid link label at all**, on any of the four paths. Applying the cap before building the label isn't a mitigation bolted on for performance — it's enforcing a conformance rule that's currently applied in **one of four places**. The other three: collapsed (path 3), shortcut (path 4), and `tryScanOneLinkRefDef` in SpineHandler, which stores definition keys with no length check either.

One correction to my own experiment: I wrote `>= 999`, but "at most 999" means only `> 999` is invalid. The real patch needs `> 999`, or it rejects legal 999-char labels.

## Measured effect

| n | before | after |
|---|---|---|
| 1 000 | 0.36s | 0.07s |
| 4 000 | 5.14s | 0.08s |
| 8 000 | 18.54s | 0.09s |
| 32 000 | (not attempted) | 0.11s |

~200× at n=8000, and the curve goes flat — 32 000 nested brackets, 4× the input that previously took 18 seconds, now completes in 0.11s.

## What I still owe you

The conformance check is **not yet done**. My earlier test run showed mass mdast failures, but those came from the in-progress `JsonRenderer.cpp` changes in your working tree, not from the cap — which is exactly why a baseline is needed before claiming anything. Now that the tree is clean I'm running baseline first, then re-applying the corrected `> 999` cap to diff the two. I'll report the comparison rather than assert the patch is safe.

Two things worth flagging for when we get to the fix:

- **`get_bracket_text()`** ([line 1100](src/markdown_parser/parser/InlineParser.cpp#L1100)) is defined but I never found a call site — likely dead code carrying the same O(n) shape. Worth confirming and deleting.
- The cap bounds *label* cost, but the bracket **nesting** still produces an O(n)-deep inline tree, so the recursive `~InlineNode` and renderer descent remain exposed. That's the separate stack issue, and it confirms the point from earlier: a cap in `openBlock` wouldn't have touched any of this.

That's the stale run with the bad cwd — ignoring it. Let me check the baseline.

Two ctest runs are racing on the same build dir — the stale one is contaminated (it was launched pre-revert but is now testing the rebuilt baseline binary). Killing it.

That's the stale run I killed. Waiting on the clean baseline:

Meanwhile, let me confirm the `get_bracket_text` dead-code suspicion:

Confirmed dead — defined at [line 1099](src/markdown_parser/parser/InlineParser.cpp#L1099), zero call sites. Correction to what I said earlier: since it's a never-called lambda the compiler emits no body, so it costs nothing at runtime — it's a cleanliness issue only, not part of the quadratic.

Let me set up the conformance edge case the cap could plausibly regress:

Useful: baseline resolves *both*, so it's already non-conformant on the 1000-char case. The corrected cap should fix that rather than break it. Let me check the baseline suite.

Both empty — that job produced nothing. Let me stop fighting ctest and run the test binaries directly, which is faster and gives cleaner counts.

My `pkill` killed the baseline job. Let me run the test binary directly instead — faster and controllable.

Baseline is clean: **1303 passed, 0 failed, 1 skipped** (the known 541 case) — and it runs in 39ms; ctest's per-test process spawning was the slow part. Let me baseline the other suites too.

`spec_tests` fails on a missing data file — pre-existing environment issue (stale Jul 18 binary), unrelated. Baseline is established. Now applying the **corrected** `> 999` cap:

Identical to baseline — no regression. Now the edge cases and perf with the corrected bound:

Investigation complete, and the fix is verified.

## Root cause

`sample` put **2428 of 2430** stack samples on one line — [InlineParser.cpp:1215](src/markdown_parser/parser/InlineParser.cpp#L1215), the shortcut-reference path in `handleBracketCloser`. `bracket.src_pos` points just after the opening `[`, so in `[[[[a]]]]` the k-th closer from the inside spans ~2k characters. Each of the n closers allocates an O(n) string and runs `normaliseLabel` over it (two passes plus `unicode_fold::foldString`, which decodes UTF-8 per codepoint and allocates again) — **O(n²) with a heavy constant**. In the test `ref_map` is empty, so every one of those lookups is guaranteed to miss; the entire cost buys a key that cannot match.

It's quadratic, not exponential — which is why it was a single redundant scan rather than a redundant branch.

## The fix is a conformance rule, not a mitigation

[spec.txt:7996](docs/commonmark/spec.txt#L7996): *"A link label can have at most 999 characters inside the square brackets."* A longer span isn't a valid label on **any** path. That cap already existed in this file at [line 1158](src/markdown_parser/parser/InlineParser.cpp#L1158) — but only on the full-reference path. It was applied in one of four places; the collapsed and shortcut paths and `tryScanOneLinkRefDef` all lacked it.

Checking the spec caught an off-by-one in my first attempt: I'd written `>= 999`, but "at most 999" means only `> 999` is invalid. The applied patch uses `> 999`.

## Verification

| | baseline | patched |
|---|---|---|
| markdown_tests | 1303 passed, 1 skipped, 0 failed | **identical** |
| md_tests | 5 passed | **identical** |
| 999-char label resolves | yes | yes (preserved) |
| 1000-char label resolves | yes | **no** — spec says invalid |
| n=1000 | 0.36s | 0.07s |
| n=8000 | 18.54s | 0.09s |
| n=32000 | — | 0.11s |

The curve is flat: 32 000 nested brackets — 4× the input that previously took 18 seconds — now finishes in 0.11s. And the boundary row is a small **conformance gain**: baseline resolved the illegal 1000-char label; patched correctly refuses.

That baseline number is worth trusting now — my earlier run showing mass mdast failures was your in-progress `JsonRenderer.cpp` edits, not the cap. Establishing the baseline separately is what disambiguated it.

**The patch is currently in your working tree** (the diff above is the whole change). Revert it if you'd rather land it separately.

## Two follow-ups this exposed

1. **`tryScanOneLinkRefDef` still has no 999 cap.** A `[1000×b]: /url` line is swallowed as a definition and renders as nothing; the spec says an invalid definition should fall through as a paragraph. Pre-existing and unchanged by this patch, but it's the natural companion fix — and it's the reason the cap on the lookup side is safe today (an over-long key can be stored, but per spec nothing should ever match it).
2. **`get_bracket_text()`** at [line 1099](src/markdown_parser/parser/InlineParser.cpp#L1099) has zero call sites. Correcting what I said earlier: an uncalled lambda emits no body, so it costs nothing at runtime — cleanliness only.

Note the cap bounds *label* cost but not *nesting*: `[`×32000 still builds a 32 000-deep inline tree, so the recursive `~InlineNode` and renderer descent stay exposed. That's the stack issue, and it confirms the earlier point — an `openBlock` cap would never have touched any of this. Ready to move to that whenever you are.

## ▸ Prompt — 2026-07-22 12:10

> Add the 999 cap to tryScanOneLinkRefDef and remove the unused get_bracket_text

Two edits. First, removing the dead lambda:

Now the definition scanner:

The build actually failed on an unknown target name, so nothing rebuilt — that result is stale. Rebuilding properly:

Now genuinely rebuilt and matching baseline. Let me rebuild the other suite and verify the new behavior:

Confirmed: `markdown_tests` is the only live target — `md_tests`/`spec_tests` on disk are stale Jul-18 leftovers, not in the build graph. Now verifying the behavior change:

Both correct now. Final verification — perf, plus a pathological definition input to make sure the new early-return didn't introduce its own scan problem:

Both changes are in and verified.

## What changed

**`tryScanOneLinkRefDef`** ([SpineHandler.cpp:636](src/markdown_parser/parser/SpineHandler.cpp#L636)) — rejects labels over 999 chars after the closing `]` is confirmed. I put the check *after* the `]` match rather than folding it into the loop guard on purpose: a guard like `p - label_start <= 999` exits the loop at exactly 1000, where `content[p]` may legitimately be `]`, so the over-long label would slip through the existing `content[p] != ']'` test. The explicit post-check has no such boundary hazard.

**`get_bracket_text`** ([InlineParser.cpp:1098](src/markdown_parser/parser/InlineParser.cpp#L1098)) — deleted, zero call sites.

## Verification

`markdown_tests`: **1303 passed, 0 failed, 1 skipped** — identical to the baseline I captured before any of this.

The residual conformance gap is now closed. For a 1000-char label:

```
before:  <p>[bbb…]</p>                      ← definition line silently swallowed
after:   <p>[bbb…]: /url &quot;t&quot;</p>  ← renders as paragraph, per spec
         <p>[bbb…]</p>
```

The legal 999-char boundary still resolves to a link, so the cap lands exactly where the spec puts it. Performance is unchanged and flat (n=32000 in 0.11s), and 2000 over-long definition lines parse in 0.18s — the early return is a cheap length compare before the `substr` and `normaliseLabel`, so it adds no scan cost.

## Two corrections to things I said earlier

- The `md_tests` "5 passed" I reported was a **stale Jul-18 binary**. `ninja -t targets` confirms `markdown_tests` is the only live test target; `md_tests` and `spec_tests` are leftovers from an older CMake configuration and aren't rebuilt. That also explains `spec_tests` failing on a missing data file — it's not a real regression, but it does mean those two binaries are dead weight in `build/`.
- My first run of these edits reported 1303 passing from a build that had **failed** on an unknown target name, so nothing recompiled. The numbers above are from a confirmed rebuild.

Remaining from the original thread, untouched: the recursive `~InlineNode`/renderer descent still goes O(n) deep on nested brackets, and the block-side `openBlock` cap plus the WASM `-sSTACK_SIZE` default. Say the word when you want to pick those up.

