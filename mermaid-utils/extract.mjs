#!/usr/bin/env node
/**
 * extract.mjs
 *
 * For each input .mmd file, produces two output files alongside it:
 *   <name>.ast.json   — flowDb AST: vertices, edges, subgraphs, direction, title
 *   <name>.svg        — rendered SVG via mermaid CLI (mmdc)
 *
 * Usage:
 *   node extract.mjs file1.mmd file2.mmd   # explicit files
 *   node extract.mjs *                      # all .mmd files in cwd
 *   node extract.mjs                        # same as *
 *
 * Dependencies (install once):
 *   npm install mermaid svgdom dompurify jsdom @mermaid-js/mermaid-cli
 *
 * @mermaid-js/mermaid-cli installs the `mmdc` binary and Puppeteer.
 * AST extraction uses mermaid directly (no browser needed).
 * SVG rendering delegates to mmdc (which handles the browser internally).
 */

// ---------------------------------------------------------------------------
// Static imports — none of these touch the DOM
// ---------------------------------------------------------------------------
import { readFileSync, writeFileSync, readdirSync } from 'fs';
import { resolve, dirname, basename, extname, join } from 'path';
import { execFileSync }                              from 'child_process';
import { createRequire }                             from 'module';

import { createHTMLWindow } from 'svgdom';
import { JSDOM }            from 'jsdom';

// ---------------------------------------------------------------------------
// 1.  Bootstrap DOM globals BEFORE loading mermaid
//     (dynamic imports respect execution order; static imports are hoisted)
// ---------------------------------------------------------------------------

const svgWindow   = createHTMLWindow();
const jsdomWindow = new JSDOM('<!DOCTYPE html><body></body>').window;

globalThis.window   = svgWindow;
globalThis.document = svgWindow.document;

if (!globalThis.window.location) {
  globalThis.window.location = { href: 'about:blank' };
}

// Stub CSSStyleSheet — svgdom doesn't implement it, but mermaid's import
// chain references it. We only need it to not throw; AST extraction never
// actually calls any CSS sheet methods.
if (typeof globalThis.CSSStyleSheet === 'undefined') {
  globalThis.CSSStyleSheet = class CSSStyleSheet {
    replaceSync() {}
    replace()    { return Promise.resolve(this); }
  };
  globalThis.window.CSSStyleSheet = globalThis.CSSStyleSheet;
}

// ---------------------------------------------------------------------------
// 2.  DOMPurify — instance must be on globalThis before mermaid loads
// ---------------------------------------------------------------------------

const { default: createDOMPurify } = await import('dompurify');
const purifyInstance = createDOMPurify(jsdomWindow);

globalThis.DOMPurify        = purifyInstance;
globalThis.window.DOMPurify = purifyInstance;

// ---------------------------------------------------------------------------
// 3.  Load mermaid after globals are ready
// ---------------------------------------------------------------------------

const { default: mermaid } = await import('mermaid');

mermaid.initialize({
  startOnLoad:   false,
  securityLevel: 'antiscript',
  htmlLabels:    false,
  flowchart: { htmlLabels: false },
});

// ---------------------------------------------------------------------------
// 4.  Locate the mmdc binary
//     Resolves node_modules/.bin/mmdc relative to this script's own location,
//     then falls back to PATH so a global install also works.
// ---------------------------------------------------------------------------

function findMmdc() {
  // Try local node_modules/.bin first (most common case)
  const require   = createRequire(import.meta.url);
  const localBin  = join(dirname(resolve(import.meta.url.replace('file://', ''))), 'node_modules', '.bin', 'mmdc');
  try {
    // Check it exists and is executable
    execFileSync(localBin, ['--version'], { stdio: 'pipe' });
    return localBin;
  } catch (_) { /* fall through */ }

  // Fall back to PATH
  try {
    execFileSync('mmdc', ['--version'], { stdio: 'pipe' });
    return 'mmdc';
  } catch (_) {
    return null;
  }
}

const MMDC = findMmdc();
if (!MMDC) {
  console.error(
    '[ERROR] mmdc not found. Install it with: npm install @mermaid-js/mermaid-cli\n' +
    '        SVG output will be skipped for all files.'
  );
}

// ---------------------------------------------------------------------------
// 5.  AST extraction helpers
// ---------------------------------------------------------------------------

function serializeVertices(vertices) {
  return Object.values(vertices).map(v => ({
    id:      v.id,
    label:   v.text    ?? v.id,
    shape:   v.type    ?? 'rect',
    classes: v.classes ? [...v.classes] : [],
    styles:  v.styles  ? [...v.styles]  : [],
    tooltip: v.tooltip ?? null,
    link:    v.link    ?? null,
    domId:   v.domId   ?? null,
  }));
}

