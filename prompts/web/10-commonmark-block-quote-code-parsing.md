# CommonMark block quote code parsing

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `fabde004-85e0-4dc4-95e3-54bac4872b9c`  
**Created:** 2026-06-04  
**Messages:** 8 (4 prompts)

**Related commits:**

- `0e17dec fix open/continuation errors for blockquotes`
- `378f572 fix raw html parsing`
- `c8c27c8 fix html blocks continuation rules`

---

## ▸ Prompt — 2026-06-04 13:42

> Can you check cmark implementation or commonMark and let me know what should happen when this code is run?
>
> ┌─ Section    : Block quotes
> │  Example #  : 252
> │  Spec lines : 4081-4093
> ├─ Markdown input ──────────────────────────────────────────
> >     code
> >    not code
> ├─ Expected HTML ───────────────────────────────────────────
> <blockquote>
> <pre><code>code
> </code></pre>
> </blockquote>
> <blockquote>
> <p>not code</p>
> </blockquote>

The search results confirm the general rules but don't show the exact example. Let me look it up directly from the cmark source and the spec.
Good, I have the spec. Now let me fetch the block quotes section specifically around example 252.
Now I have everything I need. There's an important detail in the test case itself.

Here's a full breakdown of what's happening:

---

## The Key Detail: There's a Blank Line in the Spec

The test case shown in your question renders the two lines without a blank line between them, but the actual CommonMark spec (example 252, spec lines 4081–4093) has a **blank line** separating them:

```
>     code
             ← blank line here
>    not code
```

Without that blank line, `cmark 0.30.2` (the installed reference implementation) produces:

```html
<blockquote>
<pre><code>code
</code></pre>
<p>not code</p>
</blockquote>
```
— i.e., both lines inside **one** `<blockquote>`.

**With the blank line** (as the spec actually states), both `cmark` and the expected output agree:

```html
<blockquote>
<pre><code>code
</code></pre>
</blockquote>
<blockquote>
<p>not code</p>
</blockquote>
```
— **two separate** `<blockquote>` elements.

---

## Why the Logic Works This Way

The spec explains this in the text immediately preceding the example:

> *"When including an indented code block in a block quote, remember that the block quote marker includes both the `>` and a following space of indentation. So **five spaces** are needed after the `>`."*

Breaking down each line:

**Line 1: `>     code`**
- `>` + ` ` = block quote marker (consumes 1 `>` and 1 space)
- Remaining: `    code` = **4 spaces** + `code`
- 4 spaces of indentation = **indented code block** ✓

**Line 2: `>    not code`**
- `>` + ` ` = block quote marker (consumes 1 `>` and 1 space)
- Remaining: `   not code` = **3 spaces** + `not code`
- Only 3 spaces = **below the 4-space threshold** → treated as a paragraph, not a code block

The blank line between them is what forces the parser to **close the first blockquote** before opening the second. Without it, the second line continues inside the open blockquote (as a paragraph within it), producing a single `<blockquote>` with both a `<pre><code>` and a `<p>` inside.

---

**TL;DR:** The expected HTML in the test case is correct, but it depends on the blank line between the two `>` lines being present. Your displayed input appears to be missing it — if you're writing a test that reproduces example 252, make sure there's an empty line between `>     code` and `>    not code`.

## ▸ Prompt — 2026-06-09 16:45

> Check the CommonMark specification for the following question:
>
> * html inline block skips inline parsing?
> * what types of inline node skips inline parsing?

Good, I have a lot of the spec. Let me now fetch the inline parsing section and the raw HTML section which are the most relevant parts.
Now I have everything needed. Let me also check the backslash escape section since that explicitly lists what skips inline parsing.
Now I have everything. Here's the complete answer:

---

## 1. Do HTML blocks skip inline parsing?

**Yes — completely.** The spec (§4.6) defines an HTML block as:

> *"A group of lines that is treated as raw HTML (and will not be escaped in HTML output)."*

The content is **passed through verbatim** to the output. The parser never performs inline parsing on it — no `*emphasis*`, no `` `code spans` ``, no backslash escapes, nothing. The content inside an HTML block is opaque to the Markdown parser.

This is confirmed by the two-phase parsing model in the Appendix:

> *"In the second phase, the raw text contents of **paragraphs and headings** are parsed into sequences of Markdown inline elements."*

