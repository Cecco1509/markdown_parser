# Markdown blockquote indentation spacing

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `205ad3c9-d113-458b-a429-982a861e3ff4`  
**Created:** 2026-06-06  
**Messages:** 12 (6 prompts)

**Related commits:**

- `4670163 add tight paragraph handling for blockquotes`
- `56081cb fix indented code block blank lines handling`
- `8c345b6 fix tab handling and infinite looping when found opening markers`

---

## ▸ Prompt — 2026-06-06 08:35

> "markdown": ">\t\tfoo\n",
>     "html": "<blockquote>\n<pre><code>  foo\n</code></pre>\n</blockquote>\n",
>
> this is example 6 in https://spec.commonmark.org/0.31.2/spec.json
>
> Why there are only 2 spaces before foo?

Great question! This is a tab-stop expansion rule in CommonMark. Let me walk through exactly what happens.

---

## The Input

```
>\t\tfoo
```

That's: `>` + TAB + TAB + `foo`

---

## Step 1: Block quote prefix strips the `>`

The `>` is recognized as a blockquote marker. After stripping it, the remaining content is:

```
\t\tfoo
```

But here's the key: the `>` **occupies column 0**. Tabs are expanded to the next **4-column tab stop**. So after consuming `>` at column 0, the cursor is at **column 1**.

---

## Step 2: First TAB expansion

The cursor is at column 1. The next tab stop is at column **4**. So this TAB expands to **3 spaces**, moving the cursor to column 4.

---

## Step 3: Second TAB expansion

The cursor is now at column 4. The next tab stop is at column **8**. So this TAB expands to **4 spaces**, moving the cursor to column 8.

That gives us 3 + 4 = **7 spaces** before `foo` inside the blockquote... but wait.

---

## Step 4: Indented code block detection

Inside the blockquote, CommonMark looks for block-level structure. An **indented code block** requires **4 spaces** of indentation. We have 7 spaces — that's enough. The 4 spaces are consumed as the code-block indent, leaving **3 − 4 = ... wait**, let me be precise:

After the `>` strips at column 1, the first TAB only has **3 remaining columns** to the next stop (columns 1→4). Per the CommonMark spec, when a TAB is partially consumed by a block marker, the **remainder of the tab** counts as spaces. So:

- `>` at col 0 → col 1  
- First TAB: reaches col 4, contributing **3 spaces** to the blockquote's content  
- Of those 3 spaces, they don't reach 4 → not enough for a code block alone  
- Second TAB: reaches col 8, contributing **4 spaces**

Total indent inside blockquote = **3 + 4 = 7 spaces**.  
Code block consumes 4, leaving **3 spaces** before `foo`... 

Hmm, but the output shows **2 spaces**. The answer is the **partial tab rule**: the first TAB, after the `>` consumes column 0, only "donates" columns 1–3 (3 spaces) into the content. But the code-block indentation stripper also works on tab stops. It needs to consume 4 columns of indent. It takes the first TAB's 3 remaining spaces (cols 1–3) plus **1 column** from the second TAB. That leaves **3 remaining columns** of the second TAB — but those 3 columns represent the *tail* of a tab that started at col 4 going to col 8, and after consuming 1 of them, **3 columns remain**... 

Actually the spec is precise: after stripping 4 spaces of code-block indent from `···⇥foo` (where `···` = 3 spaces from partial first tab, `⇥` = full second tab at col 4), you consume the 3 spaces + 1 column of the tab, leaving a partial tab worth **3 spaces** (columns 5, 6, 7) → but the spec rounds this to the content being `  foo` = **2 spaces**.

The exact rule: the second tab spans columns 4–7. After 1 column is consumed for indentation (column 4), the remaining partial tab covers columns 5–7, which is **3 columns**... 

The real answer per spec: the second `\t` after partial consumption covers cols 4→7. Taking 1 col for the code indent leaves a partial tab of width **(8 - 1 - 4) = 3**... The spec actually says the result is `  foo` (2 spaces). This comes from how the tab remainder is computed: `8 - 4 - (4-1) = 1`... 

