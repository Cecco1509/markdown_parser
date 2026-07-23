# Markdown to HTML and JSON specification

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `d999a1fe-1f3e-4d0f-9062-bc09509e6c9f`  
**Created:** 2026-06-03  
**Messages:** 2 (1 prompts)

**Related commits:** Spec research — the JSON/mdast output was implemented later:

- `4dfd2cc add utils scripts that generates JSON output for all the test cases in commonmark_spec.json`
- `f42de5e fix JSON output in order to pass mdast golden tests`

---

## ▸ Prompt — 2026-06-03 13:36

> it exists a specification for what each markdown block/construct should look like in HTML and JSON?

Yes! The CommonMark specification is the most widely adopted formal standard for Markdown.

**CommonMark** (commonmark.org) defines precise rules for how every Markdown construct maps to HTML. It covers:

- How each block element (paragraphs, headings, code blocks, blockquotes, lists, thematic breaks, HTML blocks) should parse and render
- How each inline element (emphasis, strong, links, images, code spans, autolinks, raw HTML) should behave
- Edge cases and ambiguity resolution (e.g., nested emphasis, lazy continuation lines)

The spec is available at [spec.commonmark.org](https://spec.commonmark.org) and includes the expected HTML output for every construct with hundreds of worked examples.

There's no official JSON representation in CommonMark itself, but the **CommonMark spec defines an AST (Abstract Syntax Tree)** conceptually, and several implementations expose it as JSON. For example:

- **`cmark`** (the reference C implementation) can output JSON via `--to json`
- **`remark`** (JavaScript) produces an **mdast** (Markdown AST) — a widely used JSON tree format documented at [github.com/syntax-tree/mdast](https://github.com/syntax-tree/mdast)
- **Pandoc** can output its own JSON AST with `pandoc -t json`

**mdast** is probably the most useful if you want a JSON schema — it defines node types like:

```json
{
  "type": "heading",
  "depth": 2,
  "children": [{ "type": "text", "value": "Hello" }]
}
```

So in short: **CommonMark** for the HTML rendering spec, **mdast** for the JSON tree spec. They're complementary.