HTML blocks are not in that list — they are never handed to the inline parser.

There's also a subtle edge case (HTML block type 6) where a `<table>` starts an HTML block that ends at a blank line. The content *before* the blank line is verbatim, but text *after* the blank line resumes normal parsing (including inline parsing). The spec example demonstrates this:

```markdown
<table><tr><td>
<pre>
**Hello**,

_world_.
</pre>
```

The `**Hello**,` is verbatim (inside the HTML block), but `_world_.` after the blank line is parsed as normal Markdown and becomes `<em>world</em>`.

---

## 2. What inline node types skip inline parsing?

There are four inline constructs whose content is **opaque** — the spec explicitly treats them as literal/verbatim and does not run further inline parsing inside them. The spec calls this out directly in the backslash escape section (§2.4):

> *"Backslash escapes do not work in **code blocks**, **code spans**, **autolinks**, or **raw HTML**."*

Since backslash escapes are the most basic mechanism of inline parsing, anything that disables them is effectively skipping inline parsing altogether. The full list:

| Node type | Spec section | Why inline parsing is skipped |
|---|---|---|
| **Indented code block** | §4.4 | *"The contents of a code block are literal text, and do not get parsed as Markdown."* |
| **Fenced code block** | §4.5 | *"The content of a code fence is treated as literal text, not parsed as inlines."* |
| **Code span** (inline) | §6.1 | Content between backticks is normalized and passed through; no further inline constructs are recognized inside it. Entity references are also treated as literal text. |
| **Autolink** (inline) | §6.5 | The URI/email between `<` and `>` is used as-is; it's not parsed for inline structure. |
| **Raw HTML tag** (inline) | §6.6 | Passed through to output without escaping or inline parsing. Backslash escapes don't work inside HTML attributes. |
| **HTML block** (block) | §4.6 | Entire block passed through verbatim; inline parser is never invoked. |

The key distinction is between **leaf blocks that receive inline parsing** (paragraphs, ATX headings, setext headings) and everything else. The Phase 2 appendix is unambiguous:

> *"We then walk the tree, visiting every node, and parse raw string contents of **paragraphs and headings** as inlines."*

Everything not in that list — code blocks, HTML blocks, thematic breaks, link reference definitions — has its content used as-is, never passed to the inline parser.

## ▸ Prompt — 2026-06-09 16:52

> Can you explain the following  test? the following is a html link which is recognized as paragraph but its content (inline) was not inline parsed
>
> ┌─ Section    : Hard line breaks
> │  Example #  : 642
> │  Spec lines : 9305-9311
> ├─ Markdown input ──────────────────────────────────────────
> <a href="foo  
> bar">
> ├─ Expected HTML ───────────────────────────────────────────
> <p><a href="foo  
> bar"></p>
> ├─ Actual HTML ─────────────────────────────────────────────
> <p>&lt;a href=&quot;foo<br />
> bar&quot;&gt;</p>
> └───────────────────────────────────────────────────────────
>
> My parser is running the inline parsing trough the paragraph content when it shouldn't

This is a really interesting case. Let me check the spec carefully.
Now I have everything. The picture is completely clear. Here's the full explanation:

---

## What's actually happening

This example sits at the intersection of **two** inline rules: raw HTML tags and hard line breaks. Your parser is getting it wrong because it's treating the paragraph content as plain text and running standard inline parsing over it, rather than first recognising the entire span `<a href="foo  \nbar">` as a single raw HTML inline node.

### Step 1 — why is this a paragraph, not an HTML block?

An HTML block (§4.6) requires the tag to appear **on a line by itself** (for type 6/7) or match specific block-level tags. This is `<a>`, which is not on the block-level list, and the full tag spans two lines. So the parser correctly identifies this as a **paragraph** containing inline content. Your parser is right about that.

### Step 2 — why is the content a raw HTML inline tag, not parsed text?

The raw HTML inline grammar (§6.6) defines an **open tag** as:

> *"`<` character, a tag name, zero or more attributes, optional spaces/tabs and **up to one line ending**, an optional `/`, and a `>`."*

And an **attribute value** (double-quoted) is defined as:

