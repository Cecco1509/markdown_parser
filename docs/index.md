# Markdown parser — implementation documentation

> Language: C++20
> Reference: [CommonMark v0.31.2](https://spec.commonmark.org/0.31.2/) and
> [mdast](https://github.com/syntax-tree/mdast)
> External libs: `nlohmann/json` (test fixtures), `googletest` (tests)

This documents the parser **as implemented**. For the original pre-implementation
design specification see [`legacy/`](legacy/index.md) — it is kept for historical
reference (and for the AI-usage report) but no longer matches the code.

---

## Design principle: a render-neutral AST

The parser produces one tree that is **not biased toward any output format**.
Normalization that belongs to a specific target lives in that target's renderer:

| Concern | Where it lives | Why |
|---|---|---|
| Code-span line endings → spaces | `HtmlRenderer` | HTML-only rendering rule; mdast keeps them raw |
| Reference link → concrete URL | `HtmlRenderer` | mdast keeps `linkReference` + `definition` |
| HTML-escaping, URL escaping | `HtmlRenderer` | never part of the tree |
| Entity/backslash decoding | parser | mdast stores decoded `value`s |
| Blank-line / tightness analysis | parser | structural, shared by both renderers |

Both outputs are verified against external references — HTML against the
CommonMark spec, JSON against **remark** — which is what keeps the tree honest:
any format-specific decision leaking into the parser shows up as a failure in the
other suite. See [§13 Testing](13_testing.md).

---

## Table of contents

1. [Project structure](01_project_structure.md)
2. [Data types and node structures](02_data_types.md)
   - 2.1 [Enumerations](02_data_types.md#21-enumerations)
   - 2.2 [BlockData variant](02_data_types.md#22-blockdata-variant)
   - 2.3 [InlineData variant](02_data_types.md#23-inlinedata-variant)
   - 2.4 [BlockNode](02_data_types.md#24-blocknode)
   - 2.5 [InlineNode, Delimiter, BracketEntry](02_data_types.md#25-inlinenode-delimiter-bracketentry)
3. [Continuation, open, and close rules](03_continuation_rules.md)
4. [ScannedLine — line scanning](04_scanned_line.md)
5. [SpineHandler — phase 1 (block tree)](05_spine_handler.md)
6. [InlineParser — phase 2 (inline tree)](06_inline_parser.md)
7. [Tab algorithm](07_tab_algorithm.md)
8. [Data flow through phases](08_data_flow.md)
9. [Design decisions — resolved and open](09_open_decisions.md)
10. [block_rules — continuation, open, and close predicates](10_block_rules.md)
11. [Link reference definitions](11_link_reference_definitions.md)
12. [Renderers — HTML and JSON/mdast](12_renderers.md)
13. [Testing — dual conformance](13_testing.md)

### Related

- [Mermaid engine](mermaid/status.md) — the standalone flowchart
  parse → lower → layout → SVG pipeline.
- [`legacy/`](legacy/index.md) — the original design specification (superseded).
