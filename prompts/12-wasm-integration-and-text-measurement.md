# WASM integration, emscripten::val and text measurement

**Theme:** Mermaid engine

**Source transcript:** `7316746a-768b-435e-8961-a44e9b8886c9.jsonl`  
**Records:** 916–1097

**Dates:** 2026-07-12 15:58 → 2026-07-16 09:15  
**User turns:** 9

**Related commits:**

- `4714bd7 bind both mermaid parser and markdown parser`
- `46e5652 bind markdown wasm build to mermaid generated parser`
- `7526636 add mermaid tests to github workflow`

---

## ▸ Prompt — 2026-07-12 15:58

> Let's design the BrowserMeasurer.
>
> - how to call the JS from the cpp code
> - which types should we use to correctly pass sizes?

Good one to design carefully, because it's the seam between C++ and the browser. Since the WASM target already builds with **`--bind`** (embind), we have `emscripten::val` available, which is the clean tool here.

## Q1 — how to call JS from the C++ code

There are three Emscripten mechanisms; the trade-off is how well they marshal *arrays of strings in* and *numbers out*:

| Mechanism | Fit for this job |
|---|---|
| `EM_ASM` / `EM_ASM_` | Inline JS, args are raw numbers/pointers only. Manual heap juggling for a string array. Too low-level. |
| `EM_JS` | Define a JS function with a C signature. Great for a *fixed* small call, but passing a `vector<string>` means packing into WASM memory yourself (length-prefix or delimiter). Awkward for a batch of arbitrary labels. |
| **`emscripten::val`** (embind) | A C++ handle to a JS value: build JS arrays, call JS functions, read numbers back — **native type marshaling**, no manual packing. ✅ |

**Recommended pattern: `emscripten::val` to marshal, plus one `EM_JS`-installed batch function so the measuring *loop* runs in JS (one boundary crossing for the whole batch — your "measure all at once").**

Flow:
1. Once (lazily), install a JS measuring function via `EM_JS` — it owns a single reusable offscreen canvas.
2. Per `measure()` call: build a JS array of labels with `val`, call the installed function once with `(labels, fontString)`, get a numeric array back, convert to `std::vector<double>`, reshape into `LabelBox`es.

```cpp
// BrowserMeasure.cpp — compiled ONLY under __EMSCRIPTEN__
#include "mermaid/TextMeasure.hpp"
#include <emscripten/val.h>
#include <emscripten/em_js.h>
using emscripten::val;

// Install a batch measurer that runs the whole loop in JS (one crossing).
// Returns a flat Float64Array [w0,h0,w1,h1,...].
EM_JS(void, mm_install_measurer, (), {
  if (globalThis.__mmMeasure) return;
  const canvas = (typeof OffscreenCanvas !== 'undefined')
      ? new OffscreenCanvas(1, 1)
      : document.createElement('canvas');
  const ctx = canvas.getContext('2d');
  globalThis.__mmMeasure = function(labels, font) {
    ctx.font = font;
    const out = new Float64Array(labels.length * 2);
    for (let i = 0; i < labels.length; i++) {
      const m = ctx.measureText(labels[i]);
      const h = (m.actualBoundingBoxAscent + m.actualBoundingBoxDescent) ||
                (parseFloat(font) * 1.2);
      out[2*i] = m.width; out[2*i + 1] = h;
    }
    return out;
  };
});

class BrowserMeasurer : public mermaid::TextMeasurer {
public:
  std::vector<mermaid::LabelBox> measure(const std::vector<std::string>& labels,
                                         const mermaid::FontSpec& font) override {
    mm_install_measurer();
    val jsLabels = val::array();
    for (const auto& s : labels) jsLabels.call<void>("push", val(s));

    std::string fontStr = std::to_string(font.size_px) + "px " + font.family;
    val res = val::global("__mmMeasure")(jsLabels, val(fontStr));

    std::vector<double> flat = emscripten::vecFromJSArray<double>(res);
    std::vector<mermaid::LabelBox> out;
    out.reserve(labels.size());
    for (size_t i = 0; i + 1 < flat.size(); i += 2) out.push_back({flat[i], flat[i + 1]});
    return out;
  }
};
```

