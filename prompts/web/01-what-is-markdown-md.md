# What is Markdown (.md) and whe

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `00bdedc7-efbf-4d21-8d76-377d1ae5bb26`  
**Created:** 2026-06-02  
**Messages:** 21 (10 prompts)

**Related commits:** Research and design only — no code commit; this shaped the two-phase spine design.

---

## ▸ Prompt — 2026-06-02 08:31

> What is Markdown (.md) and where it stands compared to other markdown languages like xml

## What is Markdown?

Markdown is a **lightweight markup language** created by John Gruber in 2004, designed to be readable as plain text while also converting cleanly to HTML. The core philosophy: formatting should be intuitive enough to understand even without rendering.

```md
# Heading
**bold**, *italic*, [link](url), `code`
```

---

## Markdown vs. Other Markup Languages

The term "markup language" covers a broad family. Here's how Markdown fits in:

| Language | Purpose | Readability | Complexity |
|---|---|---|---|
| **Markdown** | Lightweight docs, notes, web content | Very high | Very low |
| **HTML** | Web page structure | Low (tag-heavy) | Medium |
| **XML** | Data interchange, config | Low | Medium–High |
| **LaTeX** | Academic/scientific typesetting | Low | High |
| **reStructuredText** | Python docs (Sphinx) | Medium | Medium |
| **AsciiDoc** | Technical documentation | Medium | Medium |

---

## How Markdown Differs from XML Specifically

These two are often compared but serve very different purposes:

**XML** is a *data* markup language — it structures and transports information between systems. It's verbose, strict, and machine-oriented:
```xml
<person>
  <name>Alice</name>
  <age>30</age>
</person>
```

**Markdown** is a *presentation* markup language — it formats human-readable content. It's terse and author-oriented:
```md
## Alice
Age: 30
```

Key differences:

- **Schema**: XML can enforce strict schemas (via XSD/DTD); Markdown has no schema
- **Extensibility**: XML is infinitely extensible for any data domain; Markdown is fixed to text formatting
- **Machine use**: XML is widely used in APIs, configs, databases; Markdown is not
- **Human use**: Markdown is far easier to write and read; XML is tedious for prose
- **Output**: Markdown renders to HTML/PDF; XML describes structure and needs separate transforms (like XSLT) to render

---

## Where Markdown Stands Today

Markdown occupies a unique niche: it's the go-to for **human-written content** (README files, documentation, blogs, notes, chat messages). It's not trying to compete with XML — they operate at different layers. If XML is a filing cabinet for data, Markdown is a notepad for thoughts.

Popular flavors extend the original spec: **CommonMark** (standardized), **GitHub Flavored Markdown (GFM)** (tables, task lists), and **MDX** (Markdown + JSX for React).

## ▸ Prompt — 2026-06-02 09:34

> Is .md a context sensitive language or is it context free one? If it is not context-free explain briefly the constructs that make .md context sensitive

## Is Markdown Context-Free or Context-Sensitive?

**Markdown is neither purely context-free nor formally context-sensitive** — it occupies an awkward middle ground that makes it hard to classify under the Chomsky hierarchy. Most Markdown parsers are implemented *as if* it were context-sensitive, because several constructs require surrounding context to parse correctly.

---

## Why It's Not Context-Free

A context-free grammar (CFG) means every production rule can fire independently of surrounding symbols. Markdown violates this in several places:

### 1. Indentation-Dependent Meaning
The same text means different things depending on its indentation level:

```md
- item
    - nested item       ← child of above (4-space indent)
- item
    continuation line   ← continuation of paragraph, NOT a list
```
The parser must track *how much* indentation preceded the current token — context from earlier in the line.

---

### 2. Setext Headings (Lookahead / Lookbehind)
```md
This is a heading
=================
```
The first line is only *retroactively* classified as a heading once the `===` line is seen. A CFG cannot look ahead or behind — the meaning of line 1 depends on what comes after it.

---

### 3. Link Reference Definitions
```md
[link text][ref]

[ref]: https://example.com
```
The `[ref]` label on line 1 is only resolvable if a matching definition exists *anywhere* in the document — before or after. This requires a global symbol table, which no CFG can maintain.

---

### 4. Emphasis Nesting Rules (The Hardest One)
The CommonMark spec dedicates ~30 rules to asterisk/underscore parsing. Whether `*` opens or closes emphasis depends on what characters surround it:

```md
*foo bar*       ← valid emphasis
*foo bar *      ← NOT emphasis (space before closing *)
foo*bar*        ← emphasis only on "bar"
```
The open/close decision depends on Unicode categories of adjacent characters — pure context sensitivity.

---

### 5. HTML Block Interruption
```md
<div>

Some text

</div>
```
Whether a blank line terminates an HTML block depends on what *type* of HTML block was opened earlier (CommonMark defines 7 types). The blank line token has different meanings in different parser states.

---

## Summary

| Property | Markdown |
|---|---|
| Regular (Type 3) | ✗ — nesting rules exceed regex |
| Context-Free (Type 2) | ✗ — indentation, lookahead, global refs |
| Context-Sensitive (Type 1) | ≈ Yes, informally |
| Formally Specified | ✗ — CommonMark is the closest attempt |

In practice, Markdown is best described as a **stateful, scannerless language** parsed top-to-bottom with a mutable state machine — which is why every implementation has subtly different behavior, and why a single formal grammar for it has never been agreed upon.

## ▸ Prompt — 2026-06-02 09:44

> Explain briefly what is CommonMark

## CommonMark

CommonMark is a **standardized, unambiguous specification for Markdown**, launched in 2014 by John MacFarlane (author of Pandoc) and others, in response to a long-standing problem: every Markdown implementation behaved differently.

---

## The Problem It Solves

Gruber's original 2004 Markdown spec was informal and full of edge cases it didn't address. This led to dozens of incompatible implementations — GitHub, Reddit, Stack Overflow, and others all parsed the same Markdown differently. A document rendered one way on one platform and differently on another.

---

## What CommonMark Provides

