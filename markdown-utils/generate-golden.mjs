// Generates golden mdast for every CommonMark spec case.
//
// Reads  tests/test-files/markdown/commonmark_spec.json  (the same fixture the
// HTML conformance suite uses) and, for each case, parses the `markdown` field
// with remark (unified + remark-parse — the reference mdast implementation).
// Writes  commonmark_spec_mdast.json  next to it, where each entry keeps
// `example` / `section` and gains an `mdast` field with the parsed tree.
//
// The only normalization applied is stripping remark's `position` data (line/
// column offsets), which our C++ renderer does not emit. No field values are
// altered — the point is to see exactly where our output diverges from remark.
//
// Usage (from repo root or this dir):
//   npm install
//   node generate-golden.mjs

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { unified } from "unified";
import remarkParse from "remark-parse";

const here = dirname(fileURLToPath(import.meta.url));
const SPEC_IN = resolve(here, "../tests/test-files/markdown/commonmark_spec.json");
const SPEC_OUT = resolve(here, "../tests/test-files/markdown/commonmark_spec_mdast.json");

const processor = unified().use(remarkParse);

// Recursively drop `position` (and nothing else) from an mdast tree.
function stripPosition(node) {
  delete node.position;
  if (Array.isArray(node.children))
    for (const child of node.children) stripPosition(child);
  return node;
}

const cases = JSON.parse(readFileSync(SPEC_IN, "utf8"));

const out = cases.map((c) => ({
  markdown: c.markdown,
  mdast: stripPosition(processor.parse(c.markdown)),
  example: c.example,
  section: c.section,
}));

writeFileSync(SPEC_OUT, JSON.stringify(out, null, 2) + "\n");
console.log(`wrote ${out.length} golden cases -> ${SPEC_OUT}`);