Why this split: `val` handles the *string array in* and *number array out* without any manual memory packing; the `EM_JS` function keeps the per-label loop (and the reused canvas) on the JS side so we cross the boundary essentially **once per batch**, not once per label. (If you'd rather not use `EM_JS` at all, you can drive `document.createElement('canvas')…measureText` entirely through `val` from C++, but then each label is several `val` calls — fine for tiny diagrams, wasteful otherwise.)

## Q2 — which types to pass the sizes

**Dimensions are `double` end-to-end**, and the batch travels as a **numeric array**:

- **Widths/heights → `double`.** CSS pixels are fractional (`measureText().width` is a float), and a JS `number` *is* an IEEE-754 double — so `double` is the lossless, natural match. Our interface already uses `LabelBox { double w, h; }`. No `int`/`float` truncation anywhere.
- **The transfer container → a flat `Float64Array` → `std::vector<double>`.** Returning `[w0,h0,w1,h1,…]` and converting once with `emscripten::vecFromJSArray<double>` (or `convertJSArrayToNumberVector<double>`) is a single marshaling op for the whole batch, then we reshape into `LabelBox` pairs. This beats returning an array of `{w,h}` *objects*, where each field read is another `val` property access (2 crossings per label).
- **`FontSpec` → one CSS font string** (`"16px trebuchet ms, verdana, arial, sans-serif"`), passed as a `std::string` (embind converts to a JS string). Keeping the font as a single string mirrors the canvas `ctx.font` API exactly. (If we later add weight/style, they just fold into that string.)

So: **strings in (labels + one font string), doubles out (batched as `Float64Array`), reshaped to `LabelBox{double,double}`** — the same interface the CLI's `ApproxMeasurer` satisfies, so `layout()` doesn't change at all.

## Integration & build notes

- **Emscripten-only TU.** `BrowserMeasure.cpp` compiles only in the WASM target; `<emscripten/val.h>` + `<emscripten/em_js.h>`; `--bind` is already on. It's *not* in the native `libmermaid`.
- **Dependency to actually *use* it:** the browser render path also needs the mermaid **parser compiled for WASM** (its build-time LR codegen is native-only today — the deferred host-tool ExternalProject). So `BrowserMeasurer` is a standalone piece we can build now, but end-to-end web rendering needs that codegen wired too. Until then, the fence handler stays passthrough under `__EMSCRIPTEN__`.
- **Injection point:** whoever renders in the WASM path (e.g. `wasm_bindings.cpp`) constructs `BrowserMeasurer` and passes it to `layout(db, measurer)` — same call the CLI makes with `ApproxMeasurer`.
- **Robustness:** if no canvas/`document` is available (non-browser WASM host), the JS falls back to `parseFloat(font)*1.2` for height; we could also fall back to `ApproxMeasurer` widths. Worth a guard.

## Small things to confirm

- Return shape: **flat `Float64Array`** (my recommendation, above) vs array of `{w,h}` objects.
- Height source: canvas `actualBoundingBox*` (accurate, single-line) with `fontSize*1.2` fallback — fine, since multi-line labels are out of scope for now.

Want me to implement `BrowserMeasure.cpp` as designed (and add it to the Emscripten target's sources), even though the full web render path still awaits the WASM parser codegen? It'll compile and be unit-callable from the WASM side; wiring it end-to-end comes with that codegen.

## ▸ Prompt — 2026-07-13 17:52

> Explain what is emscripten::val with a simple example

`emscripten::val` is a C++ class (from `<emscripten/val.h>`, part of embind) that is a **handle to a live JavaScript value**. It lets your C++ code reach into the JS world and manipulate *any* JS value — numbers, strings, objects, arrays, functions, DOM nodes — at runtime, as if you were writing JavaScript, but in C++ syntax.

## The mental model

Normally, crossing the C++/JS boundary needs fixed, declared signatures. `val` is the **dynamic escape hatch**: one C++ type that can hold *any* JS value, and whose operations are resolved at runtime by actually running them in JS. Think of it as a **remote control for JS objects** — every button press (`.call`, `["prop"]`, `.set`) sends a command across the boundary and JS executes it.

It's dynamically typed, just like JS itself: a `val` might be a number now and an object later; you only commit to a C++ type when you pull a value *out* with `.as<T>()`.

## A simple example

Say we want, from C++, to compute `Math.max(3, 7)` and log it — both are pure JS:

```cpp
#include <emscripten/val.h>
using emscripten::val;

void demo() {
  // JS:  Math.max(3, 7)
  double m = val::global("Math").call<double>("max", 3, 7);   // m == 7.0

  // JS:  console.log("max is", 7)
  val::global("console").call<void>("log", std::string("max is"), m);
}
```

Line by line:
- `val::global("Math")` → a `val` pointing at the JS `Math` object.
- `.call<double>("max", 3, 7)` → invokes its `max` method with `3` and `7`; `<double>` says "convert the JS result back to a C++ `double`."
- `val::global("console").call<void>("log", …)` → calls `console.log`; C++ `3`/strings are auto-marshaled to JS values.

## The core operations

Side by side with the JavaScript they stand for:

| C++ (`val`) | JavaScript |
|---|---|
| `val::global("document")` | `document` |
| `v.call<T>("method", a, b)` | `v.method(a, b)` → as `T` |
| `v["prop"]` | `v.prop` (returns another `val`) |
| `v.set("prop", x)` | `v.prop = x` |
| `fn(a, b)` (`operator()`) | `fn(a, b)` (call a function value) |
| `v.as<double>()` / `.as<std::string>()` | pull the value into C++ |
| `val(3.5)`, `val(std::string("hi"))` | make a JS number / string from C++ |
| `val::array()`, `val::object()` | `[]`, `{}` |

## Another quick one (objects + properties)

```cpp
val el = val::global("document").call<val>("createElement", std::string("div"));
el.set("textContent", std::string("hi"));                 // el.textContent = "hi"
std::string t = el["textContent"].as<std::string>();      // read it back -> "hi"
```

Notice `createElement` returns a `val` (a JS DOM element), which you keep poking at.

## Why we use it for the measurer

In `BrowserMeasurer`, measuring text *is* a JS/DOM operation (`canvas.getContext("2d").measureText(label).width`). `val` lets us call exactly that from C++ and read the resulting number:

```cpp
val ctx = /* the 2d canvas context, a val */;
double w = ctx.call<val>("measureText", label)["width"].as<double>();
//         └ measureText(label)         └ .width   └ into a C++ double
```

So `val` is what makes "call the browser's real font engine from C++" possible without inventing a fixed binding for every call — you just script the browser dynamically. The one caveat: each `val` operation is a real boundary crossing, which is why (in the measurer design) we batch the whole label loop into a single JS function instead of doing thousands of individual `val` calls.

## ▸ Prompt — 2026-07-14 21:47

> Implement BrowserMeasurer.cpp using emscripten::val
>
> javascript input is (labels + one font string) and out an arrays of floats

Let me check whether Emscripten is available so I can compile-verify, then implement.

emcc 5.0.7 is available — I can compile-verify. First, declare the factory in `TextMeasure.hpp` (guarded), then implement the file:

Now compile-verify it with `emcc` (standalone, since it only needs `TextMeasure.hpp`):

Compiled clean (the warnings are just `--bind` being a link-only flag during a compile-only step). Let me confirm the object built and do a full link with embind to make sure `val`/`EM_JS` symbols resolve:

Full link succeeds — a 62KB `.wasm` builds, so `val` + `EM_JS` + the `make_browser_measurer()` factory all resolve correctly with embind. Let me add it to the Emscripten target so it's part of the build:

Let me verify the full Emscripten build still succeeds with `BrowserMeasure.cpp` integrated:

Full Emscripten build succeeds — `BrowserMeasure.cpp` compiles and links into `markdown_parser.wasm`. Let me clean up the verification build dir:

`BrowserMeasurer` implemented and verified.

## What was built

- **[BrowserMeasure.cpp](src/mermaid/BrowserMeasure.cpp)** (Emscripten-only) — a `TextMeasurer` that measures with the browser's real font engine:
  - **JS input**: a `string[]` of labels + one CSS `font` string. **JS output**: a `Float32Array` `[w0,h0,w1,h1,…]` (your "array of floats").
  - `EM_JS` installs the batch function `__mmMeasureLabels(labels, font)` once (reusing a single offscreen canvas; height from `actualBoundingBox*` with a `fontSize*1.2` fallback) — so the per-label loop runs in JS, one boundary crossing per batch.
  - **`emscripten::val`** does all the marshaling: builds the JS label array (`val::array()` + `push`), calls the function (`val::global(...)(...)`), and `convertJSArrayToNumberVector<float>` pulls the floats back, reshaped into `LabelBox{double,double}`.
  - Exposed via `make_browser_measurer()`, declared in [TextMeasure.hpp](include/mermaid/TextMeasure.hpp) under `#ifdef __EMSCRIPTEN__`.
- **CMake**: added to the `markdown_parser_wasm` target.

## Verified
- Compiles clean with `emcc -std=c++20 --bind`.
- Full link produces a valid `.wasm` (embind `val`/`EM_JS` symbols resolve).
- The complete Emscripten build of `markdown_parser_wasm` succeeds with it integrated.

## Two notes
- It **builds but isn't wired end-to-end yet** — the web render path also needs the mermaid **parser/layout compiled for WASM** (the deferred Emscripten LR-codegen). Once that lands, the WASM entry constructs `make_browser_measurer()` and passes it to `layout(db, measurer)` — the exact same call the CLI makes with `ApproxMeasurer`, so nothing else changes.
- Building the wasm target **regenerated the committed artifacts** `web/dist/markdown_parser.{js,wasm}` (they show as modified). That's just the rebuilt output — revert them if you don't want the binary churn in this change, or keep them.

Next logical step for a full web render path: wire the Emscripten LR-codegen (host `lrgen` via ExternalProject) so `parse_flowchart`/`layout` exist in the WASM build, then swap the `__EMSCRIPTEN__` passthrough in `MermaidHandler` for the real render using `BrowserMeasurer`. Want to tackle that?

## ▸ Prompt — 2026-07-14 21:56

> The LR-codegen tool doesn't need to be compiled to wasm, only the generated parser right?

Exactly right. `lrgen` is a **build-time host tool** — it runs on the build machine to emit `flowchart_parser.{hpp,cpp}`, and only that **generated C++** gets compiled to wasm. `lrgen` itself never runs at runtime and never needs to be WebAssembly.

## So what actually needs to be wasm

For the web render path, the Emscripten target must compile:
- the **generated parser** (`flowchart_parser.cpp`),
- plus the mermaid **runtime** it depends on: `FlowParse.cpp`, `Lexer.cpp`, `Lower.cpp`, `Layout.cpp`, `SvgRenderer.cpp`,
- plus `BrowserMeasure.cpp` (already done).

`lrgen` stays a native executable. Its output is just more source files fed to `emcc`.

## The one wrinkle (why it's not automatic)

When you run `emcmake cmake`, CMake sets the compiler to `emcc` for **everything** in that build. So a plain `add_executable(lrgen …)` inside the Emscripten build would try to compile `lrgen` *to wasm* — wrong, because we then need to **run** it at build time to generate the parser, and you can't just execute a `.wasm` as a build step.

That's the whole reason for the **ExternalProject** approach we deferred: it builds `lrgen` with the **host** toolchain (native `clang++`/`gcc`), even while the outer build is cross-compiling to wasm. Then:

```
[host clang]  lrgen.cpp … ─▶ lrgen (native exe)
                                 │  run at build time
                                 ▼
              flowchart_parser.{hpp,cpp}  ─▶ [emcc] ─▶ .wasm
                                             (with FlowParse, Lexer, Lower,
                                              Layout, SvgRenderer, BrowserMeasure)
```

Two clean ways to get the host `lrgen` during the wasm build:
1. **`ExternalProject_Add(lrgen …)`** with the host toolchain — self-contained, always fresh (what we planned).
2. **Reuse the native-built `lrgen`** — point the wasm build's custom command at the `lrgen` binary from the native `build/` dir (or `find_program`). Simpler, but assumes you've done a native configure first.

The `add_custom_command` that runs `lrgen grammar → generated files` is otherwise identical to the native branch; only *where `lrgen` comes from* differs.

So: **build `lrgen` for host, run it, compile its output (+ the mermaid runtime + `BrowserMeasure`) to wasm.** Want me to wire that (option 1 or 2) so `parse_flowchart`/`layout` exist in the WASM build and the fence handler can render in the browser?

## ▸ Prompt — 2026-07-14 22:00

> Implement option one as it is the cleanest solution in my opinion

Implementing option 1. This is a CMake refactor (share the codegen between native and wasm, differing only in how `lrgen` is built) plus enabling the real render in the Emscripten handler. Let me read the current CMakeLists to edit precisely:

Now I'll refactor the CMake to share the codegen, building `lrgen` via ExternalProject under Emscripten:

Now link the mermaid library into the wasm target:

Now rewrite `MermaidHandler.cpp` to render in both builds, choosing the measurer by build:

First verify the native build still works after the CMake refactor:

Native still green (15/15). Now the real test — the Emscripten build with `lrgen` via ExternalProject:

The ExternalProject worked perfectly: it built `lrgen` with the **host** toolchain (native clang — see it compiling `main.cpp`, `GrammarParser.cpp`… into a native exe), ran it to generate the parser, then `emcc` compiled the generated parser + `FlowParse` + `MermaidHandler` + `BrowserMeasure` to wasm and linked. No host-compiler override needed — CMake auto-detected it for the sub-build.

Let me check the wasm API and confirm the module loads/runs in node (basic markdown works; mermaid render needs a browser DOM for `BrowserMeasurer`):

The wasm exposes `parseMarkdownWithFlags(input, "mermaid")`. Let me actually exercise the end-to-end wasm render path in node with a small canvas shim (so `BrowserMeasurer` has something to measure) — this proves the whole browser pipeline compiled correctly:

Default emscripten module. Let me write a node harness with a canvas shim and drive the full pipeline:

Path is relative to the test file in `/tmp`. Let me use an absolute path:

**Full web render path works end to end.** In node with a canvas shim: plain markdown renders correctly, and the mermaid block becomes an SVG with 3 nodes — using `BrowserMeasurer` (the shim's `measureText` was called; note width `316` differs from the native `ApproxMeasurer`'s `338`, confirming the browser measurer is actually driving the sizing). Let me clean up the verification artifacts:

Option 1 implemented and verified end to end.

## What was done

**CMake refactor** ([CMakeLists.txt](CMakeLists.txt)) — the mermaid-parser codegen is now **shared between native and wasm**, differing only in how `lrgen` is obtained:
- **Native**: `add_subdirectory(tools/lrgen)` (the native build *is* the host).
- **Emscripten**: `ExternalProject_Add(lrgen_host)` builds `lrgen` with the **host toolchain** (native clang, not emcc) during the wasm build.
- Both then run the same `add_custom_command` to emit `flowchart_parser.{hpp,cpp}`, compiled into `libmermaid`.
- The wasm target now **links `mermaid`**, so parser + lower + layout + render compile to wasm.

**Handler** ([MermaidHandler.cpp](src/markdown_parser/handlers/MermaidHandler.cpp)) — renders in **both** builds now; the only `#ifdef` is the measurer choice: `ApproxMeasurer` (native) vs `make_browser_measurer()` (wasm).

## Verified

- **ExternalProject did exactly the right thing**: built a *native* `lrgen` (compiling `main.cpp`, `GrammarParser.cpp`… with host clang), ran it to generate the parser, then `emcc` compiled that output + the mermaid runtime + `BrowserMeasure` to wasm — no host-compiler override needed.
- **Native**: build + 15/15 fixtures green (no regression from the refactor).
- **Wasm end-to-end** (node + canvas shim): plain markdown ✓; a ```` ```mermaid ```` block → inline SVG with 3 nodes, sized via `BrowserMeasurer` (measured widths came from the shimmed canvas — proving the browser measurement path is live, not the heuristic).

## The complete picture now

```
markdown ```mermaid``` fence
   → parse (verified vs mermaid, 15 fixtures)
   → lower → layout → render_svg → inline SVG
      • native build: ApproxMeasurer
      • web/wasm build: BrowserMeasurer (real font metrics via canvas)
```

Both builds render server-side to SVG (no client-side mermaid.js needed). As before, building the wasm target regenerated the committed `web/dist/markdown_parser.{js,wasm}` — revert if you don't want that binary churn.

Remaining roadmap items (all deferred, none blocking): the self-consistency SVG test, real recursive subgraph clusters, visual styling (`style`/`classDef`/`linkStyle`), and coordinate/curve polish.

## ▸ Prompt — 2026-07-14 22:23

> how to compile the wasm build?

No wasm target in the Makefile, so it's a manual `emcmake` + `cmake` build. From the repo root:

## Prerequisite: Emscripten

You need the Emscripten toolchain (`emcc`/`emcmake`) on your `PATH`. You already have it (`emcc 5.0.7`). If a shell doesn't, activate the SDK first:

```bash
source /path/to/emsdk/emsdk_env.sh   # only if emcc isn't already on PATH
```

## Build

```bash
# 1. Configure with the emscripten toolchain (emcmake injects it)
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release

# 2. Build
cmake --build build-wasm -j4
```

That produces the module in the repo's committed output dir:

```
web/dist/markdown_parser.js
web/dist/markdown_parser.wasm
```

(The `RUNTIME_OUTPUT_DIRECTORY` in CMake points there, so a wasm build overwrites those committed files.)

## Notes specific to this project

- **First wasm build is slower**: the `ExternalProject` step builds a **native `lrgen`** with the host compiler, runs it to generate `flowchart_parser.{hpp,cpp}`, then `emcc` compiles the mermaid pipeline to wasm. You'll see `Performing build step for 'lrgen_host'` in the log — that's expected.
- **Use a separate build dir** (`build-wasm`) from the native `build/` — they use different toolchains and can't share a configure.
- **Rebuild after changes**: just re-run step 2 (`cmake --build build-wasm -j4`); re-run step 1 only if you change `CMakeLists.txt`.
- **Clean rebuild**: `rm -rf build-wasm` then repeat both steps.

## Quick check it worked

```bash
ls -la web/dist/markdown_parser.wasm     # ~230 KB
```

The module exposes `parseMarkdown(input)` and `parseMarkdownWithFlags(input, "mermaid")` (embind), which the page in [web/index.html](web/index.html) loads.

If you'd like, I can add a `make wasm` target (or a `build-wasm.sh`) that wraps these two commands so it's one step.

## ▸ Prompt — 2026-07-14 22:43

> Good it is working, create a document withe the missing/deferred in this first implementation

Creating a consolidated status/deferred document covering the whole first implementation. There's already a parser-stage [deferred.md](docs/mermaid/deferred.md) and the render [rendering.md](docs/mermaid/rendering.md) roadmap — I'll make this the holistic entry-point that ties them together without duplicating detail.

Created [docs/mermaid/status.md](docs/mermaid/status.md) — the holistic status + deferred overview for the whole first implementation.

## What it covers

- **What works** — the full happy path (fence → lexer → LR(1) parser → `lower` → `FlowDb` → layout → SVG), verified against mermaid across 15 fixtures, rendering in both native and web/wasm builds.
- **Deferred/missing by stage**, in tables/lists:
  - **Parser/lower** — `click`, `linkStyle`, front-matter `title`, entity decoding (+ out-of-scope `@{shape}`/edge IDs) — summarized, pointing to [deferred.md](docs/mermaid/deferred.md) for detail.
  - **Layout** — subgraphs collapsed (no real clusters), longest-path only, fixed barycenter sweeps, rank-centering-only coordinates, straight polylines, self-loops not drawn, geometry-not-matched-by-design.
  - **Rendering** — no visual styling applied, approximate CLI sizing, approximate shape templates, single-line labels only, basic markers.
  - **Build/WASM** — browser DOM required for web measurement (no fallback yet), wasm build regenerates committed `web/dist` artifacts.
  - **Verification** — AST comparison is the only automated gate; SVG self-consistency test deferred.
- **Prioritized next steps** (7 items).

It's positioned as the **entry point** and cross-links to the two detailed docs (`deferred.md` for parser specifics, `rendering.md` for the render plan/roadmap) rather than duplicating them — so there's one place to see overall status and three docs that stay non-overlapping.

## ▸ Prompt — 2026-07-14 23:22

> Check if the github workfows are running the mermaid tests

**No — the CI does not run the mermaid tests.** Confirmed:

- The `test` job runs `ctest --test-dir build -L spec`, which filters to tests **labeled `spec`**.
- Only `spec_tests` carries that label: `gtest_discover_tests(spec_tests PROPERTIES LABELS "spec")`. That's the 652 CommonMark cases.
- `mermaid_tests` and `md_tests` have **no label** (`gtest_discover_tests(mermaid_tests)`), so `-L spec` excludes them — **0** of the 36 mermaid tests match.

The build step *does* compile `mermaid_tests` (it's part of `all`), but ctest never runs it. So a mermaid regression would slip through CI.

## Fix

Label the mermaid tests and include them in the CI filter. `ctest -L` takes a regex, so:

**1. Label the suite** — `tests/CMakeLists.txt`:
```cmake
gtest_discover_tests(mermaid_tests PROPERTIES LABELS "mermaid")
```

**2. Widen the CI filter** — `.github/workflows/ci.yml`:
```yaml
- name: Run tests
  run: ctest --test-dir build -L "spec|mermaid" --output-on-failure
```

That keeps `md_tests` excluded (they appear to be intentionally left out — small stubs, no label) while adding the 36 mermaid tests (lexer + parse + lower + the 15 fixture comparisons against `extract.mjs`).

Note: the fixture tests need the committed `.ast.json` golden files (they are committed) — CI does a fresh native build which runs `lrgen` to generate the parser, then compares, so no `extract.mjs`/node needed at CI time. Good.

Want me to apply both changes?

## ▸ Prompt — 2026-07-16 09:15

> I run the algorithm trought each of the fixtures we have under @tests/test-files/mermaid and all of them are acceptable, these are the things I want to fix:
>
>  @tests/test-files/mermaid/f04b_chainlabels.mmd  : There is not enough space to render the edges and the edges labels. see the screenshot f04b.png attached
>
>  @tests/test-files/mermaid/f05_length.mmd : Edge lenght is not taken in consideration: see image.png
>
> Compare them visually with the golden generated .svgs