- **A formal, unambiguous specification** — ~100 pages covering every edge case with explicit rules and ~600 example tests
- **A reference implementation** (`cmark` in C) that other parsers can be tested against
- **A validation suite** — so any parser can prove CommonMark compliance
- **Predictable parsing** — the same `.md` file renders identically everywhere that supports it

---

## What It Doesn't Do

CommonMark deliberately stays **close to the original Markdown feature set** — it does not add tables, footnotes, or task lists. Those are left to extensions (e.g. GitHub Flavored Markdown builds *on top of* CommonMark and adds such features).

---

## In Short

CommonMark is to Markdown what the **ECMAScript spec is to JavaScript** — not a new language, but the authoritative rulebook that implementations should agree on.

## ▸ Prompt — 2026-06-02 09:49

> Which is the parsing strategy used in cmark reference implementation that CommonMark provides?

## Parsing Strategy in `cmark`

`cmark` uses a **two-phase parsing strategy**, combining a top-down block parser with a subsequent inline parser. This is sometimes called a **"lazy continuation" block parser with a separate inline pass**.

---

## Phase 1 — Block Structure (Top-Down, Line by Line)

The first pass reads the document **one line at a time**, building a tree of block-level elements:

```
Document
├── Paragraph
├── BlockQuote
│   └── Paragraph
├── List
│   ├── ListItem
│   └── ListItem
└── FencedCode
```

Each line is fed into an **open block stack**. The parser tries to continue each open block with the new line, or closes blocks and opens new ones. This is where indentation and context sensitivity is handled — the stack *is* the context.

Key traits:
- **Single left-to-right scan** per line — no backtracking
- Blocks are opened and closed greedily
- Lazy continuation rules (e.g. a paragraph can "swallow" lines that would otherwise look like new blocks) are resolved here

---

## Phase 2 — Inline Parsing (Left-to-Right with a Delimiter Stack)

Once the block tree is built, each leaf node's raw text content is parsed for inline elements — emphasis, links, code spans, etc.

This uses a **delimiter stack** (a linked list of potential openers and closers):

1. Scan left to right, pushing `*`, `_`, `[` etc. onto the stack as *potential* openers
2. When a potential closer is found, scan the stack backwards for a matching opener
3. If found, wrap the content between them into an emphasis/link node
4. If not, the delimiter is treated as literal text

This is what handles the famously complex emphasis rules — the delimiter stack encodes the context needed to resolve ambiguous `*` and `_` characters.

---

## Why This Two-Phase Design?

| Concern | Solved By |
|---|---|
| Indentation & block nesting | Open block stack in Phase 1 |
| Setext headings (retroactive) | Line buffering in Phase 1 |
| Link reference definitions | Collected in Phase 1, resolved in Phase 2 |
| Emphasis ambiguity | Delimiter stack in Phase 2 |
| No backtracking needed | Each phase is a single linear pass |

---

## In Short

`cmark` is essentially a **stateful line-by-line automaton** for blocks, followed by a **delimiter-stack inline parser** — two linear passes that together handle all the context-sensitive constructs without ever needing a formal grammar or backtracking. This is why it's fast and predictable, and why it became the reference implementation.

## ▸ Prompt — 2026-06-02 10:08

> Explain in details  which are the steps performed in each iteration of the phase 1 and how lazy continuation is handled? report the fonts you used

## Phase 1 — Detailed Iteration Steps in `cmark`

The primary sources for this answer are:

