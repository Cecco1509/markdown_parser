# Is a leak possible? Ownership audit of the node tree

**Theme:** Memory and performance hardening

**Source transcript:** `597bf292-13cb-44cb-8e2e-283a6490b94c.jsonl`  
**Records:** 0–248

**Dates:** 2026-07-22 09:35 → 2026-07-22 10:35  
**User turns:** 3

**Related commits:**

- `0b67771 update markdown documentation to match the current state of the project`

---

## ▸ Prompt — 2026-07-22 09:35

> @docs/09_open_decisions.md 
> See chapter 9.1, a question was raised on the BlockNode and InlineNode memory management, this section claims that the current state of the program could lead to memory leaks due to the deallocation of SpineHandler::document_ which leads the children nodes to be leaked. 
>
> I already run the binary with the "leaks" mac os terminal command, and it reported no leaks, maybe I should try with a bigger file?
>
> I need you to investigate on this matter, first assure that a memory leak is possible, right now this problem is solved by doing nothing, so I would like to know if it is worth doing anything. Before proposing any solution I'd like to carefully understand the problem

I'll dig into the actual code before trusting the doc.

The doc describes a design that no longer exists. Let me confirm at the allocation sites.

Zero raw allocations in the entire parser. Let me verify the actual remaining risk — recursive destruction depth.

No nesting-depth limit exists. Let me test that empirically.

Confirmed a crash. Let me find the threshold and, crucially, *where* it crashes — parse vs. destruction.

Threshold is between 10k and 20k. Now the key question — which recursion blows the stack:

Crash is in **HtmlRenderer::visit** recursion, not the destructor. Let me isolate whether the destructor is also vulnerable.

Now a harness that parses and destroys **without** rendering, to isolate the destructor:

Exit codes were masked by the pipe. Let me redo cleanly:

Let me pin the exact cliffs for destructor vs. renderer, since that determines whether fixing the destructor alone buys anything.

Now the crucial question for this project — there's a WASM target, whose stack is far smaller than 8 MB:

No `-sSTACK_SIZE` is set, so the WASM build gets Emscripten's 64 KB default stack — 128× smaller than native's 8 MB. Let me simulate that.

Clean linear scaling: ~600 bytes of stack per nesting level. Let me confirm the Emscripten default that applies here.

Emscripten 5.x defaults to a 64 KB stack. Let me test the *actual* shipped wasm rather than extrapolate.

The node harness can't load the module. Let me check the Emscripten default directly from the toolchain instead of guessing.

Confirmed: `STACK_SIZE = 64*1024` default, no override in this project. Let me re-verify the no-raw-allocation claim without my earlier filter.

## Bottom line

**§9.1 is stale — it describes the spec's design, not your code.** A leak is not merely unlikely, it's structurally impossible. `leaks` reporting nothing wasn't luck, and a bigger file won't change it.

The doc claims `openBlock` uses `raw new` with parent→child linked lists and only `document_` being owned. None of that is true anymore:

- [BlockNode.hpp](include/markdown_parser/core/BlockNode.hpp) holds `std::vector<std::unique_ptr<BlockNode>> children` + `inline_children` — not `first_child`/`next` raw pointers.
- [SpineHandler.cpp:391](src/markdown_parser/parser/SpineHandler.cpp#L391) uses `make_unique`; `spine_` is itself a vector of `unique_ptr`, and [closeBlock](src/markdown_parser/parser/SpineHandler.cpp#L464-L468) *moves* ownership into the parent's `children`.
- A grep for `new`/`delete`/`malloc`/`.release()` across all of `src/markdown_parser/` and `include/markdown_parser/` returns **zero hits**.

So options A and B in §9.1 solve a problem you don't have. Nothing to do there.

## But the investigation did surface a real bug

Chasing this, I found the thing §9.1's option A was *actually* gesturing at — "avoids stack overflow for deep trees". That risk is real, and it's a crash, not a leak. Deeply nested input segfaults:

```
depth 13000 -> exit=0
depth 15000 -> exit=139   (SIGSEGV)
```

lldb puts the faulting frame in `HtmlRenderer::visit` ([HtmlRenderer.cpp:60](src/markdown_parser/renderer/HtmlRenderer.cpp#L60)), not the destructor. Isolating each phase with a harness that parses and destroys *without* rendering, there are three independent recursion cliffs on an 8 MB stack:

| phase | cliff |
|---|---|
| renderer recursion | ~14 000 |
| `~BlockNode` recursion | ~15–16 000 |
| parse / `parseInlineContent` | ~50–80 000 |

Consumption is ~600 bytes of stack per nesting level, and it scales linearly and predictably — I confirmed by re-running under `ulimit -s`: 512 KB → cliff ~850, 1 MB → cliff ~1700, 8 MB → ~14 000.

**That linearity is why the WASM build matters.** [CMakeLists.txt:139-142](CMakeLists.txt#L139-L142) sets no `-sSTACK_SIZE`, and your toolchain (Emscripten 5.0.7) defaults to `var STACK_SIZE = 64*1024` — 64 KB, **128× smaller than native**. Extrapolating the measured slope puts the browser cliff at roughly **100 nesting levels**, i.e. `>` repeated ~100 times crashes the web parser.

Caveat on that number: I could not get `web/dist/markdown_parser.js` to initialize under node to measure it directly (the Module never populated), so ~100 is an extrapolation from native frame sizes, not a measurement. Wasm frames differ, so treat it as order-of-magnitude — but even 5× off, it's trivially reachable, whereas 14 000 is not. For scale, the deepest nesting in your entire test corpus is 4.

## Is it worth doing anything?

For the leak: no. I'd rewrite §9.1 to say "not applicable — ownership is `unique_ptr`" rather than leave "(SOLVED)" attached to a description of code that doesn't exist, since it'll keep re-raising this question.

For the stack overflow: the native cliff is unreachable in practice and I would ignore it. The WASM one is worth taking seriously if that build ever sees untrusted input, since it's a browser-tab crash on a short pasted string. The cheap fix is a nesting-depth cap in `openBlock` (cmark does exactly this) — that bounds all three recursions at once, unlike making only the destructor iterative, which by the numbers buys nothing because the renderer fails first anyway. Happy to dig into where the cap belongs and what depth to pick, but I'll hold there since you wanted the problem understood first.

## ▸ Prompt — 2026-07-22 10:03

> Review this idea:
> So the problem relies on the recursion calls of the code, which is unbound and depend on the user input, of course this is not safe. It is an implementation detail that is worth addressing. I'd ask the following question, would a for loop save us from this problem? Any recursion algorithm can be rewritten as a cycle, would it resolve the situation? Or the possible unbounded cycle could always create problems? If it is a problem in both implementation I'd say that we put a cap in openBlock, which is not unreasonable since this program is a markdown parser, who is going to nest more than 10, 15, 30!! blocks to explain something?

Good question, and the answer differs per recursion. But before I answer it, there's a gap in the proposal worth testing: a cap in `openBlock` bounds *block* nesting. Inline nesting is a separate tree with its own recursive destructor and renderer, and `openBlock` never sees it.

It hung at n=20000 — that's a *different* failure mode. Let me characterize it.

Emphasis is fine. So it's the brackets:

Your reasoning is right, and I'd land where you land — but with three corrections, plus one finding that changes the priority order.

## "Would a for loop save us?"

For the stack overflow specifically: **yes, genuinely** — and not merely because the heap is bigger. That's the part worth being precise about.

Any recursion can be rewritten with an explicit worklist, which moves frames from the 8 MB (or 64 KB) stack to the heap. But the real win is qualitative. Stack overflow is undefined behaviour: you get SIGSEGV, no diagnostic, nothing catchable, and in WASM a dead tab. Heap exhaustion throws `std::bad_alloc` — catchable, reportable, recoverable. So iteration converts an uncatchable crash into a handleable error. That's a category change, not a bigger number.

Your instinct that "the unbounded cycle could always create problems" is also correct, but it's the *good* kind of problem. After conversion, consumption becomes proportional to input: a 10 MB document uses ~10 MB. Today a **100-byte** document crashes. The defect isn't that resource use is unbounded — it's that the cliff sits at a fixed depth unrelated to document size.

**But the cost is wildly uneven across the three sites**, which is why I wouldn't convert all of them:

- `~BlockNode` → iterative is cheap and self-contained: a standard idiom (splice children into a local worklist, drain it), no API change, ~15 lines.
- `HtmlRenderer::visit` → iterative is genuinely ugly. It emits an open tag, recurses, then emits a close tag, so an explicit stack needs enter/exit sentinel frames — a state machine replacing a clean, readable visitor. For this codebase that's a bad trade.

So "rewrite the recursion" is right in principle but only worth paying for in the destructor. The cap is the better primary move, exactly as you say. Three corrections though:

**1. Don't cap at 10–15–30.** Your intuition undercounts, because the number that matters is *tree* depth, not visual nesting. Every list level costs two nodes (`List` + `Item`), so 15 visible indent levels is ~30 tree levels before any blockquotes. Real documents — nested API docs, changelogs — reach 6–10 visual levels without anyone being clever. I'd use **100**, which is the ballpark cmark uses for the same guard (I'm going from memory there; cmark isn't vendored here, so treat the exact constant as unverified). 100 is still ~140× below the native cliff and comfortably below the WASM one, while never touching a real document.

**2. Decide what happens *at* the cap.** Not an implementation detail — a spec-visible choice. CommonMark guarantees every input is valid markdown; there's no parse error. So you can't bail. cmark stops opening new blocks and lets the excess fall through as text content. You need to pick that behaviour deliberately, or you'll break the "no input is invalid" property.

There's also friction you'll hit immediately: [`openBlock`](include/markdown_parser/parser/SpineHandler.hpp#L52) returns `BlockNode&`, so it has no way to express "refused." The check has to live in the three callers ([207](src/markdown_parser/parser/SpineHandler.cpp#L207), [221](src/markdown_parser/parser/SpineHandler.cpp#L221), [334](src/markdown_parser/parser/SpineHandler.cpp#L334)) or the signature changes. The depth itself is free — it's just `spine_.size()`.

**3. The cap doesn't cover inlines at all** — and that's where the worse bug is.

## The finding that reorders the priorities

Inline nesting is a separate tree that `openBlock` never sees. Testing it, nested emphasis is fine (12 000 levels in 0.01s). Nested brackets are not:

```
brackets n=1000  real=0.36s
brackets n=2000  real=1.22s
brackets n=4000  real=5.14s
brackets n=8000  real=18.54s
```

That's clean **O(n²)** — and it's not a stack or memory problem, so neither a nesting cap nor an iterative rewrite touches it. An 8 KB file of `[` characters burns 18 seconds of CPU; ~16 KB gets you well over a minute. On the WASM build that's a frozen tab from a trivially small paste, and it needs no unusual depth at all — just repetition.

So I'd rank the work: the O(n²) bracket path first (smallest input, worst effect, and completely unaddressed by your plan), the `openBlock` cap second (cheap, bounds all three block recursions at once), and the iterative destructor third — optional, since with the cap in place it's belt-and-braces. Setting `-sSTACK_SIZE` on the WASM target is a one-line partial mitigation for the block side, worth doing regardless since 64 KB is very tight for any recursive parser.

Want me to dig into the bracket loop and find where the quadratic behaviour comes from?

## ▸ Prompt — 2026-07-22 10:35

> Yes let's investigate on the brackets nesting exponential time, then we'll address the other problems

Let me look at the bracket handling. (Worth noting it's quadratic, not exponential — that matters, since quadratic is usually one redundant scan rather than a redundant branch.)

Let me confirm the mechanism by profiling rather than guessing.

2428 of 2430 samples on a single line. Let me see exactly which:

Confirmed — line 1215, the shortcut-reference path. Let me check `normaliseLabel` and the length-cap asymmetry I noticed:

`normaliseLabel` is O(len) with a heavy constant — two passes plus `foldString` (UTF-8 decode + table lookup per codepoint), three allocations. Called once per closer on an O(n) label gives the quadratic.

Note the asymmetry: the *full*-reference path at line 1155 caps the label scan, but paths 3 and 4 don't. Let me check whether that cap is the spec's rule:

Both the collapsed and shortcut paths, *and* the definition scanner, lack the cap — the 999 rule is enforced in exactly one of four places. Let me verify the diagnosis by patching and measuring, then revert.

8000: **18.54s → 0.09s** (~200×), and 32 000 now runs in 0.11s. But n=1000 looks worse than the 0.36s baseline — let me check that isn't a regression.

Noise — 0.07s vs 0.36s baseline. Now the critical part: does the cap break conformance?

Huge number of failures — but that smells pre-existing (the whole mdast suite failing, and there's an untracked goldens file in git status). Let me get counts, then establish the baseline.

API Error: Unable to connect to API (ENOTFOUND)

