# Failing Tests Report

**Total failing tests:** ~189 unique (out of 652 CommonMark spec tests)

---

## 1. List Items — 30 failures

Indented content inside list items is not always recognised correctly. The most common error is failing to detect indented code blocks inside list items (treating them as `<p>` instead of `<pre><code>`). Blank-line and block quote handling within list items also fail.

Examples: `254_List_items`, `255_List_items`, `257_List_items`, `263_List_items`, `270_List_items`

---

## 2. HTML Blocks — 27 failures

The parser incorrectly handles HTML block boundaries. Opening tags like `<pre>` are dropped or the block is terminated too early, causing inner content to be parsed as Markdown instead of raw HTML.

Examples: `148_HTML_blocks`, `149_HTML_blocks`, `150_HTML_blocks`

---

## 3. Fenced Code Blocks — 24 failures

Lines with leading whitespace inside fenced blocks cause the block to split prematurely — the parser closes the first `<pre><code>` and opens a second empty one instead of keeping all content in a single block.

Examples: `119_Fenced_code_blocks`, `120_Fenced_code_blocks`, `122_Fenced_code_blocks`

---

## 4. Setext Headings — 16 failures

The `---` underline sequence is not always recognised as a setext heading marker, especially when it follows another heading or occurs in edge-case contexts. The parser emits `<p>` + `<hr />` instead of `<h2>`.

Examples: `080_Setext_headings`, `082_Setext_headings`, `083_Setext_headings`

---

## 5. Links — 15 failures

Two distinct sub-issues: (a) spaces inside angle-bracket URLs are not percent-encoded (`/my uri` → `/my%20uri`); (b) backslashes in bare URLs are not percent-encoded (`foo\bar` → `foo%5Cbar`).

Examples: `489_Links`, `502_Links`, `503_Links`, `506_Links`, `520_Links`

---

## 6. Lists — 12 failures

A list item starting with a different bullet marker (e.g. `+` vs `-`) should open a new `<ul>`, but the parser nests it as a child list inside the existing one instead of closing and reopening.

Examples: `301_Lists`, `302_Lists`, `308_Lists`, `313_Lists`, `315_Lists`

---

## 7. Entity and Numeric Character References — 12 failures

HTML named entities (e.g. `&nbsp;`, `&copy;`, `&AElig;`) are not resolved to their Unicode equivalents; they are escaped verbatim as `&amp;nbsp;` etc.

Examples: `025_Entity_and_numeric_character_ref`, `026_Entity_and_numeric_character_ref`, `031_Entity_and_numeric_character_ref`

---

## 8. Link Reference Definitions — 8 failures

Link labels containing escaped brackets (e.g. `[Foo*bar\]]`) are not parsed as reference definitions; the entire line is emitted as paragraph text instead of being consumed and resolved on use.

Examples: `194_Link_reference_definitions`, `195_Link_reference_definitions`, `201_Link_reference_definitions`

---

## 9. Images — 8 failures

Inline markup (e.g. `*bar*`) inside an image's alt-text is not stripped to plain text — emphasis markers are partially removed, leaving a truncated alt string (e.g. `alt="foo "` instead of `alt="foo bar"`).

Examples: `573_Images`, `574_Images`, `575_Images`, `576_Images`

---

## 10. Emphasis and Strong Emphasis — 6 failures

Non-breaking spaces (`\xC2\xA0`) adjacent to delimiter characters are not treated as whitespace for the left/right-flanking delimiter rules, so `* a *` (with non-breaking spaces) is incorrectly parsed as `<em>`.

Examples: `353_Emphasis_and_strong_emphasis`, `354_Emphasis_and_strong_emphasis`, `411_Emphasis_and_strong_emphasis`

---

## 11. Block Quotes — 6 failures

Multi-line block quotes are truncated: only the first line is included in the output `<blockquote>`, and subsequent lines are dropped.

Examples: `231_Block_quotes`, `241_Block_quotes`, `244_Block_quotes`

---

## 12. Thematic Breaks — 5 failures

Leading spaces in paragraph text that starts with emphasis markers are not stripped before output, resulting in `<p> <em>-</em></p>` instead of `<p><em>-</em></p>`.

Examples: `056_Thematic_breaks`, `057_Thematic_breaks`, `059_Thematic_breaks`

---

## 13. Indented Code Blocks — 4 failures

Blank lines between indented lines do not continue an indented code block: the parser closes the block at the blank line even when subsequent lines are still indented by four spaces.

Examples: `110_Indented_code_blocks`, `111_Indented_code_blocks`, `112_Indented_code_blocks`

---

## 14. Backslash Escapes — 4 failures

Tilde-fenced blocks (`~~~`) with inner content produce an extra empty `<pre><code></code></pre>` after the real block (same fenced-block splitting bug as category 3).

Examples: `019_Backslash_escapes`, `020_Backslash_escapes`, `021_Backslash_escapes`

---

## 15. Raw HTML — 3 failures

Malformed inline tags spanning multiple lines are incorrectly passed through as valid raw HTML instead of being escaped as literal text.

Examples: `621_Raw_HTML`, `625_Raw_HTML`, `626_Raw_HTML`

---

## 16. Paragraphs — 3 failures

Leading spaces (up to 3) at the start of a paragraph line are not stripped before rendering, so `  aaa` becomes `<p>  aaa` instead of `<p>aaa`.

Examples: `222_Paragraphs`, `224_Paragraphs`, `226_Paragraphs`

---

## 17. Hard Line Breaks — 3 failures

Raw HTML inline content containing two trailing spaces followed by a newline is escaped and broken into a `<br />` hard line break instead of being preserved as-is.

Examples: `642_Hard_line_breaks`, `643_Hard_line_breaks`, `645_Hard_line_breaks`

---

## 18. Soft Line Breaks — 1 failure

Trailing spaces before a soft line break are not stripped from the output, leaving `foo ` (with a trailing space) instead of `foo`.

Example: `649_Soft_line_breaks`

---

## 19. Code Spans — 1 failure

Backticks inside autolink URLs are not percent-encoded — `` ` `` in `<https://foo.bar.`baz>` should produce `%60` in the href attribute.

Example: `346_Code_spans`

---

## 20. Autolinks — 1 failure

Special characters (backslash, bracket) inside autolink URLs are not percent-encoded in the `href` attribute.

Example: `603_Autolinks`