- **The CommonMark spec** — [spec.commonmark.org](https://spec.commonmark.org)
- **The `cmark` source code** — [github.com/commonmark/cmark](https://github.com/commonmark/cmark), primarily `blocks.c`
- **John MacFarlane's notes** on the parsing strategy, referenced in the spec appendix: [spec.commonmark.org/0.31.2/#appendix-a-parsing-strategy](https://spec.commonmark.org/0.31.2/#appendix-a-parsing-strategy)

Let me fetch the spec appendix directly to give you the most accurate account.
Now I have everything needed from authoritative sources. Here is the detailed answer.

---

## Phase 1 — Per-Line Iteration Steps in Detail

**Primary sources:**
- CommonMark Spec v0.31.2, *Appendix A: A Parsing Strategy* — [spec.commonmark.org/0.31.2](https://spec.commonmark.org/0.31.2/)
- GFM Spec Appendix (mirrors CommonMark's strategy) — [github.github.com/gfm](https://github.github.com/gfm/)
- `cmark` source code — [github.com/commonmark/cmark](https://github.com/commonmark/cmark), `blocks.c`

---

### The Tree and the Open Block Stack

Before the per-line loop begins, the parser initializes a tree with a single open root `document` node. The block parser operates line-by-line, maintaining a stack of currently open blocks (called the "spine"), handling both container blocks (like blockquotes) and leaf blocks (like paragraphs).

Each line that is processed has an effect on this tree. The line is analyzed and, depending on its contents, the document may be altered in one or more of the following ways: one or more open blocks may be closed; one or more new blocks may be created as children of the last open block; or text may be added to the last (deepest) open block remaining on the tree. Once a line has been incorporated into the tree in this way, it can be discarded, so input can be read in a stream.

---

### The Three Steps of Each Iteration

For every line of input, the parser executes exactly these three steps in order:

---

#### Step 1 — Walk the Open Block Stack and Try Continuations

First we iterate through the open blocks, starting with the root document, and descending through last children down to the last open block. Each block imposes a condition that the line must satisfy if the block is to remain open. For example, a block quote requires a `>` character. A paragraph requires a non-blank line. In this phase we may match all or just some of the open blocks.

Each block type has its own continuation rule:

| Block type | Continuation condition |
|---|---|
| `document` | always continues |
| `block_quote` | line starts with `>` |
| `list_item` | line is sufficiently indented |
| `fenced_code` | line is not the closing fence |
| `paragraph` | line is non-blank (+ lazy continuation — see below) |
| `indented_code` | line is blank or indented ≥ 4 spaces |
| `html_block` | depends on the HTML block type (1–7) |

After walking the stack, the parser knows which prefix of open blocks were **matched** (their continuation condition was satisfied) and which were **unmatched**. Crucially, unmatched blocks are **not yet closed** at this point — that must wait until Step 2, because of lazy continuation.

---

#### Step 2 — Look for New Block Starts

Next, after consuming the continuation markers for existing blocks, we look for new block starts (e.g. `>` for a block quote). If we encounter a new block start, we close any blocks unmatched in step 1 before creating the new block as a child of the last matched block.

The parser scans the remainder of the line (after stripping continuation markers consumed in Step 1) for block-start signals, tried in this priority order per the spec:

1. Block quote marker `>`
2. ATX heading `#`
3. Fenced code block ` ``` ` or `~~~`
4. HTML block start
5. Setext heading underline `===` or `---` *(only valid if last open block is a paragraph)*
6. Thematic break `***`, `---`, `___`
7. List item marker (`-`, `*`, `+`, or `1.`)
8. Indented code block (4 spaces)

If a new block start is detected, all **unmatched** open blocks from Step 1 are closed first, then the new block is opened as a child of the deepest matched block.

---

#### Step 3 — Incorporate Remaining Text into the Last Open Block

Finally, we look at the remainder of the line (after block markers like `>`, list markers, and indentation have been consumed). This is text that can be incorporated into the last open block (a paragraph, code block, heading, or raw HTML).

Two special cases happen here:

- Setext headings are formed when we see a line of a paragraph that is a setext heading underline. This retroactively reclassifies the paragraph accumulated so far as a heading — the only "look-behind" in Phase 1.
- Reference link definitions are detected when a paragraph is closed; the accumulated text lines are parsed to see if they begin with one or more reference link definitions. Any remainder becomes a normal paragraph.

---

### How Lazy Continuation Is Handled

Lazy continuation is the most subtle part of the algorithm, and the reason unmatched blocks cannot be closed eagerly in Step 1.

The spec defines a **lazy continuation line** as a line that does *not* supply the required continuation markers for its enclosing container blocks (e.g. a line without `>` inside a block quote), yet is still treated as continuing the innermost open paragraph rather than closing the container.

If a string of lines constitute a block quote with contents, then the result of deleting the initial block quote marker from one or more lines in which the next non-whitespace character after the block quote marker is paragraph continuation text is a block quote with the same contents.

In algorithmic terms, the parser cannot know at the end of Step 1 whether an unmatched block should be closed, because the unmatched line might still be a lazy continuation of the innermost open paragraph. So it **defers closing** until Step 2, at which point it checks: if the line fails to start a new block, and the innermost open block is a paragraph, the line is a lazy continuation and gets appended to that paragraph's text — the unmatched containers remain open. Only if a new block *is* found in Step 2 are the unmatched containers actually closed.

The walkthrough from the spec illustrates this precisely:

```
> Lorem ipsum dolor    ← opens block_quote → paragraph
sit amet.              ← NO '>' marker, but lazy continuation of paragraph
> - Qui *quodsi*       ← new block start: closes paragraph, opens list
> - aliquando id       ← new list_item
```

The line `sit amet.` is a "lazy continuation" of the open paragraph, so it gets added to the paragraph's text without requiring a `>` marker.

---

### Summary of One Full Iteration

```
for each line:
  ┌─ Step 1: walk open block stack top-down
  │    for each open block: does the line satisfy its continuation condition?
  │    → record matched/unmatched prefix (do NOT close unmatched yet)
  │
  ├─ Step 2: look for new block starts in the remaining line
  │    if new block found:
  │      → close all unmatched blocks from Step 1
  │      → open new block as child of deepest matched block
  │    if NO new block found AND innermost open block is paragraph:
  │      → lazy continuation: append text, keep unmatched blocks open
  │    if NO new block found AND NOT lazy:
  │      → close unmatched blocks, text falls into last open block
  │
  └─ Step 3: append remaining text to deepest open block
       special case: setext underline → retroactively promote paragraph to heading
       special case: paragraph close → scan for link reference definitions
```

This three-step loop, with the deferred close in Step 2, is precisely what makes the algorithm handle lazy continuation without backtracking.

## ▸ Prompt — 2026-06-02 10:25

> Does the line need to be pre-scanned before going into phase 1 or it can go raw?

Good question. The answer is: **yes, a small amount of pre-processing happens before the three-step loop**, but it is minimal and well-defined — not a full scan.
I now have the `cmark` source code details directly. Let me also fetch the actual `blocks.c` to confirm the exact pre-processing steps.
I now have everything needed directly from `blocks.c` and the spec. Here is the precise answer.

---

## Short Answer

**The line is not passed raw.** A small, well-defined pre-processing sequence runs at the very top of `S_process_line()` — the function that drives Phase 1 — before the three-step block loop begins. It has exactly three responsibilities: encoding sanitisation, newline normalisation, and parser-state reset. Notably, **tabs are not expanded globally** — that was deliberately removed in a past version of the spec.

---

## What Happens Before the Three-Step Loop

### 1. Optional UTF-8 Validation / Sanitisation

At the start of `S_process_line`, if the `CMARK_OPT_VALIDATE_UTF8` flag is set, `cmark_utf8proc_check` is called on the raw buffer before it is placed into `parser->curline`; otherwise the bytes are copied as-is with `cmark_strbuf_put`.

This step replaces any invalid UTF-8 byte sequences with the Unicode replacement character `U+FFFD`, and also replaces the forbidden null byte `U+0000` with `U+FFFD` for security reasons. It is the **only** transformation that touches character values, and it is optional (off by default).

### 2. Newline Normalisation

After the buffer is copied, `cmark` checks if the line ends with a newline character; if it does not, it appends one with `cmark_strbuf_putc`.

This guarantees that every line the three-step loop sees ends with `\n`, eliminating the need for every block-continuation check to separately handle the end-of-file edge case. Carriage returns (`\r`) from `\r\n` line endings are handled at the line-splitting level above `S_process_line`, so by the time a line arrives here it is already `\n`-terminated or bare.

This is consistent with the spec, which says: a line ending is a newline (`U+000A`), a carriage return (`U+000D`) not followed by a newline, or a carriage return and a following newline. All three forms are collapsed into a single `\n` before the loop runs.

### 3. Parser State Reset for the New Line

After the newline is ensured, the parser resets its per-line cursor fields: `offset`, `column`, `first_nonspace`, `first_nonspace_column`, `thematic_break_kill_pos`, `indent`, `blank`, and `partially_consumed_tab` are all zeroed or set to false.

These fields are the virtual cursor that the three-step loop advances as it consumes continuation markers and indentation. Resetting them here means the loop always starts at column 0 with no partial state from the previous line.

### What Is Deliberately NOT Done: Tab Expansion

This is an important design point. An earlier version of the spec had a global preprocessing step that expanded all tabs to spaces. In version 0.21, the spec changed the handling of tabs: instead of having a preprocessing step where tabs are converted to spaces, tabs are now handled directly in the parser.

Tabs are instead virtualised on-the-fly using the `partially_consumed_tab` flag and the `column` counter. When the loop advances past a tab, it calculates how many virtual spaces it occupies to the next 4-column tab stop, and may "split" the tab — consuming part of its virtual width for one block's indentation and leaving the remainder for the next. This is why `partially_consumed_tab` is a first-class field on the parser state.

---

## Complete Picture of `S_process_line` Entry

```
raw bytes from caller
        │
        ▼
[1] UTF-8 sanitise (optional)     ← replaces U+0000 and invalid sequences with U+FFFD
        │
        ▼
[2] Newline normalisation          ← appends '\n' if line has no line-end character
        │
        ▼
[3] Per-line state reset           ← zeros offset, column, indent, blank, partially_consumed_tab …
        │
        ▼
  ┌─────────────────────────┐
  │  THREE-STEP BLOCK LOOP  │   ← line is consumed here, tabs handled lazily in-place
  └─────────────────────────┘
```

So the answer to your question is: the line goes in **almost raw**, but with three lightweight, non-structural transformations applied upfront. No tokenisation, no tab expansion, no lookahead — all of that is left to the loop itself.

## ▸ Prompt — 2026-06-02 12:04

> I know markdown has links definitions, how are they handled by the cmark parser? Also can they be declared everywhere?

Great question — link reference definitions have some of the most interesting handling in the whole parser because they span both phases. Let me fetch the exact details from the spec and source.
I have enough from the spec and source to give a precise, well-sourced answer.

---

## How Link Reference Definitions Are Handled in `cmark`

They are uniquely special: they are the **only construct that is not a node in the AST**. Instead they populate a global lookup table — the `refmap` — that lives on the parser and is consumed later by Phase 2. Here is exactly how, step by step.

---

### What a Link Reference Definition Looks Like

```md
[foo]: /url "optional title"

[foo]          ← shortcut reference link
[bar][foo]     ← full reference link
```

A link reference definition does not correspond to a structural element of a document. Instead, it defines a label which can be used in reference links and reference-style images elsewhere in the document.

---

### Phase 1 — Buffered Inside a Paragraph, Extracted on Close

During the line-by-line block loop, a link reference definition is **not recognised as its own block type**. It has no dedicated block-start signal in Step 2. Instead, its lines are accumulated inside a **paragraph** node, just like ordinary text — because the parser cannot tell the difference until it sees the whole structure.

The extraction happens at a very specific moment: reference link definitions are detected when a paragraph is closed; the accumulated text lines are parsed to see if they begin with one or more reference link definitions. Any remainder becomes a normal paragraph.

In `cmark`'s `blocks.c`, this is done inside the `S_finalize` function when it handles a `CMARK_NODE_PARAGRAPH`. The accumulated content buffer is handed to `cmark_parse_reference_inline` (from `inlines.c`), which attempts to parse as many consecutive reference definitions off the front of the text as possible. Each successfully parsed definition is inserted into `parser->refmap`, and any leftover text that is not a definition stays as the paragraph's content. If all the text was consumed by definitions, the paragraph node is removed from the tree entirely — it leaves no trace in the AST.

The `refmap` itself is a simple hash map keyed on the **normalised** label string (Unicode case-folded, whitespace collapsed), mapping to `{url, title}` pairs.

---

### Phase 2 — Resolved During Inline Parsing

The second step of parsing requires information about link reference definitions that will be available only at the end of the first step. This is precisely why Phase 2 cannot begin until Phase 1 is fully complete — the entire `refmap` must be populated before any inline parsing starts.

When the inline parser encounters `[label][ref]` or `[ref]`, it looks up the normalised label in `parser->refmap`. If a match is found, it constructs a link node with the stored URL and title. If not found, the brackets are emitted as literal text.

---

### Can They Be Declared Everywhere?

**Almost everywhere, but with three important restrictions.**

#### ✅ Before or after the links that use them
Link reference definitions can come either before or after the links that use them. This is the forward-reference capability — a definition at the bottom of the file resolves a link at the top, because Phase 1 collects all definitions before Phase 2 resolves any of them.

#### ✅ Inside container blocks (blockquotes, list items)
Link reference definitions can occur inside block containers, like lists and block quotations. They affect the entire document, not just the container in which they are defined.

So this works, and `[foo]` resolves document-wide:

```md
> [foo]: /url

[foo]   ← resolves correctly, outside the blockquote
```

#### ❌ Cannot interrupt a paragraph
A link reference definition cannot interrupt a paragraph. However, it can directly follow other block elements, such as headings and thematic breaks, and it need not be followed by a blank line.

If text precedes it without a blank line, the definition is absorbed into the paragraph as plain text instead:

```md
Some text
[foo]: /url    ← NOT a definition; treated as paragraph continuation
```

#### ❌ Cannot appear inside code blocks, code spans, or HTML blocks
These block types treat their content as literal and never call `cmark_parse_reference_inline` on finalization, so anything that looks like a definition inside them is ignored.

#### ❌ Duplicate labels: first definition wins
Whether something is a link reference definition is independent of whether the link reference it defines is used in the document. For duplicates, `cmark` inserts into the `refmap` only if the label is not already present — so the first definition in document order takes precedence, and later ones are silently discarded.

---

### Full Lifecycle Summary

```
Phase 1 (line by line)
  │
  ├─ Lines accumulate in a paragraph node
  │
  └─ On paragraph close (S_finalize):
       └─ cmark_parse_reference_inline() called on content
            ├─ Parses [label]: url "title" from the front
            ├─ Inserts each into parser->refmap (first-wins on duplicates)
            └─ Leftover text stays as paragraph; if none, node is removed

        ↓ (after ALL of Phase 1 is complete)

Phase 2 (inline parsing, parallelisable)
  │
  └─ When [text][label] or [label] encountered:
       └─ Lookup normalised label in parser->refmap
            ├─ Hit  → emit link node with stored url/title
            └─ Miss → emit literal bracket text
```

This design — buffering in paragraphs, extracting on close, resolving in a second pass — is exactly what makes forward references possible and what ties the two-phase architecture together.

Great question — link reference definitions have one of the more interesting lifetimes in the whole parser. Let me check the spec and source directly.
Good. I have all the authoritative material I need. Here is the complete answer.

---

## Link Reference Definitions in cmark

### What They Are

A link reference definition consists of a link label, optionally preceded by up to three spaces of indentation, followed by a colon (`:`), optional spaces or tabs (including up to one line ending), a link destination, optional spaces or tabs (including up to one line ending), and an optional link title, which if it is present must be separated from the link destination by spaces or tabs. No further character may occur.

So the full syntax spans up to three lines:

```md
[label]: https://example.com "Optional Title"
```

or multi-line:

```md
[label]: https://example.com
         "Title on second line"
```

A link reference definition does not correspond to a structural element of a document. Instead, it defines a label which can be used in reference links and reference-style images elsewhere in the document. This means it produces **no output** — it is invisible in the rendered HTML.

---

### How cmark Handles Them — The Two-Phase Lifecycle

Link reference definitions have one of the most unusual lifetimes in the parser: they are **discovered in Phase 1** but only **consumed in Phase 2**, and this split is by design.

#### Phase 1 — Discovery on Paragraph Close

Link reference definitions are not parsed as a block type of their own during the line-by-line loop. Instead, they initially accumulate as ordinary **paragraph text**. Reference link definitions are detected when a paragraph is closed; the accumulated text lines are parsed to see if they begin with one or more reference link definitions.

This is the key mechanism: when Phase 1 closes a paragraph node (because a blank line, a new block start, or end-of-document is encountered), it takes all the raw text lines buffered in that paragraph and runs a special sub-scanner over them. If those lines parse as one or more valid link reference definitions, the definitions are stripped from the paragraph and registered into a document-wide **reference map** (`cmark_map` / `refmap`). Any leftover text that did not form a definition is kept as a real paragraph node.

This is why a file that contains *only* link reference definitions renders as empty: the paragraph is closed, all its lines are consumed as definitions, nothing remains, and the node is discarded.

#### Phase 2 — Resolution During Inline Parsing

The second step of parsing requires information about link reference definitions that will be available only at the end of the first step.

This is the reason the two phases are split. `cmark_parse_inlines` receives the completed `refmap` and passes it into the inline subject, so that when a reference link like `[foo]` or `[foo][bar]` is encountered during inline parsing, the label is looked up in the map to retrieve the stored destination and title.

Because the entire refmap is built before Phase 2 begins, a reference link can resolve a definition that appears **anywhere** in the document — before or after the usage site — with no backtracking needed.

---

### Where Can They Be Declared?

#### ✅ Anywhere in the document flow — before or after usage

Link reference definitions can come either before or after the links that use them. This works because the refmap is fully populated at the end of Phase 1, before any inline resolution happens.

#### ✅ Inside container blocks

Link reference definitions can occur inside block containers, like lists and block quotations. They affect the entire document, not just the container in which they are defined.

So this is valid and `[foo]` resolves globally:

```md
> [foo]: /url

[foo]
```

#### ✅ Independent of usage

Whether something is a link reference definition is independent of whether the link reference it defines is used in the document. An unused definition is silently discarded with no output and no error.

#### ❌ Inside fenced or indented code blocks

Definitions inside code blocks are **literal content**, not parsed as Markdown structure at all. The block parser never closes a paragraph inside a code block, so the definition scanner never runs on that text.

#### ❌ With more than 3 spaces of indentation at the label

The label may be optionally preceded by up to three spaces of indentation. Four or more spaces triggers an indented code block instead, which swallows the text as literal content.

#### ❌ Duplicates — first definition wins

Multiple definitions for the same reference are handled consistently — the first definition takes precedence. Subsequent definitions for the same label (case-insensitively compared) are silently ignored.

---

### Summary Timeline

```
Phase 1 (line-by-line loop)
  │
  ├─ Lines accumulate in a paragraph node as raw text
  │
  └─ On paragraph CLOSE:
       sub-scanner runs over accumulated text
       → valid definitions → stripped and stored in refmap
       → leftover text     → kept as real paragraph node

                ↓ (Phase 1 complete, refmap fully populated)

Phase 2 (inline parsing of each leaf node)
  │
  └─ [foo], [foo][bar], ![img][ref] → label lookup in refmap
       → found:  emit link/image node with stored URL + title
       → not found: emit as literal text
```

The split is intentional: it allows Phase 2 to be parallelised across leaf nodes (since inline parsing of one node does not affect another), while still supporting forward references — all without any backtracking.

## ▸ Prompt — 2026-06-02 12:23

> Explain in detail cmark phase 2, show which are the steps when the inline parsing function is called, are there any preprocessing or postprocessing of the content?

Let me fetch the actual source and spec directly before answering.
I now have everything from the spec appendix and the `cmark` source. Here is the complete, detailed answer.

---

## Phase 2 — Inline Parsing in Detail

**Sources:** CommonMark Spec v0.31.2 Appendix A (*Phase 2: inline structure*); `cmark` source `src/inlines.c` (functions `cmark_parse_inlines`, `parse_inline`, `process_emphasis`, `handle_close_bracket`).

---

### Where Phase 2 Is Triggered

Once all of the input has been parsed and all open blocks are closed, the parser "walks the tree," visiting every node, and parses the raw string contents of paragraphs and headings as inlines. At this point all link reference definitions have been seen, so reference links can be resolved as it goes.

In `cmark`, the entry point is `cmark_parse_inlines()`, called once per **leaf node** that carries raw text content — paragraphs, ATX headings, setext headings, and the text portions of link labels. Code blocks, HTML blocks, and thematic breaks are never passed to Phase 2 because their content is literal.

---

### The `subject` — The Core Data Structure

Before any character is inspected, `cmark_parse_inlines()` constructs a **`subject`** struct by calling `subject_from_buf()`. This is the cursor object that all inline functions share:

```c
typedef struct subject {
    cmark_mem      *mem;
    cmark_chunk     input;        // the raw text slice (pointer + length)
    int             line;         // current source line (for source positions)
    bufsize_t       pos;          // current byte offset in input
    int             block_offset;
    int             column_offset;
    cmark_reference_map *refmap; // the completed refmap from Phase 1
    delimiter      *last_delim;  // top of the delimiter stack
    bracket        *last_bracket;// top of the bracket stack
    bufsize_t       backticks[MAXBACKTICKS + 1]; // backtick-scan cache
    bool            scanned_for_backticks;
    bool            no_link_openers;
} subject;
```

The subject carries a `cmark_reference_map *refmap`, a `delimiter *last_delim` (top of the delimiter stack), a `bracket *last_bracket`, and a backtick scan cache.

---

### Pre-processing: Right-Trim

Immediately after `subject_from_buf` is called, `cmark_chunk_rtrim` is applied to `subj.input`.

This strips trailing spaces and newlines from the entire raw content string **before** the scan loop begins. This is the only pre-processing step — it is lightweight and has a precise reason: the spec requires that final whitespace in paragraphs and headings is discarded before inline parsing, so that trailing spaces do not produce spurious hard line breaks at the end of a block.

---

### The Main Scan Loop

After the right-trim, the main loop runs:

```c
while (!is_eof(&subj) && parse_inline(parser, &subj, parent, options))
    ;
```

Each call to `parse_inline()` peeks at the **current character**, dispatches to a handler, appends the resulting inline node as a child of `parent`, and advances `subj.pos`. The loop terminates when EOF is reached or a handler returns 0.

#### The `parse_inline` Dispatch Table

Inside `parse_inline`, the current character is read with `peek_char`. If it is `0` (EOF), the function returns 0. Otherwise it dispatches via a switch statement: `\r` or `\n` → `handle_newline`; `` ` `` → `handle_backticks`; `\\` → `handle_backslash`; `&` → `handle_entity`; `<` → `handle_pointy_brace`; `*`, `_`, `'`, `"` → `handle_delim`; `-` → `handle_hyphen`; `.` → `handle_period`; `[` → push a literal `[` node and add to the delimiter stack; `]` → `handle_close_bracket`; `!` followed by `[` → push a literal `![` node and add to the delimiter stack.

For any character not in that list, the parser calls `subject_find_special_char()` to find the **next** special character and bulk-copies all the plain text between the current position and that next special character into a single `text` node. This is the fast path — runs of plain text are never processed character-by-character.

Each handler is described below.

---

### Handler Details

**`handle_newline`**
Skips over `\r`, `\r\n`, or `\n`, increments the source line counter, resets the column offset, then skips any leading spaces at the start of the next line. If there were two or more spaces immediately before the newline position, a `hardbreak` node is emitted; otherwise a `softbreak` node is emitted. This is where the "two trailing spaces = hard line break" rule is enforced.

**`handle_backticks`**
Scans forward to find a matching closing backtick run of the same length. If found, the content between the runs is normalised (interior newlines become spaces, leading/trailing spaces stripped if the content is not all-spaces) and emitted as a `code` node. If no matching closer is found, the opening backticks are emitted as literal text. The `backticks[]` cache on the subject avoids re-scanning for closers of the same length in future calls.

**`handle_backslash`**
Consumes the `\`. If followed by an ASCII punctuation character, emits it as a literal `text` node (escape). If followed by a newline, emits a `hardbreak`. Otherwise emits a literal `\` text node.

**`handle_entity`**
Scans for a valid HTML entity (`&name;`) or numeric character reference (`&#N;`, `&#xN;`). If valid, emits the decoded Unicode character as a `text` node. Otherwise emits a literal `&`.

**`handle_pointy_brace`**
Tries, in order: autolink (`<scheme:...>` or `<email>`), then raw HTML tag. If either matches, emits the appropriate node. Otherwise emits a literal `<`.

**`handle_delim` (`*`, `_`, `'`, `"`)**
This handler does **not** immediately create an emphasis node. Instead it: counts the run length, inspects the Unicode categories of the characters immediately before and after the run to determine `can_open` and `can_close` flags, creates a `text` node with the delimiter characters, and **pushes a delimiter record** onto the delimiter stack. The actual emphasis nodes are only created later in `process_emphasis`. The `'` and `"` cases apply only when the `CMARK_OPT_SMART` flag is set (smart quotes).

**`handle_hyphen` and `handle_period`**
Smart typography handlers (en-dash, em-dash, ellipsis), also only active under `CMARK_OPT_SMART`. Otherwise the characters fall through to the plain-text path.

**`[` — bracket push**
When the parser hits `[` or `![`, it inserts a text node with these symbols as its literal content, and adds a pointer to this text node to the delimiter stack. The delimiter stack entry marks whether this is a potential image opener (`![`) or a link opener (`[`).

**`handle_close_bracket` (`]`)**
This is the most complex single-character handler. It implements the *look for link or image* procedure:

Starting at the top of the delimiter stack, it looks backwards for an opening `[` or `![` delimiter. If none is found, it returns a literal `]` text node. If one is found but it is not active, it removes the inactive delimiter and returns a literal `]`. If one is found and is active, it parses ahead to determine whether an inline link `(url)`, a full reference link `[label]`, a collapsed reference link `[]`, or a shortcut reference link follows. If none of these match, it removes the opening delimiter and returns a literal `]`. If a match is found, it returns a link or image node whose children are the inlines between the opener and the `]`, runs `process_emphasis` on those children with the `[` opener as `stack_bottom`, removes the opening delimiter, and — for links (not images) — sets all earlier `[` delimiters to inactive to prevent nested links.

---

### Post-processing: `process_emphasis`

After the main scan loop finishes (EOF reached), there may be unresolved delimiter stack entries for `*` and `_`. The final call is:

```c
process_emphasis(parser, &subj, NULL);
```

The `NULL` argument means "process the entire stack." The `process_emphasis` procedure takes a `stack_bottom` parameter that sets a lower bound on how far it descends into the delimiter stack. It keeps track of `openers_bottom` for each delimiter type (`*`, `_`) and each length of the closing delimiter run modulo 3, initialised to `stack_bottom`.

The algorithm runs as follows:

Move `current_position` forward until the first potential closer with delimiter `*` or `_` is found. Then look back in the stack (staying above `stack_bottom` and the `openers_bottom` for this delimiter type) for the first matching potential opener. If one is found: determine whether it is emphasis or strong emphasis (strong if both closer and opener spans have length ≥ 2), insert an `emph` or `strong` node, remove any delimiters between opener and closer from the stack, and consume 1 or 2 delimiter characters from the opener and closer text nodes (removing them entirely if they become empty). If no opener is found: set `openers_bottom` to the element before `current_position` (establishing a lower bound for future searches), remove the closer from the stack if it cannot be an opener, and advance `current_position`.

The `openers_bottom` optimisation is critical: it needs to be indexed to delimiter run lengths as well as types, ensuring the algorithm never re-scans the same region of the stack for the same closer length, which keeps `process_emphasis` linear rather than quadratic.

### Stack Cleanup

After `process_emphasis` returns, any remaining entries on the delimiter and bracket stacks are freed:

```c
while (subj.last_delim)  remove_delimiter(&subj, subj.last_delim);
while (subj.last_bracket) pop_bracket(&subj);
```

Anything still on the delimiter stack at this point was an unmatched `*`, `_`, `[`, or `![` — those text nodes remain in the tree as literal characters, which is the correct fallback.

---

### Complete Flow Summary

```
cmark_parse_inlines(parent, refmap)
│
├─ PRE-PROCESSING
│   subject_from_buf()       ← build subject cursor with refmap attached
│   cmark_chunk_rtrim()      ← strip trailing whitespace from raw content
│
├─ MAIN SCAN LOOP  while (!eof) parse_inline()
│   peek current char → dispatch:
│   ├─ \n \r              → handle_newline   → softbreak / hardbreak node
│   ├─ `                  → handle_backticks → code node or literal text
│   ├─ \                  → handle_backslash → escaped char / hardbreak / literal
│   ├─ &                  → handle_entity    → decoded char or literal &
│   ├─ <                  → handle_pointy_brace → autolink / raw HTML / literal <
│   ├─ * _ ' "            → handle_delim    → text node + push delimiter stack entry
│   ├─ - .                → handle_hyphen/period (smart typography or literal)
│   ├─ [                  → text node "[" + push bracket/delimiter stack entry
│   ├─ ]                  → handle_close_bracket → link/image node or literal ]
│   ├─ !                  → if followed by [ → text node "![" + push
│   └─ anything else      → bulk-copy to text node up to next special char
│
├─ POST-PROCESSING
│   process_emphasis(NULL)   ← resolve all pending * and _ delimiter pairs
│   │   for each closer: scan back for matching opener
│   │   → wrap content in emph / strong node
│   │   → openers_bottom tracks search lower bounds (prevents quadratic)
│   └─ remove remaining delimiters and brackets from stack (→ literal text)
│
└─ inline AST attached as children of parent node
```

The two most important design points are that emphasis resolution is **entirely deferred** to post-processing (so `*` and `_` are never eagerly consumed), and that `process_emphasis` is also called **mid-scan** inside `handle_close_bracket` for the content inside a link — which is how nested emphasis inside links is handled correctly before the link node is closed.

## ▸ Prompt — 2026-06-03 21:51

> Which are the rules for applying lazy continuation?

Let me fetch the exact spec rules directly before answering.
Good. I now have all the authoritative material from spec 0.31.2 and the source. Here is the full, precise answer.

---

## Rules for Lazy Continuation in CommonMark

**Source:** CommonMark Spec v0.31.2, sections 5.1 (Block quotes) and 5.2 (List items); Appendix A, Phase 1.

---

### The Formal Definition

**Laziness.** If a string of lines *Ls* constitute a block quote with contents *Bs*, then the result of deleting the initial block quote marker from one or more lines in which the next non-whitespace character after the block quote marker is *paragraph continuation text* is a block quote with *Bs* as its content. Paragraph continuation text is text that will be parsed as part of the content of a paragraph, but does not occur at the beginning of the paragraph.

The same principle applies symmetrically to **list items**: a line that would need indentation to belong to a list item's content can be a lazy continuation of the innermost open paragraph inside that item, without the required indentation.

So the rule in one sentence is: **a line that fails the container's continuation condition can still be a lazy continuation, if and only if it would be parsed as paragraph continuation text — that is, if it is a non-blank line that does not start a new block.**

---

### Rule 1 — The Line Must Be Non-Blank

A blank line is **never** a lazy continuation line. It always closes the open paragraph, regardless of the surrounding container context. If a blank line appears inside a block quote without a `>` marker, it terminates the paragraph inside the block quote and may close the block quote itself.

```md
> foo

bar        ← NOT a lazy continuation; blank line ended the paragraph
```

---

### Rule 2 — The Line Must Not Start a New Block

Laziness only applies to lines that would have been continuations of paragraphs had they been prepended with block quote markers. For example, the `> ` cannot be omitted in the second line of `> foo` / `> ---` without changing the meaning.

A line that would start any of the following blocks is **never** a lazy continuation, even if it is non-blank:

| Block type | Why it cannot be lazy |
|---|---|
| ATX heading (`# …`) | Interrupts paragraphs; starts a new block |
| Thematic break (`---`, `***`, `___`) | Interrupts paragraphs; starts a new block |
| Fenced code block (` ``` ` or `~~~`) | Interrupts paragraphs; starts a new block |
| HTML block (type 1–6) | Interrupts paragraphs; starts a new block |
| New list item (new bullet or ordered marker) | Starts a new container |
| Blank line | Closes the paragraph |

For example:

```md
> foo
---             ← NOT lazy: --- would be a thematic break, not paragraph text
```

This renders as a block quote containing `foo`, followed by a standalone thematic break — not a block quote containing `foo---`.

---

### Rule 3 — Setext Heading Underlines Cannot Be Lazy

The setext heading underline cannot be a lazy continuation line in a list item or block quote.

```md
> Foo
---             ← NOT lazy: this is a thematic break outside the blockquote
```

```md
> foo
bar
===             ← bar is lazy; === is NOT lazy, it becomes literal text inside
                  the blockquote paragraph, not a setext underline
```

The reason: a setext underline is only valid directly below its heading text inside the same container. A lazy line that is an `===` or `---` sequence cannot "reach back" through a container boundary to retroactively create a heading.

---

### Rule 4 — Laziness Only Propagates to the Innermost Open Paragraph

Lazy continuation only feeds text into the **innermost open paragraph**. It does not create new paragraphs or feed into non-paragraph leaf blocks (code blocks, HTML blocks, headings). If the innermost open block at the point of the lazy line is not a paragraph, there is nothing to lazily continue into and the line must be re-evaluated as a new block start.

```md
> > foo
bar             ← lazy continuation of the paragraph inside the inner blockquote
```

It is a consequence of the Laziness rule that any number of initial `>`s may be omitted on a continuation line of a nested block quote. So the above is equivalent to `> > foo` / `> > bar`.

---

### Rule 5 — Indented Code Blocks Cannot Be Lazy

An indented code block (4+ spaces) cannot interrupt a paragraph — therefore its continuation lines inside a container are treated as paragraph continuation text when the paragraph is still open. But a line that would *start* an indented code block (if it were at the top level) is still blocked by Rule 2 if it starts a new block. The practical outcome: inside a block quote, a 4-space-indented line that would otherwise be a code block is instead lazy paragraph continuation, because indented code blocks cannot interrupt paragraphs:

```md
> foo
    bar         ← lazy continuation of paragraph (4 spaces, but cannot interrupt para)
```

---

### Summary Decision Tree

This is what the Phase 1 loop (Step 2 + deferred closing from Step 1) effectively checks on each line that fails container continuation:

```
Is the line blank?
  YES → NOT lazy; close unmatched containers
  NO  → Would this line start a new block (ATX heading, thematic break,
         fenced code, HTML block type 1-6, new list item)?
          YES → NOT lazy; close unmatched containers, open new block
          NO  → Is the innermost open block a paragraph?
                  YES → LAZY CONTINUATION; append text, keep containers open
                  NO  → NOT lazy; close unmatched containers
```

### Quick Reference

| Line type | Lazy? |
|---|---|
| Blank | ✗ — always ends paragraph |
| ATX heading `# …` | ✗ — interrupts paragraph |
| Thematic break `---` | ✗ — interrupts paragraph |
| Fenced code fence ` ``` ` | ✗ — interrupts paragraph |
| HTML block start | ✗ — interrupts paragraph |
| New list marker `- `, `1. ` | ✗ — interrupts paragraph |
| Setext underline `===`/`---` | ✗ — explicitly excluded |
| Any plain non-blank text | ✓ — canonical lazy case |
| 4-space-indented text (inside open para) | ✓ — indented code cannot interrupt para |
| Nested `>` omitted continuation | ✓ — classic block quote laziness |

## ▸ Prompt — 2026-06-03 21:58

> With this you mean any block that can be filled with content? so even code blocks are treated as paragraphs?

No, the wording in that excerpt is precise and intentional — "paragraph" means **only a paragraph node**, not any block that accepts content.

The distinction matters a lot. In cmark's block tree, every leaf node has a type, and lazy continuation is **exclusively tied to the `CMARK_NODE_PARAGRAPH` type**. Other content-bearing leaf blocks — fenced code, indented code, HTML blocks — have their own continuation rules and are never candidates for lazy continuation.

Here is why each content-bearing block behaves differently:

**Paragraph** — the only block that accepts lazy continuation. It is specifically defined as "non-blank lines that cannot be interpreted as other kinds of blocks," which means by definition any non-blank, non-block-starting line naturally belongs to a paragraph. Lazy continuation exploits exactly this property.

**Fenced code block** — continues only while the closing fence has not been seen. A line that fails the block quote's `>` requirement inside a fenced code block is not lazily continued — the fenced code block is simply closed, because fenced code has its own hard termination condition independent of paragraph logic.

**Indented code block** — cannot even be opened while a paragraph is open (it cannot interrupt a paragraph). Once open, its continuation condition is "line is blank or indented ≥ 4 spaces," and a lazy line failing that condition closes it, not lazily continues it.

**HTML block** — has 7 subtypes each with their own end condition. None of them participate in lazy continuation.

So the correct mental model is:

```
lazy continuation = the line has nowhere else to go
                    AND the only open leaf block is a paragraph
```

The paragraph is special precisely because it is the **residual block** — the one that forms when no other block structure applies. That is why lazy continuation only works for it: a line without a block quote marker that also fails to start any new block is, by definition, paragraph text, and the only place paragraph text can go is into the open paragraph.