function serializeEdges(edges) {
  return edges.map((e, i) => ({
    index:     i,
    id:        e.id     ?? null,
    start:     e.start,
    end:       e.end,
    label:     e.text   ?? null,
    stroke:    e.stroke ?? 'normal',   // normal | dotted | thick
    arrowHead: e.type   ?? 'arrow',    // arrow | circle | cross | none
    length:    e.length ?? 1,
  }));
}

function serializeSubgraphs(subgraphs) {
  return subgraphs.map(sg => ({
    id:        sg.id,
    label:     sg.title ?? sg.id,
    nodes:     sg.nodes ? [...sg.nodes] : [],
    direction: sg.dir   ?? null,
  }));
}

async function extractAST(diagramText) {
  const diagram = await mermaid.mermaidAPI.getDiagramFromText(diagramText);
  const db      = diagram.db;

  return {
    diagramType: diagram.type           ?? 'flowchart',
    title:       db.getDiagramTitle?.() ?? null,
    direction:   db.getDirection?.()    ?? null,
    vertices:    serializeVertices(db.getVertices()),
    edges:       serializeEdges(db.getEdges()),
    subgraphs:   serializeSubgraphs(db.getSubGraphs()),
    classes:     db.getClasses?.()      ?? {},
  };
}

// ---------------------------------------------------------------------------
// 6.  SVG rendering via mmdc
//     mmdc writes the SVG to the output path directly.
//     We run it synchronously to preserve the sequential processing guarantee.
// ---------------------------------------------------------------------------

function renderSVGviaMMDC(inputPath, svgPath) {
  if (!MMDC) {
    throw new Error('mmdc binary not available');
  }
  execFileSync(MMDC, [
    '--input',  inputPath,
    '--output', svgPath,
    '--outputFormat', 'svg',
    '--backgroundColor', 'white',
    '--quiet',
  ], {
    stdio: 'pipe',
    timeout: 30_000,   // 30 s — puppeteer can be slow on first launch
  });
}

// ---------------------------------------------------------------------------
// 7.  File processing
// ---------------------------------------------------------------------------

async function processFile(inputPath) {
  const abs     = resolve(inputPath);
  const dir     = dirname(abs);
  const name    = basename(abs, extname(abs));
  const astPath = join(dir, `${name}.ast.json`);
  const svgPath = join(dir, `${name}.svg`);

  let source;
  try {
    source = readFileSync(abs, 'utf8');
  } catch (err) {
    console.error(`  [ERROR] Cannot read "${inputPath}": ${err.message}`);
    return { file: inputPath, ok: false, error: err.message };
  }

  console.log(`Processing: ${inputPath}`);

  // -- AST (mermaid library, no browser) --
  try {
    const ast = await extractAST(source);
    writeFileSync(astPath, JSON.stringify(ast, null, 2), 'utf8');
    console.log(`  ✓ AST  → ${astPath}`);
  } catch (err) {
    console.error(`  [ERROR] AST extraction failed: ${err.message}`);
    return { file: inputPath, ok: false, error: err.message };
  }

  // -- SVG (mmdc, uses Puppeteer internally) --
  try {
    renderSVGviaMMDC(abs, svgPath);
    console.log(`  ✓ SVG  → ${svgPath}`);
  } catch (err) {
    const msg = err.stderr ? err.stderr.toString().trim() : err.message;
    console.error(`  [WARN] SVG render failed: ${msg}`);
    return { file: inputPath, ok: true, svgError: msg };
  }

  return { file: inputPath, ok: true };
}

// ---------------------------------------------------------------------------
// 8.  Argument resolution
// ---------------------------------------------------------------------------

function resolveInputFiles(args) {
  if (args.length === 0 || (args.length === 1 && args[0] === '*')) {
    const cwd   = process.cwd();
    const files = readdirSync(cwd).filter(f => extname(f) === '.mmd');
    if (files.length === 0) {
      console.error('No .mmd files found in current directory.');
      process.exit(1);
    }
    return files.map(f => join(cwd, f));
  }
  return args;
}

// ---------------------------------------------------------------------------
// 9.  Entry point
// ---------------------------------------------------------------------------

async function main() {
  const args  = process.argv.slice(2);
  const files = resolveInputFiles(args);

  console.log(`\nmermaid extractor — processing ${files.length} file(s)\n`);

  const results = [];
  for (const f of files) {
    results.push(await processFile(f));
  }

  const ok     = results.filter(r => r.ok);
  const failed = results.filter(r => !r.ok);

  console.log(`\n── Summary ──────────────────────────`);
  console.log(`  Processed : ${results.length}`);
  console.log(`  OK        : ${ok.length}`);
  console.log(`  Failed    : ${failed.length}`);
  if (failed.length > 0) {
    console.log('\nFailed files:');
    failed.forEach(r => console.log(`  ${r.file}: ${r.error}`));
  }
  console.log('─────────────────────────────────────\n');

  process.exit(failed.length > 0 ? 1 : 0);
}

main().catch(err => {
  console.error('Unhandled error:', err);
  process.exit(1);
});
