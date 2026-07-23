# Hosting a parser on GitHub pages

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `06d56814-9e65-4e2a-b2ea-ae0ea56edc57`  
**Created:** 2026-06-07  
**Messages:** 20 (10 prompts)

**Related commits:**

- `dc0da1a remove web/dist from git ignore`
- `3f21252 Add write permissions to CI workflow`
- `d58bfb5 edit github CI workflow WASM build commands`

---

## ▸ Prompt — 2026-06-07 13:37

> Can I host my parser on GitHub pages?

GitHub Pages is designed to serve **static files** (HTML, CSS, JS) — so whether you can host your parser there depends on what kind of parser it is:

**Yes, you can host it on GitHub Pages if:**
- Your parser is written in **JavaScript** and runs entirely in the browser (client-side)
- It's a static web app (e.g., a React/Vue/vanilla JS tool where parsing happens in the user's browser)
- You just need to serve the built files (e.g., an `index.html` + JS bundle)

**No, GitHub Pages won't work if:**
- Your parser is a **server-side application** (Python, Node.js, Ruby, etc.) that needs to execute code on a backend
- It requires a **database** or persistent server process
- It needs to handle **POST requests** or any dynamic server logic

**Alternatives if you need a backend:**
- **Vercel** or **Netlify** — free tiers, support serverless functions, easy GitHub integration
- **Railway** or **Render** — good for containerized or long-running services
- **Fly.io** — great for lightweight servers

If you share a bit more about what your parser does and how it's built, I can give you a more specific recommendation!

## ▸ Prompt — 2026-06-07 13:39

> I can convert my cpp project into an executable web assembly script and then execute it from GitHub pages by linking it as a script in an html page?

Yes, exactly! That workflow is totally valid. Here's the general idea:

**The flow:**
1. Compile your C++ → WebAssembly (`.wasm`) using **Emscripten**
2. Emscripten also generates a `.js` glue file that handles loading the `.wasm`
3. You link that glue `.js` in your `index.html`
4. GitHub Pages serves the static files, and the browser runs the Wasm

**Basic example:**
```bash
# Compile with Emscripten
emcc parser.cpp -o parser.js -s EXPORTED_FUNCTIONS='["_parse"]' -s MODULARIZE=1
```

This produces `parser.js` + `parser.wasm`, then in your HTML:
```html
<script src="parser.js"></script>
<script>
  Module().then(m => {
    const result = m._parse(...);
  });
</script>
```

**A few things to keep in mind:**

