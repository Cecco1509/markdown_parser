# 13. Testing — dual conformance

← [12. Renderers](12_renderers.md) | [Index](index.md)

---

## 13.1 Strategy

Every CommonMark spec example is checked **twice**, against two independent
external references:

| Suite | Compares | Reference |
|---|---|---|
| `CommonMarkSpecTest` | our HTML | the spec's own expected HTML |
| `JsonMdastTest` | our JSON AST | mdast produced by **remark** |

This is the load-bearing idea of the test design. A single-output suite can be
satisfied by a parser that has quietly baked its output format into the tree.
Two suites over the same tree cannot: any HTML-specific shortcut in the parser
immediately shows up as a *JSON* failure, and vice versa. Several such
shortcuts were found and moved into the renderers exactly this way (see
[§12.2](12_renderers.md#122-the-division-of-labour)).

The mermaid engine is verified the same way — against the real mermaid library
(see [`mermaid-utils/`](../mermaid-utils/README.md)).

---

## 13.2 Fixtures

| File | Produced by | Used by |
|---|---|---|
| `tests/test-files/markdown/commonmark_spec.json` | upstream CommonMark | `CommonMarkSpecTest` |
| `tests/test-files/markdown/commonmark_spec_mdast.json` | `markdown-utils/generate-golden.mjs` | `JsonMdastTest` |
| `tests/test-files/mermaid/*.ast.json` | `mermaid-utils/extract.mjs` | `FlowAstFixture` |

### Regenerating the mdast goldens

```bash
cd markdown-utils && npm install && node generate-golden.mjs
```

It reads `commonmark_spec.json`, parses each `markdown` field with
`unified().use(remarkParse)`, and writes one entry per case:

```jsonc
{ "markdown": "...", "mdast": { /* tree */ }, "example": 1, "section": "Tabs" }
```

The generator can optionally strip remark's `position` data (line/column/offset
on every node). Our renderer does not yet emit positions, so goldens are
generated **without** them; re-enabling `position` makes the suite compare
source spans too — see [§9](09_open_decisions.md).

---

## 13.3 Layout and running

```
tests/markdown/
├── test_commonmark_spec.cpp   # CommonMarkSpecTest  (HTML)
├── test_json_mdast.cpp        # JsonMdastTest       (JSON)
├── commonmark_spec_case.hpp   # fixture loader + PrintTo
└── case_report.hpp            # shared failure-report box
```

Both suites are `TEST_P` parameterized over all spec examples and live in the
same binary, `markdown_tests`.

```bash
# everything
cd build && ctest --output-on-failure

# one suite
./build/tests/markdown_tests --gtest_filter='*CommonMarkSpec*'   # HTML
./build/tests/markdown_tests --gtest_filter='*JsonMdast*'        # JSON
cd build && ctest -R JsonMdast --output-on-failure

# one example (spec example 23, JSON suite)
./build/tests/markdown_tests --gtest_filter='*JsonMdast*/023_*'
```

Test names are generated as `NNN_Section` (zero-padded example number plus a
sanitized section name), so examples are individually addressable.

### Failure reporting

Both suites render the same box via `case_report.hpp`, so output is comparable
across formats:

```
┌─ Section    : Links
│  Example #  : 541
│  Spec lines : 540-545          (HTML suite only)
├─ Markdown input ──…
├─ Expected (spec | remark) ──…
├─ Actual (ours) ──…
└──…
```

Each case type also defines `PrintTo`, so a failing parameter prints as
`#541 [Links]` rather than a raw byte dump.

---

## 13.4 Known-divergence lists

Each suite carries a `kKnownFailures` set of spec example numbers that are
skipped with a documented reason, rather than silently weakened assertions.

Current JSON-suite entry:

- **541** — multi-line reference-definition label; mdast preserves the label's
  original indentation via source positions, which our pipeline strips before
  the label is scanned. See [§12.5](12_renderers.md#125-known-divergences-from-remark).

A skip must always name *why* it diverges. If a divergence turns out to be a
bug rather than a representational choice, it is fixed instead of listed.

---

## 13.5 Continuous integration

`.github/workflows/ci.yml`:

1. **Tests** — configure, build, `ctest -L "spec|mermaid"`.
2. **Build WASM** — only on `main` and only if the tests passed: builds with
   Emscripten and commits `web/dist/` so the
   [live demo](https://cecco1509.github.io/markdown_parser/web/) tracks `main`.

---

← [12. Renderers](12_renderers.md) | [Index](index.md)
