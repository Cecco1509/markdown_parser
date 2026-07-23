# Markdown parser architecture and component design

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `0e6bddd6-b9e4-4f03-b0ef-85eccdbb1283`  
**Created:** 2026-06-02  
**Messages:** 22 (11 prompts)

**Related commits:**

- `053d360 revise specs and add open decisions`
- `3e30935 solved open questions`
- `6ae7eeb first implementation for continuation, open and close rules for each blocks`

---

## ▸ Prompt — 2026-06-02 15:11

> Help me design the project components, how they interact between them. The goal is to have a document comprehensive of the backbone components behavior, structure and data flow.
>
> We take these as references:
>
> * CommonMark Spec v0.31.2, Appendix A: A Parsing Strategy — [spec.commonmark.org/0.31.2](https://spec.commonmark.org/0.31.2/)
> * cmark source code — [github.com/commonmark/cmark](https://github.com/commonmark/cmark), blocks.c
>
> What is already defined: 
>
> 1. process_line function behavior:
>   ┌─ Step 1: walk open block stack top-down
>   │    for each open block: does the line satisfy its continuation condition?
>   │    → record matched/unmatched prefix (do NOT close unmatched yet)
>   │
>   ├─ Step 2: look for new block starts in the remaining line
>   │    if new block found:
>   │      → close all unmatched blocks from Step 1
>   │      → open new block as child of deepest matched block
>   │    if NO new block found AND innermost open block is paragraph:
>   │      → lazy continuation: append text, keep unmatched blocks open
>   │    if NO new block found AND NOT lazy:
>   │      → close unmatched blocks, text falls into last open block
>
> 1. 
>
> What we need to define:
> Block nodes structure
> Inline nodes structure
> Continuation rules (check in CommonMark spec)
> open/close blocks rules (check in CommonMark spec)
> Components:
>
> * pre-scan line
> * SpineHandler (the spine is the Open Block Stack)
> * phase 2 inline parser
>
> aside from the block/inline nodes structures, now we don't make any implementations maybe just methods signatures

Now I have a clear picture of the design system. Let me think carefully about the architecture before building the interactive document.

The system has three main phases: **Pre-scan** → **SpineHandler (block phase)** → **Inline Parser**, with two core data structures (Block nodes, Inline nodes). Let me build a rich interactive reference document.
Now the detailed interactive document — method signatures, data structures, continuation and open/close rules all in one navigable reference:
Here's what the document covers across both widgets:

**Architecture diagram** (top) — the full pipeline at a glance: Pre-scan → SpineHandler → Inline parser, with the open block stack and AST as the central data structures. The node type enums are listed at the bottom. Every box is clickable to drill in.

**Component reference** (bottom, tabbed) — seven sections:

- **Overview** — data flow in pseudocode form, showing how a raw line becomes an inline-parsed AST node
- **Block nodes** — all 14 core fields plus per-type `BlockData` union variants (Heading, CodeBlock, List, Item, HtmlBlock, LinkRefDef)
- **Inline nodes** — core fields, `InlineData` union, and the internal delimiter stack entry used during phase 2
- **Pre-scan** — `ScannedLine` structure, `LineClass` enum, and four method signatures
- **SpineHandler** — full state, nine method signatures covering `step1_walk_spine`, `step2_new_blocks`, `open_block`, `close_block`, `close_unmatched`, `finalize`, and lazy continuation detection
- **Inline parser** — internal state and eleven method signatures (backtick strings, autolinks, HTML inline, bracket stacks, emphasis handling, `process_emphasis`, label normalisation)
- **Continuation & open/close rules** — the eight per-type continuation predicates, eight open-block triggers in priority order, and five close-block conditions, all tied back to spec section numbers

A few design decisions embedded here worth flagging:

1. `ScannedLine.classification` is a hint, not a command — the SpineHandler re-verifies before acting, keeping the pre-scanner lightweight.
2. `string_content` is a `StringBuilder` on the block; inline parsing is intentionally deferred until `close_block` is called, so the inline parser never sees a partially-built block.
3. `last_line_blank` on `BlockNode` is essential for determining list tightness at finalization time — it needs to survive the close.
4. The `ref_map` lives on the Document node and is populated lazily as paragraphs are finalized, before being threaded into the inline parser.

## ▸ Prompt — 2026-06-02 15:33

> I would remove the classification in the pre scanner, I want the continuation/opening/closing rules to rely only on the row data not on the inferred type. This is the case because it makes things confusing when you have multiple openers.
>
> Also the pre scan phase does not expand tabs. Check the CommonMark specification

Good catches on both. Let me verify the tab handling in the spec before we update anything.
The spec is clear. Two corrections confirmed:

1. **Tabs are not expanded** — tabs in lines are not expanded to spaces; however, in contexts where spaces help to define block structure, tabs *behave as if* they were replaced by spaces with a tab stop of 4 characters. This is a crucial distinction: the raw tab byte stays in `string_content` exactly as-is; the *virtual column accounting* for structural decisions (indent depth, continuation matching) is what uses tab-stop arithmetic. Tab expansion is a rendering/output concern for indented code blocks, not a pre-processing step.

2. **Classification removed** — agreed, pre-scan should not infer a `LineClass`. The continuation and opening predicates each inspect the raw byte slice directly; no shared hint should be threading through.

Here's the corrected `PreScanner` spec:
Two things worth highlighting beyond what's in the widget:

**On tab accounting and `base_col`:** this subtlety is easy to miss. The spec's example 6 (`>→→foo` becoming a blockquote containing an indented code block) only works correctly if the tab's virtual expansion is computed *starting from the column after the `>`*, not from column 0. So the SpineHandler needs to pass the current column position to `scan_with_offset` every time it consumes a container prefix. That consumed-column tracking lives in the SpineHandler, not the pre-scanner.

**On the ambiguous-line problem:** removing `LineClass` is the right call precisely because the same byte sequence can mean different things at different points in the algorithm. `---` is a setext underline if a Paragraph is open and passes its continuation check first; it's a thematic break otherwise. `* foo` is a list marker in step 2, but if it appears inside a fenced code block it never reaches step 2 at all — the fenced code block's continuation predicate consumes it. A pre-assigned classification would either be wrong in one of those cases or would need to return a set of possibilities, which is just reinventing the predicates.

## ▸ Prompt — 2026-06-02 15:49

> cmark uses a flag to handle partial tab expansion, they do not expand tabs, but they keep a flag when a tab needs to be expanded partially, do you think it would be good idea to remove it and replace the tab whenever it is partially consumed
>
> example from CommonMark Spec tests - https://spec.commonmark.org/0.31.2/spec.json {
>     "markdown": "- foo\n\n\t\tbar\n",
>     "html": "<ul>\n<li>\n<p>foo</p>\n<pre><code>  bar\n</code></pre>\n</li>\n</ul>\n",
>     "example": 5,
>     "start_line": 395,
>     "end_line": 407,
>     "section": "Tabs"
>   },
>
> Do you think that expanding partially consumed tabs is a good idea?

Good concrete example to reason from. Let me trace through what actually happens in that case before giving an opinion.

The input is `- foo\n\n\t\tbar\n`. The third line is `\t\tbar`.

In the list item context, the item's padding is 2 (the `- ` marker takes 2 columns). So step 1 walks the spine and the list item continuation predicate needs to consume 2 columns of indent from `\t\tbar`.

The first tab expands to column 4. The item only needed 2 columns, so the tab is **partially consumed** — 2 columns go to satisfying the list item continuation, and 2 columns of "virtual space" are left over going into the inner context. The remaining virtual indent seen by step 2 is 2 (leftover) + 4 (second tab at column 4→8) = 6 columns, which is ≥ 4, so it opens an indented code block. The content stored for that code block is `  bar` — two spaces of leftover from the split tab, then `bar`.

That `  bar` in the output is exactly what the spec expects.

---

**My recommendation: keep a partial-tab flag (or equivalent), do not physically replace the tab.**

Here is why:

**`string_content` must reflect the source faithfully.** For code blocks, the spec says the content is the literal lines minus the indentation consumed by the fence/block. If you physically insert spaces at the split point, you are now storing a mutated string that didn't exist in the input. That mutation has to be undone or carefully tracked when you produce the final code block content. The cmark approach stores the raw bytes and records how many virtual columns of a tab were already spent — at output time, it emits the correct number of spaces for the remainder. Cleaner because the transformation is one-way and happens once, at serialisation.

**It couples pre-scan to spine state.** The partial tab only arises *because* a container consumed part of it. How many columns are left depends on what the SpineHandler decided to consume. Pre-scan doesn't know that. If you want to expand-in-place, the expansion has to happen inside the SpineHandler at the moment of consumption — which means mutating the line buffer mid-walk. That makes the step 1 loop stateful in an unpleasant way: each container predicate would need write access to the line, not just read access.

**The flag is cheap and local.** The partial-tab state is a single integer (columns remaining from the split tab) attached to the current scan position. It doesn't leak into the node structure and doesn't affect inline parsing at all, since inline parsing sees `string_content` after indentation has been stripped by the block-opening logic.

**The concrete implementation:** rather than a boolean flag, carry a `partial_tab_remaining: usize` alongside the scan cursor in the SpineHandler's step 1 walk. When a container predicate consumes N columns and a tab boundary falls in the middle, record how many virtual columns of that tab remain. The next predicate or opener check picks up from there. At code block content time, emit `partial_tab_remaining` spaces followed by the rest of the literal line from `next_non_space` onward.

The only case where physical replacement might be tempting is fenced code block content stripping (removing up to N spaces of the opening fence indent from each content line). But even there, the stripping amount is known at finalization time and can be applied during the output pass, not stored back into `string_content`.

## ▸ Prompt — 2026-06-02 15:52

> Okay lets keep the partial_tab_remaining state variable, update the interactive documentation accordingly

The visualizer tool seems to be having issues. Let me deliver this as a clear written update instead, which is what matters for the document.

---

Here is the precise diff to apply to the existing component reference:

---

### `ScannedLine` — no changes

The structure stays exactly as defined in the last revision. No `classification` field, tabs never expanded in `content`.

---

### `SpineHandler` state — two new fields

```
partial_tab_remaining  usize   Virtual columns remaining from a tab that was
                               partially consumed by a container predicate during
                               the current line walk. Reset to 0 at the top of
                               process_line. Written only by consume_columns,
                               read and cleared only by append_text.

current_col            usize   Real column position of the scan cursor as step 1
                               walks the spine and consumes container prefixes.
                               Passed as base_col to scan_with_offset for inner
                               contexts. Reset to 0 at the top of process_line.
```

Both are ephemeral per-line cursor state, not persistent node state.

---

### `consume_columns` — new method, the single split site

```
fn consume_columns(&mut self, line: &[u8], byte_offset: usize, n_cols: usize) -> usize

// Advance byte_offset through line by exactly n_cols virtual columns.
//
// Phase A — drain leftover from a prior split on this line:
//   if partial_tab_remaining > 0:
//     take = min(partial_tab_remaining, cols_needed)
//     partial_tab_remaining -= take
//     cols_needed           -= take
//     current_col           += take
//     // byte_offset unchanged — still on the same tab byte
//
// Phase B — walk forward consuming spaces and tabs:
//   for each byte at byte_offset:
//     b' '  → byte_offset++, cols_needed--, current_col++
//     b'\t' → tab_w = next tab stop - current_col
//             if tab_w <= cols_needed:
//               byte_offset++, cols_needed -= tab_w, current_col += tab_w
//             else:                                  ← TAB STRADDLES BOUNDARY
//               partial_tab_remaining = tab_w - cols_needed
//               current_col += cols_needed
//               cols_needed = 0
//               // byte_offset NOT advanced — tab byte stays for append_text
//
// Returns new byte_offset.
// This is the only function that writes partial_tab_remaining.
```

---

### `append_text` — updated

```
fn append_text(&mut self, line: &[u8], from_byte: usize)

// If partial_tab_remaining > 0:
//   emit that many space characters into string_content
//   from_byte += 1        ← skip the raw tab byte that was split
//   partial_tab_remaining = 0
//
// Append line[from_byte..] as raw bytes into string_content.
```

This is the only place virtual spaces are materialised. The inline parser and renderer see clean content with no raw tab artefacts.

---

### Worked example — spec §2.2 example 5

Input: `- foo\n\n\t\tbar\n`  
Expected: `<pre><code>  bar\n</code></pre>` (two spaces before `bar`)

Line 3 is `\t\tbar`. Column trace:

| byte | col before | col after | action |
|---|---|---|---|
| `[0] \t` | 0 | 2 | Item continuation needs `padding=2`. Tab width=4, 4>2. **Split:** `partial_tab_remaining=2`, `byte_offset` stays 0. |
| `[0] \t` cont. | 2 | 4 | Code block needs 4 cols. Phase A drains partial (2 cols). `byte_offset→1`. |
| `[1] \t` | 4 | 6 | Code block has 2 cols left. Tab width=4, 4>2. **Split:** `partial_tab_remaining=2`, `byte_offset` stays 1. |
| `[1] \t` content | — | — | `append_text`: emit `"  "` (2 spaces from partial), skip byte 1, append `bar`. `string_content = "  bar"`. |

The two spaces in the output come entirely from the second tab being split — 2 of its 4 virtual columns went to satisfying the code block's indent requirement, and the remaining 2 became content spaces.

---

### Key invariant to document

`consume_columns` is the single authoritative site for partial-tab decisions. No other function writes `partial_tab_remaining`. `append_text` is the single site that reads and clears it. This keeps the tab-splitting logic contained and auditable.

## ▸ Prompt — 2026-06-02 16:33

> Remember the project language is cpp, rewrite documentation methods signatures to align with cpp constructs
>
> Also add a section with the code structure following this blueprint
>
> ```
> markdow_parser/
> ├── CMakeLists.txt          # Top-level build config
> ├── Makefile                # Optional, sometimes wraps CMake
> ├── README.md
> ├── .gitignore
> │
> ├── include/                # Public headers (.hpp)
> │   └── markdown_parser/
> │       ├── SpineHandler.hpp
> │       └── ...
> │
> ├── src/                    # Implementation files (.cpp)
> │   ├── main.cpp
> │   ├── SpineHandler.cpp
> │   └── ...
> │
> ├── tests/                  # Test files
> │   ├── CMakeLists.txt
> │
> ├── libs/ or third_party/   # External dependencies  
> │   │-- nlohmann/     json    
> │   └── googletest/   test   
> │
> ├── build/                  # Generated build artifacts (git-ignored)
> │
> └── docs/                   # Documentation
> ```

Key C++ decisions reflected throughout:

`std::string_view` for `ScannedLine::content` and all read-only line parameters — no copies, borrows directly from the input buffer. The caller (SpineHandler's `processLine`) owns the line lifetime.

`std::variant` for `BlockData` and `InlineData` instead of a union, using `std::monostate` for the no-payload types (Document, Paragraph, ThematicBreak). This gives type-safe access via `std::get` or `std::visit` and zero overhead for the common cases.

`std::optional<std::string>` for nullable string fields like `LinkData::title` and `InlineParser::processEmphasis`'s `stack_bottom` — cleaner than sentinel values or raw pointers.

`std::unique_ptr<BlockNode>` for `document_` in SpineHandler — the handler owns the root. `releaseDocument()` transfers ownership to the caller after `finalize()`. All other `BlockNode*` in the spine are non-owning raw pointers into the tree owned by `document_`.

`partial_tab_remaining_` and `current_col_` are private members with trailing underscore following the standard private-member naming convention, visually distinct from parameters and locals in the method bodies.

The `consumeColumns` and `appendText` bodies are written as real C++ with proper casting (`unsigned char` for byte comparisons to avoid signed/unsigned issues with `char`), which matches what the actual implementation will need.

## ▸ Prompt — 2026-06-02 16:45

> When a node is removed form the spinefinalized from the spine

You seem to have cut off mid-thought. Do you mean:

**When a node is finalized and removed from the spine, what happens to it?**

If so — a few things to nail down for the doc:

1. **Inline parsing trigger** — leaf blocks (`Paragraph`, `Heading`, `CodeBlock` type fenced, `HtmlBlock`) have their `string_content` passed to `InlineParser::parse()` at this moment

## ▸ Prompt — 2026-06-02 16:47

> When a node is removed form the spine (finalized) it should be added as a child to the parent node? is this specified in the document?

Looking at the current document — no, it is not explicitly specified. `closeBlock` mentions popping the node off the spine and setting `is_open = false`, but the tree attachment step is left implicit.

However, the tree attachment actually happens at **open time**, not close time. When `openBlock` is called, the new node is immediately appended as a child of the current spine tip (which becomes its parent). By the time `closeBlock` fires, the node is already in the right place in the tree — closing just marks it as finalized and triggers inline parsing if it's a leaf.

So the doc needs two clarifications:

`openBlock` — explicitly state that it appends the new node as a child of `tip()` before pushing it onto the spine. The parent pointer, `first_child`/`last_child` on the parent, and `prev` sibling on the new node are all wired here.

`closeBlock` — explicitly state that it does **not** move the node in the tree. The node stays where it is as a child of its parent. Closing only: pops from spine, sets `is_open = false`, records `end_line`, and triggers `InlineParser::parse()` for leaf types.

Want me to update the `SpineHandler` tab in the reference doc with these semantics made explicit in the method comments?

## ▸ Prompt — 2026-06-02 16:48

> Yes

One ownership detail worth noting explicitly since it's easy to get wrong in C++: the spine holds non-owning raw pointers, while the tree owns the nodes through the parent→child linked list. The only `unique_ptr` is `document_` on the `SpineHandler` itself — destroying it recursively destroys the entire tree. This means `BlockNode` needs a destructor that deletes its `first_child` chain, or alternatively all nodes are allocated from a flat arena owned by the `SpineHandler`, which avoids deep-recursion destruction on very large documents.

That memory strategy (pointer-per-node vs arena) is worth deciding before writing `BlockNode`'s destructor — worth adding to the doc when you're ready to nail that down.

## ▸ Prompt — 2026-06-02 16:53

> In life cycle summary you have openBlock() -> processLine() -> closeBlock()
>
> Does It align with phase 1 step, also inline parsing only happens in phase 2

Good catches, both of them.

**On the lifecycle order:** `openBlock` and `closeBlock` are not at the same level as `processLine` — they are called *from within* `processLine`. The lifecycle should read as `processLine` owning the whole sequence, with open/close as subordinate operations that happen during step 2 and the unmatched-close phase respectively.

**On inline parsing:** you're right, it does not belong in `closeBlock`. Phase 1 only builds the block tree and accumulates `string_content`. `InlineParser::parse()` runs in phase 2, which is a separate pass over the finalised tree after all lines have been processed and `finalize()` has been called. Triggering it from `closeBlock` would interleave the two phases and mean inline parsing happens mid-document before all link reference definitions have been collected — which breaks ref-links that reference a definition further down the file.

The corrected model:

- `closeBlock` — pops from spine, sets `is_open = false`, records `end_line`. Nothing more.
- `finalize()` — closes all remaining open blocks, then walks the completed tree and calls `InlineParser::parse()` on each leaf node, with the fully-populated `ref_map_` available.

Let me update the widget:
Two things that fell out of fixing this that are worth keeping in mind for implementation:

`parseInlineContent` recurses into containers but not into leaf children — a `Paragraph` inside a `List` inside a `BlockQuote` is reached by recursing through the containers, not by iterating a flat list. This is why the recursion checks `is_leaf` first rather than just checking `first_child`.

`ThematicBreak` is intentionally absent from the `is_leaf` check — it has no `string_content` and no inline children. Same for `Document`, `BlockQuote`, `List`, and `Item`. Those are all container or empty structural nodes that `parseInlineContent` recurses through without calling the inline parser.

## ▸ Prompt — 2026-06-02 16:57

> Remember the lazy continuation, step1WalkSpine() does not close any block, its next step in phase 1 job to do so, if no block open symbol is registered
>
> for each line:
>   ┌─ Step 1: walk open block stack top-down
>   │    for each open block: does the line satisfy its continuation condition?
>   │    → record matched/unmatched prefix (do NOT close unmatched yet)
>   │
>   ├─ Step 2: look for new block starts in the remaining line
>   │    if new block found:
>   │      → close all unmatched blocks from Step 1
>   │      → open new block as child of deepest matched block
>   │    if NO new block found AND innermost open block is paragraph:
>   │      → lazy continuation: append text, keep unmatched blocks open
>   │    if NO new block found AND NOT lazy:
>   │      → close unmatched blocks, text falls into last open block
>   │
>   └─ Step 3: append remaining text to deepest open block
>        special case: setext underline → retroactively promote paragraph to heading
>        special case: paragraph close → scan for link reference definitions
> This three-step loop, with the deferred close in Step 2, is precisely what makes the algorithm handle lazy continuation without backtracking.

Correct. The closing decision is deferred to step 2 because only at that point do we know whether lazy continuation applies — and that determination requires knowing both that no new block opened *and* that the tip is a paragraph. Step 1 only classifies blocks as matched or unmatched; it has no authority to close anything.

Also worth noting: step 3 was missing from the lifecycle summary entirely. The setext promotion and link reference definition scanning both happen there, after the open/close decisions are settled.

Let me update the widget:
The `SpineMatchResult` struct is worth highlighting as a deliberate design choice: rather than returning just the deepest matched index (which would force step 2 to re-derive where unmatched blocks start), it carries both `deepest_matched` and `first_unmatched` out of step 1. Step 2 can then call `closeUnmatched(match.first_unmatched)` directly without re-walking the spine. Both fields are set in a single pass, so there's no redundant work.

`incorporatesLazyContinuation` now takes the `SpineMatchResult` as well — it needs to know whether there are unmatched blocks above the tip (i.e. `first_unmatched < spine_.size()`), since lazy continuation only applies when at least one ancestor failed its continuation check but the tip paragraph is absorbing the line anyway.

## ▸ Prompt — 2026-06-02 17:06

> Create a .md file comprehensive of what we have specified in this conversation
>
> * project structure
> * block & inline nodes
> * continuation/open/close blocks rules
> * pre scanner structure,  methods  and behavior
> * spine handler (phase 1) structure, methods  and behavior
> * inline parser (phase 2) structure, methods and behavior
> * tab algorithm
> * data flow through phases
>
> Add detailed comments on methods signatures/implementation

The document covers all eight sections. A few things worth calling out:

The `BlockNode` destruction note at the very end is a deliberate open question left for you to decide — individual `new`/`delete` with a recursive destructor is the simplest path but risks stack overflow on deeply nested documents; a flat arena allocated on `SpineHandler` avoids that entirely and also makes `closeBlock`/`openBlock` trivially fast.

The `InlineParser::parse()` method body is written out in full while the remaining inline methods are signature-only with detailed doc comments — that matches what was defined in this conversation (signatures only, no implementations for inline parsing internals).

The tab worked example in §7.3 includes the full column trace table from the earlier session so the reasoning is self-contained without needing to reference back to the conversation.

