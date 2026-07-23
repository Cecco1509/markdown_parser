# Deploying C++ parser to web

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `dc461435-d05f-4d56-a990-bf59a786806b`  
**Created:** 2026-06-03  
**Messages:** 10 (5 prompts)

**Related commits:**

- `011b672 add webassembly compilation for the parser`
- `523342e add html page editor to run the parser`

---

## ▸ Prompt — 2026-06-03 23:59

> What are my options if I want to make my cpp parser available to an web page, something like [markdownlivepreview](https://markdownlivepreview.com) does?

Here are your main options for bringing a C++ parser to the web:

## 1. **WebAssembly (WASM)** — Best option if you want to keep C++ code
Compile your C++ directly to WebAssembly using **Emscripten**. Your parser runs natively in the browser at near-native speed.

- **Pros:** Keep your existing C++ code, fast execution, runs client-side
- **Cons:** Larger bundle size, build toolchain complexity
- **Tools:** [Emscripten](https://emscripten.org/), CMake integration available
- **Example flow:** `emcc parser.cpp -o parser.js -s EXPORTED_FUNCTIONS="['_parse']"`

## 2. **Server-Side API** — Simplest to ship
Wrap your parser in a small HTTP server (using something like **cpp-httplib**, **Crow**, or expose it via Python/Node bindings). The web page sends text to the API, gets back the result.

- **Pros:** Simple web integration, parser stays on your infra, no browser constraints
- **Cons:** Latency on every keystroke, requires hosting, network dependency
- **Good for:** Heavy parsers, proprietary logic you don't want to expose

## 3. **Port to JavaScript/TypeScript** — Most portable
Rewrite the parsing logic in JS/TS. If your grammar is well-defined, tools like **ANTLR** (generates JS parsers from grammars) or **PEG.js / Ohm** can help.

- **Pros:** Zero-latency, no build step, easy npm distribution
- **Cons:** Rewrite effort, potential behavior divergence from C++ version

## 4. **Node.js Native Addon (N-API)** — If you use Node on the backend
Compile your C++ as a `.node` native module and call it from a Node.js server.

- **Pros:** Reuse C++ code server-side with a JS-friendly interface
- **Cons:** Still server-side, requires Node, platform-specific builds

---

## Recommendation

| Goal | Best choice |
|---|---|
| Real-time, client-side (like markdownlivepreview) | **WebAssembly** |
| Quick prototype / heavy processing | **Server-Side API** |
| Long-term maintainability | **Port to JS + ANTLR/PEG** |
| Already have a Node backend | **N-API addon** |

Since markdownlivepreview works fully client-side with no server round-trips, **WebAssembly is the closest match** — Emscripten can compile most C++ parsers with minimal changes, and you can call your exported functions directly from JavaScript via the generated JS glue code.

## ▸ Prompt — 2026-06-04 00:04

> How does web assembly work? when does it load the cpp compiled code and where I have to declare it?

Great questions. Here's how WebAssembly fits into a web page:

## How it works conceptually

Your C++ gets compiled into a `.wasm` binary file (a compact bytecode format the browser understands). The browser has a built-in WASM runtime that executes it — separate from the JS engine but in the same sandbox.

Emscripten also generates a `.js` **glue file** that handles the boring parts: loading the binary, managing memory, and exposing your C++ functions as callable JS functions.

## The files you end up with

```
parser.wasm   ← your compiled C++ (binary)
parser.js     ← Emscripten glue code (auto-generated)
```

## When does it load?

The `.wasm` file is loaded **asynchronously** at runtime, like a `fetch()`. It does **not** block page load, but your parser won't be available until the download + compilation finishes. That typically takes **~100–500ms** on first load (browsers cache it after).

## Where you declare it

You include the glue `.js` file in your HTML, then wait for it to initialize before calling anything:

```html
<!-- 1. Include the Emscripten glue script -->
<script src="parser.js"></script>

<script>
  // 2. Wait for the module to be ready
  Module.onRuntimeInitialized = function () {

    // 3. Now you can call your C++ functions
    const result = Module._parse("your input here");
    console.log(result);
  };
</script>
```

## The full flow, step by step

```
Browser loads page
       │
       ▼
<script src="parser.js"> is parsed
       │
       ▼
parser.js triggers fetch("parser.wasm")  ← async, non-blocking
       │
       ▼
.wasm downloads + browser compiles it
       │
       ▼
Module.onRuntimeInitialized fires  ← your code runs here
       │
       ▼
User types → Module._parse(input) → result rendered
```

## Passing strings (important gotcha)

WASM memory is a raw byte buffer — you can't pass JS strings directly. Emscripten gives you helpers:

```js
Module.onRuntimeInitialized = function () {
  const input = "int main() {}";

  // Allocate string in WASM memory
  const ptr = Module.allocateUTF8(input);

  // Call your parser
  const resultPtr = Module._parse(ptr);

  // Read result string back
  const result = Module.UTF8ToString(resultPtr);

  // Free the memory
  Module._free(ptr);
};
```

## Simpler alternative: `cwrap`

Instead of managing pointers manually, you can declare function signatures once:

```js
Module.onRuntimeInitialized = function () {
  // Declare: function name, return type, [arg types]
  const parse = Module.cwrap("parse", "string", ["string"]);

  // Now call it like a normal JS function
  const result = parse("int main() {}");
};
```

## Modern approach with ES modules

If you use the `-s MODULARIZE=1` Emscripten flag, you get a cleaner async/await style:

```js
import createModule from "./parser.js";

const Module = await createModule();
const parse = Module.cwrap("parse", "string", ["string"]);

// Ready to use
document.getElementById("input").addEventListener("input", (e) => {
  output.textContent = parse(e.target.value);
});
```

This is the closest to how markdownlivepreview works — parse on every keystroke, render instantly, no server involved.

## ▸ Prompt — 2026-06-14 21:24

> Can the wasm code call the browser js?

Yes! This is called **calling back from C++ into JavaScript**, and Emscripten supports it in a few ways:

## 1. `EM_JS` — Define JS functions inside your C++ file

```cpp
#include <emscripten.h>

// Define a JS function inline in C++
EM_JS(void, highlight_error, (int line, int col), {
    // This is actual JavaScript
    const editor = document.getElementById("editor");
    editor.markError(line, col);
});

void parse(const char* input) {
    // ... parsing logic ...
    if (error) {
        highlight_error(error.line, error.col); // call it like a C function
    }
}
```

## 2. `EM_ASM` — Inline JS snippet, fire and forget

```cpp
#include <emscripten.h>

void parse(const char* input) {
    EM_ASM({
        console.log("Parser started");
        document.getElementById("status").textContent = "Parsing...";
    });

    // ... parsing ...

    EM_ASM({
        document.getElementById("status").textContent = "Done";
    });
}
```

You can also pass values from C++ into the JS block:

```cpp
EM_ASM({ console.log("Error at line: " + $0) }, error_line);
//                                         ^^
//                               $0, $1... are C++ args
```

## 3. `emscripten_run_script` — Dynamic JS string (avoid in production)

```cpp
emscripten_run_script("document.getElementById('out').textContent = 'done'");
```
Works but slow — it's basically `eval()`. Useful for quick debugging only.

## 4. JS calls C++, C++ calls back via function pointer

You can pass a JS callback *into* your C++ function, store it, and invoke it later:

```cpp
// C++ side
typedef void (*Callback)(int line, int col, const char* msg);
static Callback g_error_callback = nullptr;

extern "C" {
    void set_error_callback(Callback cb) {
        g_error_callback = cb;
    }

    void parse(const char* input) {
        // ...
        if (error && g_error_callback) {
            g_error_callback(error.line, error.col, error.message);
        }
    }
}
```

```js
// JS side — register the callback
const onError = Module.addFunction((line, col, msgPtr) => {
    const msg = Module.UTF8ToString(msgPtr);
    showError(line, col, msg);
}, "viip"); // signature: void(int, int, char*)

Module._set_error_callback(onError);
Module._parse(...);
```

---

## Summary of when to use each

| Method | Best for |
|---|---|
| `EM_JS` | Reusable JS functions called from C++ |
| `EM_ASM` | Quick one-off DOM updates or logging |
| `emscripten_run_script` | Debugging only |
| Function pointers + `addFunction` | Event-style callbacks, clean separation |

The most **architecturally clean** pattern for a live parser is:

```
JS → calls C++ parse()
          │
          └→ C++ calls back JS with structured results
                    │
                    └→ JS updates the DOM
```

That way your C++ stays pure parsing logic, and all DOM interaction stays in JS.

## ▸ Prompt — 2026-06-14 21:29

> I'm developing a mermaid parser and I need to measure the text, to do so (accurately) I need to call the measureText in the browser javascript. The text measure is needed for each label so I need to call it multiple times

This is a great use case for the **function pointer callback pattern**, since you need a synchronous, repeated, bidirectional call during parsing.

## The setup

**C++ side — declare the callback type and store it:**

```cpp
#include <emscripten.h>

// Callback type: takes a string, returns float (measured width)
typedef float (*MeasureTextFn)(const char* text, const char* font);
static MeasureTextFn g_measure_text = nullptr;

extern "C" {
    void set_measure_text_callback(MeasureTextFn fn) {
        g_measure_text = fn;
    }

    // Helper you call freely anywhere in your parsing code
    float measure_text(const char* text, const char* font) {
        if (g_measure_text) return g_measure_text(text, font);
        return 0.0f;
    }

    void parse(const char* input) {
        // ...during parsing, just call measure_text() naturally...
        float w = measure_text("Some Label", "14px Arial");
        // use w for layout decisions
    }
}
```

**JS side — implement the real measurement and register it:**

```js
Module.onRuntimeInitialized = function () {
    // Create an offscreen canvas for measuring (cheap, no DOM needed)
    const canvas = document.createElement("canvas");
    const ctx = canvas.getContext("2d");

    // The actual implementation
    const measureImpl = (textPtr, fontPtr) => {
        const text = Module.UTF8ToString(textPtr);
        const font = Module.UTF8ToString(fontPtr);
        ctx.font = font;
        return ctx.measureText(text).width;
    };

    // Register it — "fipp" means: float(int ptr, int ptr)
    const fnPtr = Module.addFunction(measureImpl, "fipp");
    Module._set_measure_text_callback(fnPtr);

    // Now parse freely — C++ will call back into JS for every label
    const result = Module._parse(...);
};
```

## The signature string `"fipp"` explained

Emscripten uses a compact string to describe the function signature:

| Char | Type |
|---|---|
| `f` | float |
| `i` | int (also used for pointers) |
| `p` | pointer (preferred over `i` for ptrs in newer Emscripten) |
| `v` | void |
| `d` | double |

First char = **return type**, rest = **argument types**. So `"fipp"` = `float(ptr, ptr)`.

## Important: enable the function pointer table

You need this Emscripten flag at compile time, otherwise `addFunction` won't work:

```bash
emcc parser.cpp -o parser.js \
  -s EXPORTED_FUNCTIONS="['_parse', '_set_measure_text_callback']" \
  -s EXPORTED_RUNTIME_METHODS="['addFunction', 'UTF8ToString', 'allocateUTF8']" \
  -s ALLOW_TABLE_GROWTH=1   # ← required for addFunction
```

## Optimizing for many calls

Since you'll call this **per label**, avoid repeated string allocation overhead by caching the font on the JS side:

```js
let lastFont = null;

const measureImpl = (textPtr, fontPtr) => {
    const text = Module.UTF8ToString(textPtr);
    const font = Module.UTF8ToString(fontPtr);

    // Only update ctx.font when it actually changes
    if (font !== lastFont) {
        ctx.font = font;
        lastFont = font;
    }

    return ctx.measureText(text).width;
};
```

And if the same label text appears multiple times (e.g. repeated node labels), a simple cache helps a lot:

```js
const cache = new Map();

const measureImpl = (textPtr, fontPtr) => {
    const text = Module.UTF8ToString(textPtr);
    const font = Module.UTF8ToString(fontPtr);
    const key = `${font}::${text}`;

    if (cache.has(key)) return cache.get(key);

    if (font !== lastFont) { ctx.font = font; lastFont = font; }
    const width = ctx.measureText(text).width;
    cache.set(key, width);
    return width;
};
```

## The full flow

```
JS: parse(input)
       │
       ▼
C++: encounters label "Start"
       │
       └→ calls g_measure_text("Start", "14px Arial")
                  │
                  ▼
             JS: measureImpl() runs
             ctx.measureText("Start").width → 38.5
                  │
                  ▼
C++: receives 38.5, uses it for layout
       │
       └→ encounters next label, repeats...
       │
       ▼
C++: returns final parse/layout result
       │
       ▼
JS: renders the diagram
```

This keeps your C++ parser completely unaware of the browser — it just calls `measure_text()` as if it were any other utility function.

## ▸ Prompt — 2026-06-14 21:32

> Is it heavy to go back and forth to call the measurement function in js?

Good question. The honest answer is: **it depends on how many labels you have**, but the overhead is real and worth understanding.

## What happens on each call

Every C++ → JS callback crosses the **WASM boundary**:

```
C++ calls fn ptr
    → WASM runtime looks up function in table
    → JS engine takes over
    → UTF8ToString decodes both strings (walks bytes until \0)
    → canvas.measureText() runs
    → float returned back across boundary
    → C++ resumes
```

Each crossing is roughly **~1–5µs** overhead on top of the actual `measureText()` cost. `measureText()` itself is typically **~5–20µs** depending on the browser and string length.

So for **50 labels** that's maybe 1–2ms total — completely fine. For **5000 labels** it starts to matter.

## The real bottleneck is `measureText` itself, not the boundary

`measureText` is surprisingly expensive because the browser has to:
- Apply font shaping rules
- Handle kerning, ligatures
- Check glyph cache

The WASM boundary crossing is cheap compared to that.

## Better approach: batch the measurements

Instead of calling JS once per label, collect **all labels first**, send them in one call, get all widths back:

**C++ side:**
```cpp
// Instead of measuring during parsing, collect labels first
std::vector<std::string> pending_labels;

void queue_label(const char* text) {
    pending_labels.push_back(text);
}

// Call once after collecting all labels
extern "C" void flush_measurements() {
    // Build a single delimited string: "Label1\nLabel2\nLabel3"
    std::string batch;
    for (auto& label : pending_labels) {
        batch += label + "\n";
    }

    // One JS call for everything
    const char* result = g_measure_batch(batch.c_str());

    // Parse back the returned widths: "38.5,72.1,14.0"
    // ... store them for layout phase
}
```

**JS side:**
```js
const measureBatch = (batchPtr) => {
    const batch = Module.UTF8ToString(batchPtr);
    const labels = batch.split("\n").filter(Boolean);

    const widths = labels.map(text => {
        ctx.font = "14px Arial";
        return ctx.measureText(text).width;
    });

    // Return as comma-separated string
    const result = widths.join(",");
    const ptr = Module.allocateUTF8(result);
    return ptr; // C++ must free this!
};
```

This reduces N boundary crossings to **exactly 1**, regardless of label count.

## Even better: pre-measure entirely in JS

Since JS already knows the input text, you could extract all labels in JS *before* even calling the parser, then pass the widths in:

```js
// 1. Extract labels with a cheap regex pre-scan
const labels = extractLabels(input); // JS only, no WASM

// 2. Measure all of them
const widths = Object.fromEntries(
    labels.map(l => [l, measureText(l)])
);

// 3. Pass the map to the parser as JSON
const widthsJson = JSON.stringify(widths);
const ptr = Module.allocateUTF8(widthsJson);
Module._parse(inputPtr, ptr); // C++ parses JSON internally
```

This means **zero boundary crossings for measurement** — all text measuring is done in JS before parsing starts.

## Summary

| Approach | Boundary crossings | Best for |
|---|---|---|
| Per-label callback | N (one per label) | < 100 labels, simplest code |
| Batched callback | 1 | 100s of labels |
| Pre-measure in JS | 0 | When you can extract labels cheaply before parsing |

For a Mermaid parser where diagrams rarely have more than ~50–100 nodes, the **per-label callback is probably fine** — but the caching trick from before (skip repeated labels) gets you most of the benefit of batching with none of the complexity.