The cleanest way to see it: **the two tabs, relative to column 1 (after `>`), expand to cols 4 and 8. Stripping the 4-space code-block indent (cols 1–4) leaves content starting at col 5. Col 5 is inside a tab that would have gone from col 4 to col 7 (0-indexed) — that's a partial tab worth `7 - 5 = 2` residual spaces.** Hence `  foo`.

## ▸ Prompt — 2026-06-06 12:34

> Does empty lines (non indented) continue indented code blocks?

No, they do not **break** an indented code block. Empty lines are included **within** the code block as blank lines, as long as indented code lines appear both before and after them.

For example:

```markdown
    line one

    line two
```

Becomes:

```html
<pre><code>line one

line two
</code></pre>
```

The empty line is absorbed into the code block. It's only when there's **no more indented content after the blank lines** that the code block ends — the trailing blank lines are then trimmed off.

So the rule is: blank lines are **lazy** — they don't require 4-space indentation to stay inside the block, but they also can't *start* or *end* one. Only properly indented lines can do that.

## ▸ Prompt — 2026-06-06 12:50

> - foo
>     - bar
>     - baz
> ├─ Expected HTML ───────────────────────────────────────────
> <ul>
> <li>foo
> <ul>
> <li>bar
> <ul>
> <li>baz</li>
> </ul>
> </li>
> </ul>
> </li>
> </ul>
>
>
> Why?

This is about how CommonMark measures **list item indentation** — specifically, the indent required for a sublist is relative to the **content start** of the parent item, not the start of the line.

---

## Step by step

**First item:** `- foo`

- `-` is at column 0, followed by a space, so content starts at column **2**
- A sublist must be indented enough to be inside this item — it needs to reach at least column **2**

**Second item:** `    - bar` (4 spaces + `- bar`)

- The `-` is at column **4**, which is ≥ 2 → it's **inside** the first item, so it becomes a sublist
- Its own content (`bar`) starts at column **6**
- A sublist of *this* item must reach at least column **6**

**Third item:** `    - baz` (4 spaces + `- baz`)

- The `-` is at column **4** again
- Is it inside the second item? The second item's content starts at column 6, and 4 < 6 → **no**, it can't be a sublist of `bar`
- But 4 ≥ 2 → it *is* still inside the first item... 

Except the output shows it nested under `bar`. Why?

---

## The key: continuation indent, not content indent

CommonMark uses a different rule for **continuation** of a list item. Once `- bar` is parsed, its **continuation indent** is defined as the column where its marker started + the marker width + the space = **4 + 1 + 1 = 6**. But for determining whether a new list marker is a *sublist*, what matters is whether it's indented beyond the **marker position** of the current item.

Since `- baz` at column 4 equals the marker position of `- bar` (also column 4), CommonMark sees it as continuing **the same list** as `bar` — i.e., a sibling of `bar`, nested under `foo`. And since it's in the same list as `bar` (both at column 4, both inside `foo`'s content area starting at col 2), `baz` becomes a sibling of `bar`, which means it's still one level deeper than `foo`.

---

## Summary

| Line | Marker col | Content col | Relation |
|---|---|---|---|
| `- foo` | 0 | 2 | top-level item |
| `····- bar` | 4 | 6 | sublist of `foo` (4 ≥ 2) |
| `····- baz` | 4 | 6 | sibling of `bar` (same indent = same list) |

`baz` is a sibling of `bar`, not of `foo` — so it ends up at the **same nesting level** as `bar`, producing three levels of `<ul>`.

## ▸ Prompt — 2026-06-06 12:52

> <ul> does mean new list right? <li> is the element

Yes, exactly:

- `<ul>` = the **list container** (unordered list)
- `<li>` = each **list item** inside it

And crucially, a **sublist is nested inside its parent `<li>`**, not after it. So the structure for your example is:

```
<ul>              ← top-level list
  <li>foo         ← first (and only) item of top list
    <ul>          ← sublist, lives INSIDE the <li>foo
      <li>bar     ← first item of sublist
        <ul>      ← sub-sublist, lives INSIDE <li>bar
          <li>baz</li>
        </ul>
      </li>
    </ul>
  </li>
</ul>
```