> *"`"`, zero or more characters **not including `"`**, and a final `"`."*

Critically, **line endings are not excluded** from double-quoted attribute values. So `"foo  \nbar"` is a perfectly valid double-quoted attribute value that spans a line boundary. The full token `<a href="foo  \nbar">` therefore matches the open tag grammar as a single raw HTML inline node, consuming both lines.

### Step 3 — why is the `  ` (two spaces) not a hard line break?

The hard line break rule (§9.3) explicitly carves out an exception:

> *"A line ending (**not in a code span or HTML tag**) that is preceded by two or more spaces…"*

Because the two trailing spaces and the line ending are **inside the raw HTML tag**, the hard line break rule simply does not apply. The spaces are part of the attribute value and are passed through verbatim.

---

## What your parser is doing wrong

Your parser is doing this sequence:

```
paragraph
  └─ inline parse the text: '<a href="foo  \nbar">'
       ├─ sees "foo  \n" → fires hard line break → <br />
       └─ escapes < > " as &lt; &quot; etc.
```

But the correct sequence is:

```
paragraph
  └─ inline parse the text: '<a href="foo  \nbar">'
       └─ recognises entire span as raw HTML open tag → emit verbatim
```

The fix is in your **inline parser's raw HTML tag recogniser**. It needs to match the full open tag — including a double-quoted attribute value that may contain a line ending — **before** the hard line break scanner gets a chance to fire. Since inline parsing in CommonMark is a single-pass scanner over the paragraph's raw text, raw HTML tag recognition must take priority and consume the `  \n` inside the attribute value as part of the tag, not as a hard line break trigger.

In practice, when your inline scanner encounters `<`, it should attempt to match the full open tag grammar (including multiline double-quoted attribute values) right there. If the match succeeds, emit a raw HTML node spanning both lines and advance past the closing `>`. Only if the `<` fails to match any HTML tag grammar should it fall through to escaping as `&lt;`.

## ▸ Prompt — 2026-06-09 17:16

> I inspected the code, the inline html parser fails because it recognizes a \n inside an attribute value. This is wrong behavior always?

Good question. Let me check the spec grammar precisely.
Now I have the complete and precise picture. Here's the answer:

---

**No — it is not wrong behavior always.** Whether a `\n` is allowed inside an inline HTML tag depends precisely on *where* in the tag grammar it appears. The spec draws a clear line between structural positions and quoted attribute values.

## The exact grammar rules

**Structural positions** use the phrase **"up to one line ending"** — meaning 0 or 1 `\n`, no more:

- Before each attribute name: `spaces, tabs, and up to one line ending`
- Before the `=` in an attribute value spec: `up to one line ending`
- After the `=` in an attribute value spec: `up to one line ending`
- Before the closing `>` of an open/closing tag: `up to one line ending`

**Quoted attribute values** have no such restriction. Their grammar only excludes the quote character itself:

- **Double-quoted:** `"` + zero or more characters **not including `"`** + `"`
- **Single-quoted:** `'` + zero or more characters **not including `'`** + `'`
- **Unquoted:** explicitly excludes `spaces, tabs, line endings, ", ', =, <, >, \`` — so **no `\n` allowed**

## What this means for your parser

| Position | `\n` allowed? | How many? |
|---|---|---|
| Before attribute name | Yes | **max 1** |
| Before `=` | Yes | **max 1** |
| After `=` | Yes | **max 1** |
| Before closing `>` | Yes | **max 1** |
| Inside double-quoted value | Yes | **unlimited** |
| Inside single-quoted value | Yes | **unlimited** |
| Inside unquoted value | **No** | 0 |

So your bug is not *"reject all `\n` inside attribute values"* — it's much more surgical: **reject `\n` only inside unquoted attribute values, and reject two or more consecutive `\n` in any structural position.** Quoted values must accept `\n` freely.

A few cmark-verified examples to illustrate the boundaries:

```
<a href="foo\nbar">   → raw HTML ✓  (double-quoted, \n freely allowed)
<a href='foo\nbar'>   → raw HTML ✓  (single-quoted, \n freely allowed)
<a href="foo\n\nbar"> → raw HTML ✓  (double-quoted, unlimited \n)
<a\nhref="foo">       → raw HTML ✓  (one \n before attr name, within limit)
<a\n\nhref="foo">     → NOT raw HTML ✗  (two \n before attr name, exceeds "up to one")
<a href=foo\nbar>     → raw HTML ✓  (but the \n terminates the unquoted value "foo";
                                      "bar" is parsed as a separate boolean attribute)
```

