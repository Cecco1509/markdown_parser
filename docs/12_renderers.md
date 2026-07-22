# 12. Renderers — HTML and JSON/mdast

← [11. Link reference definitions](11_link_reference_definitions.md) | [Index](index.md) | Next: [13. Testing](13_testing.md) →

---

## 12.1 The `Renderer` contract

A renderer is any type satisfying the concept in
`include/markdown_parser/renderer/renderer_concept.hpp`:

```cpp
template <typename T>
concept Renderer = requires(T r, const BlockNode &node) {
    { r.render(node) } -> std::convertible_to<std::string>;
};
```

`parse()` is templated on it:

```cpp
template <Renderer R>
std::string parse(const std::string &source, R &renderer, bool debug = false);
```

Renderers implement `NodeVisitor` (`visit(const BlockNode&)` /
`visit(const InlineNode&)`) and walk the finished tree. They receive **no parser
state** — not even the link-definition map — which is the structural guarantee
that the tree is self-sufficient.

Implementations:

| Renderer | Output |
|---|---|
| `HtmlRenderer` | CommonMark-conformant HTML |
| `HtmlRendererDebug` | annotated HTML for debugging |
| `JsonRenderer` | mdast-conformant JSON AST |

`HtmlRendererFactory::create(flags)` wires optional fenced-block handlers
(`mermaid`, `math`) into an `HtmlRenderer`.

---

## 12.2 The division of labour

The parser produces a **render-neutral** tree; each renderer applies its own
target-specific normalization. Getting this boundary right is the central design
lesson of the project — several transformations originally lived in the parser
and had to be moved out once the JSON output existed to contradict them.

| Transformation | Owner | Rationale |
|---|---|---|
| Code-span line endings → spaces (§6.1) | **HtmlRenderer** | An HTML rendering rule. mdast stores the raw content, so the parser keeps interior newlines and only applies the §6.1 *boundary* strip. |
| Reference link → `<a href>` | **HtmlRenderer** | mdast keeps `linkReference` + `definition`; the URL is resolved eagerly at parse time but only *used* by HTML. |
| HTML escaping / URL escaping | **HtmlRenderer** | Never present in the tree. |
| Trailing-newline policy on `code`/`html` values | **JsonRenderer** | mdast-specific; HTML re-emits the raw content. |
| Splitting the info string into `lang` + `meta` | **JsonRenderer** | mdast field shape. |
| Entity / backslash decoding | parser | mdast `value`s are decoded, so this *is* AST content. |
| Blank-line & tightness analysis | parser | Structural; both renderers consume it. |

Rule of thumb: if **remark's mdast** records it, the parser should produce it; if
only the HTML output needs it, the renderer should do it.

---

## 12.3 `HtmlRenderer`

Recursive visitor emitting spec HTML. Points of note:

- **Loose/tight lists** — uses `ListData::tight` (the derived OR, see
  [§2.2](02_data_types.md#tight-vs-spread)) to decide whether item contents are
  wrapped in `<p>`.
- **Code spans** — converts interior `\n` to spaces at emit time (§6.1).
- **Reference links** — reads the already-resolved `destination`/`title` on
  `LinkData`; `reference_type` is ignored.
- **`Definition` nodes** — no case in the switch, so they emit nothing. This is
  correct: a link reference definition produces no output.

## 12.4 `JsonRenderer`

Emits [mdast](https://github.com/syntax-tree/mdast). Beyond the obvious node
mapping, the fidelity details that matter:

- **Phrasing normalization** (`emitPhrasing`) — mdast has no `softBreak` node, so
  soft breaks fold into a literal `\n` inside text; adjacent text runs are
  coalesced; empty text nodes (left by delimiter splitting) are dropped.
- **`code` / `html`** — always emit `lang` and `meta` (null when absent). Strip
  one trailing newline, except for a type 1–5 html block that ran to EOF
  (`end_matched == false`), which mdast keeps. See
  [§2.2 `end_matched`](02_data_types.md#end_matched).
- **`list` / `listItem`** — emit `spread` from the dedicated mdast fields (not
  from `tight`), plus `start` (null for bullet lists) and `checked` (null; GFM
  task lists are unsupported).
- **References** — a `Link`/`Image` with `reference_type != None` becomes
  `linkReference` / `imageReference` carrying `identifier`, `label` and
  `referenceType`, with `url`/`title` omitted. `imageReference` uses `alt` and
  has no children; `linkReference` keeps its children.
- **`image` alt** — the concatenated text of *all* descendants (mirrors
  `mdast-util-to-string`), not just direct text children.

### Example

```markdown
[foo]

[foo]: /url "title"
```

```jsonc
// JsonRenderer — reference-preserving
{ "type": "root", "children": [
  { "type": "paragraph", "children": [
    { "type": "linkReference", "identifier": "foo", "label": "foo",
      "referenceType": "shortcut",
      "children": [ { "type": "text", "value": "foo" } ] } ] },
  { "type": "definition", "identifier": "foo", "label": "foo",
    "url": "/url", "title": "title" } ] }
```

```html
<!-- HtmlRenderer — resolved -->
<p><a href="/url" title="title">foo</a></p>
```

Same tree, two faithful projections.

---

## 12.5 Known divergences from remark

One spec example is knowingly not reproduced (tracked in the test skip-list):

- **Example 541** — a multi-line definition label. mdast preserves the label's
  original inner indentation (`"Foo\n  bar"`) by re-slicing it from the source
  via position offsets; our paragraph accumulation strips continuation-line
  indentation (correct CommonMark behaviour, and remark strips it in `text`
  values too) before the label is scanned. Reproducing it needs source-position
  tracking, not a renderer change. See
  [§9 Design decisions](09_open_decisions.md).

---

← [11. Link reference definitions](11_link_reference_definitions.md) | [Index](index.md) | Next: [13. Testing](13_testing.md) →