- **MIME type for `.wasm`** — browsers require `application/wasm`. GitHub Pages *does* serve `.wasm` files with the correct MIME type, so you're fine there.
- **Exported functions** — you need to explicitly tell Emscripten which C++ functions to expose, or they get stripped by the optimizer.
- **String/memory passing** — passing strings or complex data between JS and Wasm requires a bit of extra work (using Emscripten's `cwrap`/`ccall` helpers, or manually reading/writing to the Wasm memory buffer).
- **File I/O** — if your parser reads files, you'll need to adapt that (Emscripten has a virtual filesystem, or you can pass data directly from JS).
- **Build size** — Wasm bundles can get large; use `-O2` or `-O3` optimization flags to reduce size.

It's a very clean solution for a parser since parsing is typically pure computation with no server-side dependencies.

## ▸ Prompt — 2026-06-07 13:50

> How to optimize wasm build size?

Here are the main techniques to reduce Wasm build size:

## Compiler Flags

```bash
# Optimization level (most impactful)
-O2        # Good balance of size/speed
-O3        # Max speed (can increase size)
-Os        # Optimize for size
-Oz        # Aggressively optimize for size (best for minimizing output)

# Strip debug info
-g0        # No debug info (default in release)
--strip-all  # Strip all symbols
```

## Emscripten-Specific Flags

```bash
# Only export what you need
-s EXPORTED_FUNCTIONS='["_myFunc"]'
-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]'

# Disable unused features
-s NO_FILESYSTEM=1        # If you don't use file I/O
-s DISABLE_EXCEPTION_CATCHING=1  # If you don't use C++ exceptions
-s MINIMAL_RUNTIME=1      # Lighter JS runtime glue

# Dead code elimination
-s ELIMINATE_DUPLICATE_FUNCTIONS=1

# Link-time optimization
--closure 1   # Run Google Closure Compiler on the JS glue file
```

## C++ Code-Level Optimizations

```cpp
// Avoid heavy STL headers where possible
// Prefer simple types over complex templated containers

// Disable exceptions and RTTI if unused
// Add to your compile flags:
// -fno-exceptions -fno-rtti
```

## Typical Combined Command

```bash
emcc parser.cpp -o parser.js \
  -Oz \
  -flto \
  -fno-exceptions \
  -fno-rtti \
  -s EXPORTED_FUNCTIONS='["_parse"]' \
  -s NO_FILESYSTEM=1 \
  -s DISABLE_EXCEPTION_CATCHING=1 \
  -s MINIMAL_RUNTIME=1 \
  --closure 1
```

## Compression (biggest wins at serving time)

Even after all of the above, **Gzip/Brotli compression** often gives the biggest reduction. GitHub Pages supports Gzip automatically — Wasm compresses very well (often 60–70% smaller over the wire).

## Quick Reference — Expected Impact

| Technique | Size Reduction |
|---|---|
| `-Oz` vs no optimization | ~40–60% |
| Strip unused exports | ~10–30% |
| `-fno-exceptions -fno-rtti` | ~5–15% |
| `--closure 1` (JS glue) | ~20–40% on the JS side |
| Gzip (served) | ~60–70% |

Start with `-Oz` + explicit exports + `NO_FILESYSTEM=1` — those three alone usually get you most of the way there.

## ▸ Prompt — 2026-06-07 13:56

> What are the compilation steps done by emcc?

Emscripten's `emcc` follows the same general pipeline as a normal C++ compiler, but with extra steps at the end to target WebAssembly:

## The Pipeline

```
C/C++ Source
     │
     ▼
1. Preprocessor        (cpp)
     │  resolves #include, #define, #ifdef
     ▼
2. Compilation         (clang)
     │  C++ → LLVM IR (.ll / .bc)
     ▼
3. Optimization        (LLVM optimizer)
     │  applies -O0 / -O2 / -Oz etc. on IR
     ▼
4. Code Generation     (LLVM backend)
     │  LLVM IR → WebAssembly (.wasm)
     ▼
5. Linking             (wasm-ld)
     │  combines .wasm objects + system libs (libc, libc++, etc.)
     ▼
6. Post-processing     (Emscripten tools)
     │  - wasm-opt (Binaryen) for further optimization
     │  - generates JS glue code
     │  - applies EXPORTED_FUNCTIONS, MINIMAL_RUNTIME, etc.
     ▼
Output: parser.wasm + parser.js
```

## Each Step in Detail

**1. Preprocessor**
- Same as standard `cpp` — expands macros, resolves includes
- Emscripten injects its own system headers (replacing glibc with musl)

**2. Compilation (Clang)**
- `emcc` uses **Clang** under the hood (not GCC)
- Compiles C++ to **LLVM IR**, not native machine code
- You can inspect this with `--emit-llvm`

**3. Optimization (LLVM passes)**
- Standard LLVM optimization passes run on the IR
- This is where `-O2`, `-Oz`, inlining, dead code elimination, etc. happen
- With `-flto`, this is deferred to link time for whole-program optimization

**4. Code Generation**
- LLVM's WebAssembly backend converts IR → `.wasm` object files
- At this stage it's not yet a final `.wasm` — just compiled objects

**5. Linking (wasm-ld)**
- Links your object files together with Emscripten's port of:
  - **musl libc** (instead of glibc)
  - **libc++** (LLVM's C++ standard library)
- Resolves symbols, lays out Wasm memory (stack, heap, globals)
- Dead code stripping happens here too

**6. Post-processing (Binaryen + Emscripten)**
- **`wasm-opt`** (from Binaryen) runs additional Wasm-level optimizations — this is separate from LLVM and often reduces size further
- Emscripten generates the **JS glue file** (`parser.js`) based on your flags
- Applies your `EXPORTED_FUNCTIONS`, `MINIMAL_RUNTIME`, `--closure` etc.

## Inspecting Intermediate Steps

```bash
# Stop after preprocessing
emcc parser.cpp -E

# Stop after compiling to LLVM IR
emcc parser.cpp -emit-llvm -S -o parser.ll

# Stop after compiling to object file (no link)
emcc parser.cpp -c -o parser.o

# See exactly what emcc is invoking
emcc parser.cpp -o parser.js -v
```

The `-v` flag is especially useful — it prints every subprocess call so you can see exactly what Clang, wasm-ld, and wasm-opt are doing.

## ▸ Prompt — 2026-06-07 13:58

> What is the .wasm object file? how it is adapted to run on the browser, is it running on the browser?

Great questions — let me break it down:

## What is a `.wasm` file?

It's a **binary file** containing WebAssembly instructions — a low-level, stack-based instruction set designed as a compilation target. It's not machine code for any specific CPU (not x86, not ARM) — it's an **abstract virtual instruction set**.

```
C++  →  LLVM IR  →  .wasm bytecode
                        │
              not tied to any CPU architecture
```

You can think of it like a `.class` file in Java — it's bytecode for a virtual machine, not native code.

## Yes, it runs in the browser

The browser has a **WebAssembly runtime** built into the JavaScript engine (V8 in Chrome, SpiderMonkey in Firefox, JavaScriptCore in Safari). This runtime:

1. **Validates** the `.wasm` file (checks it's well-formed and safe)
2. **Compiles** the Wasm bytecode → actual native machine code for the user's CPU
3. **Executes** the native code in a sandboxed environment

So the execution path on the user's machine is:

```
.wasm bytecode
     │
     ▼
Browser JIT compiler (e.g. V8)
     │  compiles to native x86 / ARM at load time
     ▼
Native machine code runs on the CPU
     │
     ▼
Result returned to JavaScript
```

This is why Wasm is fast — it's not interpreted, it gets compiled to real native code by the browser.

## How the JS glue file adapts it

The raw `.wasm` file can't just run on its own in a browser — it needs to be **instantiated** via JavaScript. That's what Emscripten's generated `parser.js` glue file does:

```javascript
// Simplified version of what the glue file does:

// 1. Fetch and load the .wasm binary
const wasmBuffer = await fetch("parser.wasm").then(r => r.arrayBuffer());

// 2. Define imports the wasm module needs (memory, env functions)
const importObject = {
  env: {
    memory: new WebAssembly.Memory({ initial: 256 }),
    // any C functions that call into JS (e.g. console.log)
  }
};

// 3. Compile + instantiate the wasm
const { instance } = await WebAssembly.instantiate(wasmBuffer, importObject);

// 4. Expose the exported functions to JS
const _parse = instance.exports._parse;
```

The glue file also handles:
- **Memory management** — sets up the linear memory (Wasm's heap is a flat `ArrayBuffer`)
- **Stack setup** — initializes the Wasm stack pointer
- **C stdlib bootstrapping** — initializes musl libc internals
- **String marshalling** — helpers to copy strings between JS and Wasm memory

## The Wasm Sandbox

The browser enforces strict isolation:

```
┌─────────────────────────────────┐
│           Browser Tab           │
│                                 │
│  ┌──────────┐   ┌────────────┐  │
│  │    JS    │ ↔ │    Wasm    │  │
│  │  engine  │   │  sandbox   │  │
│  └──────────┘   └────────────┘  │
│                      │          │
│              only sees its own  │
│              linear memory      │
└─────────────────────────────────┘
```

- Wasm **cannot access the DOM** directly — must go through JS
- Wasm **cannot make system calls** — no file system, no network on its own
- Wasm **cannot access memory outside its own linear memory buffer**
- All interaction with the outside world goes through the **JS↔Wasm boundary**

This is why it's safe to run arbitrary compiled C++ in a browser — it's fully sandboxed.

## ▸ Prompt — 2026-06-08 16:57

> wasm supports a cpp program that spawns multiple threads?

Yes, but with significant caveats. WebAssembly threading support exists but requires specific conditions:

## How Wasm Threading Works

Wasm threads are built on top of **Web Workers** (the browser's threading primitive) + **SharedArrayBuffer** (shared memory between workers).

```
C++ std::thread
      │
      ▼
Emscripten maps to
      │
      ▼
Web Workers (OS-level threads in the browser)
      │
SharedArrayBuffer (shared linear memory between workers)
      │
      ▼
pthread_* calls work via Emscripten's pthread port
```

## Compile Flags Required

```bash
emcc parser.cpp -o parser.js \
  -pthread \                          # enable pthreads
  -s USE_PTHREADS=1 \                 # Emscripten pthread support
  -s PTHREAD_POOL_SIZE=4 \           # pre-spawn N workers at startup
  -s TOTAL_MEMORY=67108864           # threads need more memory
```

## The Big Caveat — Security Headers

`SharedArrayBuffer` was **disabled in all browsers in 2018** after the Spectre vulnerability, and re-enabled only if the page is **cross-origin isolated**. Your GitHub Pages site must serve these HTTP headers:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

**GitHub Pages does not let you set custom HTTP headers** — so out of the box, threading won't work on GitHub Pages.

## Workarounds for GitHub Pages

**Option 1 — Service Worker header injection**
A Service Worker can intercept responses and inject the required headers:
```javascript
// sw.js
self.addEventListener("fetch", event => {
  event.respondWith(
    fetch(event.request).then(response => {
      const headers = new Headers(response.headers);
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
      headers.set("Cross-Origin-Embedder-Policy", "require-corp");
      return new Response(response.body, { headers });
    })
  );
});
```
This is a known workaround and works in most browsers.

**Option 2 — Switch to Vercel/Netlify**
Both let you set custom headers easily:
```json
// vercel.json
{
  "headers": [
    {
      "source": "/(.*)",
      "headers": [
        { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" },
        { "key": "Cross-Origin-Embedder-Policy", "value": "require-corp" }
      ]
    }
  ]
}
```

## Threading Model Differences vs Native

| Aspect | Native C++ | Wasm Threads |
|---|---|---|
| Thread creation | `std::thread` / `pthread_create` | Spawns a Web Worker |
| Shared memory | Process memory | `SharedArrayBuffer` |
| Atomics | CPU atomics | `Atomics` JS API |
| Startup cost | Low | Higher (Worker init overhead) |
| Max threads | OS limit | Browser limit (~8-16 practical) |
| `fork()` | Supported | Not supported |

## What Works vs What Doesn't

**Works:**
- `std::thread`, `pthread_create`
- Mutexes, condition variables
- `std::atomic`
- Thread-local storage (`thread_local`)

**Doesn't work:**
- `fork()` — no process model in Wasm
- Signal handling between threads
- Anything requiring OS-level process primitives

## Practical Recommendation

If your parser is CPU-bound and benefits from parallelism, threading is worth pursuing — but the Service Worker header trick is your cleanest path on GitHub Pages. If you want to avoid the complexity, Vercel is a one-config-file solution.

## ▸ Prompt — 2026-07-23 00:29

> Does github pages hosts always the root of the project repo or it can also search for something like a .github-pages directory

GitHub Pages gives you a few options for where to serve from — it's configurable in your repo settings:
GitHub Pages gives you two deployment modes, and the folder flexibility differs between them:

## Mode 1 — Deploy from a Branch

In your repo settings under Pages, you pick a branch and optionally a folder for your publishing source. The folder options are limited though — **only two choices are available**:

- `/ (root)` — serves from the repo root
- `/docs` — serves from a `/docs` folder in your repo

There is no arbitrary folder picker; you can't point it at `/dist`, `/out`, `/web`, or any custom name in this mode.

## Mode 2 — GitHub Actions (recommended)

You can also write a GitHub Actions workflow to publish your site. This is much more flexible — your workflow can build your project and then upload **any directory** as the artifact to deploy:

```yaml
# .github/workflows/deploy.yml
- name: Build
  run: emcc parser.cpp -o web/parser.js -Oz

- name: Upload artifact
  uses: actions/upload-pages-artifact@v3
  with:
    path: ./web   # any folder you want
    
- name: Deploy
  uses: actions/deploy-pages@v4
```

Simply change the path to the directory you want so that only that directory will be uploaded as the GitHub Pages source artifact.

## Summary

| Mode | Folder options |
|---|---|
| Deploy from branch | `/` root or `/docs` only |
| GitHub Actions | Any folder you want |

For a Wasm project where you're compiling C++ into a `web/` or `dist/` output directory, **GitHub Actions is the right choice** — it also lets you run `emcc` as part of the build step so you don't need to commit compiled `.wasm` files to the repo.

## ▸ Prompt — 2026-07-23 00:36

> Github is already deploying to GitHub pages but I don't have such file, is it a configuration, probably I just forgot

Yes, most likely it's a configuration set in your repo settings, not a file. GitHub Pages can be enabled in two ways:

**Check your repo settings first:**
Go to `Settings → Pages` in your repository and see what's configured there — it will show you the current source (branch + folder, or a GitHub Actions workflow).

**If it says "GitHub Actions"** then there *is* a workflow file somewhere, you may have just forgotten about it. Check:
```
.github/workflows/
```
There should be a `.yml` file in there that handles the deployment.

**If it says "Deploy from branch"** then it's purely a settings configuration — no file needed, GitHub just serves directly from the branch/folder you selected. This is the most likely case if you don't remember creating any workflow file.

You can also check the **Actions tab** in your repo — if Pages deployments are running there, you'll see the workflow name which will tell you exactly where the config is coming from.

## ▸ Prompt — 2026-07-23 00:41

> This is my ci.yml inside .github/workflows/ change it to deploy GitHub-pages to the /web directory. The current status is the default deployment offered in the settings, currently targeting the root directory

I don't see any file attached to your message — could you paste the contents of your `ci.yml` directly in the chat?

## ▸ Prompt — 2026-07-23 00:41

> This is my ci.yml inside .github/workflows/ change it to deploy GitHub-pages to the /web directory. The current status is the default deployment offered in the settings, currently targeting the root directory
>
> name: CI
> on:
>   push:
>     branches: [main]
>   pull_request:
>     branches: [main]
> jobs:
>   # ── Job 1: run CTest with a native build ─────────────────────────────────
>   test:
>     name: Tests
>     runs-on: ubuntu-latest
>     steps:
>       - uses: actions/checkout@v4
>       - name: Configure
>         run: cmake -B build -DCMAKE_BUILD_TYPE=Release
>       - name: Build
>         run: cmake --build build --parallel
>       - name: Run tests
>         run: ctest --test-dir build -L "spec|mermaid" --output-on-failure
>   # ── Job 2: rebuild WASM and commit to web/dist ───────────────────────────
>   build-wasm:
>     name: Build WASM
>     runs-on: ubuntu-latest
>     needs: test # only runs if tests pass
>     if: github.ref == 'refs/heads/main' && github.event_name == 'push'
>     permissions:
>       contents: write
>     steps:
>       - uses: actions/checkout@v4
>       - name: Install Emscripten
>         uses: mymindstorm/setup-emsdk@v14
>       - name: Configure (WASM)
>         run: emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release 
>       - name: Build (WASM)
>         run: cmake --build build-wasm -j4 
>       - name: Commit web/dist
>         run: |
>           git config user.name  "github-actions[bot]"
>           git config user.email "github-actions[bot]@users.noreply.github.com"
>           git add web/dist/markdown_parser.js web/dist/markdown_parser.wasm
>           git diff --cached --quiet || git commit -m "chore: rebuild WASM [skip ci]"
>           git push

Here's your updated `ci.yml` with a third job that deploys the `/web` directory to GitHub Pages. You'll also need to go to **Settings → Pages** and switch the source from "Deploy from a branch" to **"GitHub Actions"**.
Here's your updated `ci.yml`. The key changes are: a new `deploy` job that waits for `build-wasm` to finish, uses the official Pages actions, and targets your `/web` directory. You'll also need to switch Settings → Pages source to **"GitHub Actions"** once.
```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  # ── Job 1: run CTest with a native build ─────────────────────────────────
  test:
    name: Tests
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --parallel
      - name: Run tests
        run: ctest --test-dir build -L "spec|mermaid" --output-on-failure

  # ── Job 2: rebuild WASM and commit to web/dist ───────────────────────────
  build-wasm:
    name: Build WASM
    runs-on: ubuntu-latest
    needs: test
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
      - name: Install Emscripten
        uses: mymindstorm/setup-emsdk@v14
      - name: Configure (WASM)
        run: emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
      - name: Build (WASM)
        run: cmake --build build-wasm -j4
      - name: Commit web/dist
        run: |
          git config user.name  "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git add web/dist/markdown_parser.js web/dist/markdown_parser.wasm
          git diff --cached --quiet || git commit -m "chore: rebuild WASM [skip ci]"
          git push

  # ── Job 3: deploy /web to GitHub Pages ───────────────────────────────────
  deploy:
    name: Deploy Pages
    runs-on: ubuntu-latest
    needs: build-wasm
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    permissions:
      pages: write
      id-token: write
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@v4
        with:
          ref: main       # pull the commit that build-wasm just pushed
      - name: Setup Pages
        uses: actions/configure-pages@v5
      - name: Upload /web as Pages artifact
        uses: actions/upload-pages-artifact@v3
        with:
          path: ./web     # only this directory gets served
      - name: Deploy
        id: deployment
        uses: actions/deploy-pages@v4
```

**Two things to do on the GitHub side:**

1. Go to **Settings → Pages** and change the source from "Deploy from a branch" to **"GitHub Actions"**. You can delete the old branch/folder setting — it's replaced by this workflow.
2. That's it — the `deploy` job handles everything else automatically.

**Why `ref: main` on the checkout in the deploy job:** `build-wasm` commits and pushes the freshly compiled `.wasm` file before the deploy job runs. Without `ref: main`, the deploy job would check out the *original* commit (before that push) and serve stale or missing Wasm files.