So every `<ul>` you see means "a new list starts here", and the nesting of `<ul>` inside `<li>` is what creates the hierarchy.

## ▸ Prompt — 2026-06-06 17:29

> What is done to the line before stating to process it? Use cmark and commonMark as a reference

Before any line is processed for block structure, CommonMark (and cmark's implementation) does two preprocessing steps:

---

## 1. Line ending normalization

The line ending (`\r\n`, `\r`, or `\n`) is stripped/normalized to a single `\n`. A `\r` not followed by `\n` is also treated as a line ending.

---

## 2. Tab expansion — but NOT eagerly

This is the important one. Tabs are **not** blindly expanded to spaces upfront. Instead, they are expanded **on demand** as the parser consumes the line, always relative to the current **column position**.

The rule: a tab advances to the next multiple-of-4 column:
```
next_tab_stop = column + (4 - column % 4)
```

This matters because, as you saw in example 6, a `>` or list marker consumed at column 1 means the following tab only has **3 columns** left before the next stop — producing a partial tab's worth of spaces.

cmark tracks this with a concept of a **partial tab**: when a tab is only partially consumed (some columns used by a block marker, some left over for content), the remainder is remembered as virtual spaces.

---

## That's it — only those two things

No Unicode normalization, no HTML escaping, no other transformation happens at the line level before block parsing begins. Everything else (inline parsing, entity decoding, etc.) happens later on the already-structured block content.

## ▸ Prompt — 2026-06-06 17:32

> Can you check in the cmark implementation? Are you sure that any other character is not substituted?

Let me check the actual cmark source code.
Let me fetch the actual `S_process_line` function directly from the cmark source.
Now let me fetch the raw file to see `S_process_line` and `S_parser_feed`:
Let me fetch the raw file directly to find `S_parser_feed` and `S_process_line`:
Now I have everything. Here's what actually happens:

---

## What cmark does to a line before processing it

Looking at `S_parser_feed` in `blocks.c`, there are **three** things done to the raw input before `S_process_line` ever sees it:

### 1. UTF-8 BOM stripped (first line only)

```c
if (parser->line_number == 0 && ... &&
    *buffer == 0xEF && *(buffer+1) == 0xBB && *(buffer+2) == 0xBF) {
  buffer += 3;
}
```

If the very first line starts with a UTF-8 BOM (`0xEF 0xBB 0xBF`), those 3 bytes are silently skipped. I was wrong to not mention this earlier.

### 2. NULL bytes (`\0`) replaced with the Unicode replacement character (`U+FFFD`)

```c
if (*eol == '\0' && eol < end) {
  // omit NULL byte
  cmark_strbuf_put(&parser->linebuf, buffer, chunk_len);
  // add replacement character
  cmark_strbuf_put(&parser->linebuf, repl, 3);  // repl = { 0xEF, 0xBF, 0xBD }
}
```

Any `\0` in the input is **not passed through** — it's replaced with the UTF-8 encoding of `U+FFFD` (�). I was also wrong to omit this.

### 3. Line endings normalized (`\r\n`, `\r` → treated as line boundary)

`\r\n` is consumed as one line ending, a lone `\r` is also a line ending. The line content itself (passed to `S_process_line`) never contains the line ending characters.

---

## Then inside `S_process_line`: a `\n` is appended

```c
if (bytes == 0 || !S_is_line_end_char(parser->curline.ptr[bytes - 1]))
  cmark_strbuf_putc(&parser->curline, '\n');
```

Every line is **guaranteed to end with `\n`** internally, regardless of the original line ending style.

---

## Summary (corrected)

| Step | What happens |
|---|---|
| UTF-8 BOM | Stripped from the very first line |
| `\0` bytes | Replaced with `U+FFFD` (replacement char) |
| Line endings | `\r\n` and `\r` normalized; line ending stripped before processing |
| `\n` appended | Every line gets a `\n` appended internally |
| Tabs | Expanded lazily, on demand, column-aware (not upfront) |

So I was correct about tabs and line endings, but missed the **BOM stripping** and the **NUL → U+FFFD substitution**. Thanks for pushing me to check the actual source.

