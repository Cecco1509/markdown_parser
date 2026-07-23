# Project-wide namespace unification

**Theme:** Jun 14 refactor day

**Source transcript:** `1365d948-53d3-4761-826b-19b783ca2c02.jsonl`  
**Records:** 377–803

**Dates:** 2026-06-14 10:24 → 2026-06-14 11:35  
**User turns:** 6

**Related commits:**

- `466541d Add project-wide namespace`
- `1e25aa6 move utils functions out of classes`

---

## ▸ Prompt — 2026-06-14 10:24

> Are namespaces coherent and well defined across all header files?

The inconsistency is clear. Here's the full picture:

---

## Current state

| Header | Namespace |
|---|---|
| [parser.hpp](include/markdown_parser/parser.hpp) | `markdown_parser` |
| [block_rules.hpp](include/markdown_parser/block_rules.hpp) | `block_rules` |
| [unicode_fold.hpp](include/markdown_parser/unicode_fold.hpp) | `unicode_fold` |
| [entities.hpp](include/markdown_parser/entities.hpp) | `entities` |
| [string_utils.hpp](include/markdown_parser/string_utils.hpp) | `string_utils` |
| [commonmark_constants.hpp](include/markdown_parser/commonmark_constants.hpp) | `commonmark` |
| `HtmlRenderer`, `HtmlRendererFactory`, `HandlerRegistry`, `NodeVisitor`, `SpineHandler`, `InlineParser`, `BlockNode`, `InlineNode`, `Types`, `ScannedLine` | **no namespace** |

---

## The problems

**1 — No consistent top-level namespace.** Most classes (`HtmlRenderer`, `SpineHandler`, `InlineParser`, etc.) live in the global namespace. This is the most significant issue — any consumer of the library risks name collisions with their own code or other libraries.

**2 — Utility namespaces are inconsistent with each other.** `block_rules`, `unicode_fold`, `entities`, `string_utils`, `commonmark` are all different styles, none of which nest under a common root.

**3 — `parser.hpp` uses `markdown_parser` but nothing else does.** It's the only file that uses the library's own name as a namespace, making it look like an isolated attempt at the right thing.

---

## What the convention should be

Everything should nest under a single top-level namespace matching the library name, with optional inner namespaces for utilities:

```cpp
namespace markdown_parser {

    // core types — no inner namespace needed
    class HtmlRenderer { ... };
    class SpineHandler { ... };
    struct BlockNode { ... };

    // utilities — inner namespaces are fine
    namespace entities { ... }
    namespace unicode_fold { ... }
    namespace string_utils { ... }
    namespace commonmark { ... }
    namespace block_rules { ... }

} // namespace markdown_parser
```

---

## Is it worth fixing now?

This is a **non-trivial refactor** — every `.cpp` file and every call site would need `markdown_parser::` prefixes or `using namespace markdown_parser` added. It doesn't affect correctness today since everything is a single binary, but it should be done before the library is exposed to external consumers.

Do you want to proceed with the namespace unification?

## ▸ Prompt — 2026-06-14 10:27

> I want to proceed with namespace unification, but first explain what is going to change

Let me read all the headers and source files to give you a precise account.

Now I have a complete picture. Here's exactly what will change:

---

## What changes

### 1 — Every header gains a wrapping `namespace markdown_parser { }`

All 18 headers currently lack a top-level namespace (or use inconsistent ones). Each file gets:

```cpp
namespace markdown_parser {
  // existing content
} // namespace markdown_parser
```

The inner utility namespaces (`block_rules`, `unicode_fold`, `entities`, `string_utils`, `commonmark`) become **nested** inside it:

```cpp
namespace markdown_parser {
  namespace block_rules { ... }    // was: namespace block_rules { ... }
  namespace unicode_fold { ... }   // etc.
}
```

`parser.hpp` already uses `namespace markdown_parser` — it just needs the inner types it references (`InlineParser`, `SpineHandler`, etc.) to also be in the namespace, which will happen automatically.

---

### 2 — Every `.cpp` file gains a wrapping `namespace markdown_parser { }`

The 10 source files that define implementations (`HtmlRenderer.cpp`, `SpineHandler.cpp`, `block_rules.cpp`, etc.) each get the same wrapper. The utility `.cpp` files (`entities.cpp`, `string_utils.cpp`, `block_rules.cpp`, `unicode_fold.cpp`) that currently open their own namespace become nested:

```cpp
// before
namespace block_rules {  ...  } // namespace block_rules

// after
namespace markdown_parser {
namespace block_rules {  ...  } // namespace block_rules
} // namespace markdown_parser
```

---

### 3 — Call sites of utility namespaces update

Every place in `.cpp` files that calls `block_rules::continuationMatches(...)`, `entities::decode(...)`, `string_utils::trimRight(...)`, `commonmark::kTabStop`, `unicode_fold::foldString(...)` stays exactly the same — the `::` qualified calls are still valid because those are nested namespaces. **No call site changes needed** for utility functions.

---

### 4 — `main.cpp` and `wasm_bindings.cpp` add `using` or prefix

`main.cpp` and `wasm_bindings.cpp` instantiate `HtmlRenderer`, `SpineHandler`, `InlineParser`, `JsonRenderer`, `HtmlRendererFactory`. These now live in `markdown_parser::`, so either:

```cpp
// option A — explicit prefix (cleaner)
markdown_parser::HtmlRenderer hr = markdown_parser::HtmlRendererFactory::create(flags);

// option B — using at function scope
using namespace markdown_parser;
HtmlRenderer hr = HtmlRendererFactory::create(flags);
```

`using namespace markdown_parser` at function scope (not file scope) is idiomatic for application code consuming a library.

---

### 5 — `HandlerFn` typedef moves inside the namespace

Currently `using HandlerFn = std::function<...>` is defined at global scope in `HtmlRenderer.hpp` and also in `HandlerRegistry.hpp` (duplicated). Both move inside the namespace and the duplication is resolved — only `HandlerRegistry.hpp` defines it, `HtmlRenderer.hpp` includes `HandlerRegistry.hpp` to get it.

---

### 6 — Handler files (`MermaidHandler.cpp`, `MathHandler.cpp`) are unaffected

They call `HandlerRegistry::add(...)` which will become `markdown_parser::HandlerRegistry::add(...)` — but since the handlers themselves are compiled into the executable (not the library), a `using namespace markdown_parser` at the top of each handler file keeps them clean.

---

### Summary of touch count

| Category | Files | Change |
|---|---|---|
| Headers | 18 | Add `namespace markdown_parser { }` wrapper |
| Library `.cpp` files | 10 | Add `namespace markdown_parser { }` wrapper |
| `main.cpp` | 1 | Add `using namespace markdown_parser` |
| `wasm_bindings.cpp` | 1 | Add `using namespace markdown_parser` |
| Handler `.cpp` files | 2 | Add `using namespace markdown_parser` |
| Call sites of utility namespaces | 0 | No change — qualified calls still valid |

No logic changes anywhere — purely structural. Ready to proceed?

## ▸ Prompt — 2026-06-14 10:29

> is this compliant with cpp conventions?

Mostly yes, but there are two specific points in the plan that are not fully conventional:

---

## 1 — `using namespace markdown_parser` in library `.cpp` files is wrong

The plan said library implementation files (`SpineHandler.cpp`, `HtmlRenderer.cpp`, etc.) would add `using namespace markdown_parser`. That's not the right approach for library internals.

The convention for implementation files is to **wrap the entire file in the namespace**, not to `use` it:

```cpp
// conventional — definition belongs to the namespace
namespace markdown_parser {

void HtmlRenderer::registerHandler(...) { ... }
std::string HtmlRenderer::render(...) { ... }

} // namespace markdown_parser
```

`using namespace X` in a `.cpp` file means "import names from X into this scope" — which works but doesn't actually place your definitions *into* the namespace. It's tolerated in application code but considered sloppy in library implementation files.

`using namespace markdown_parser` is appropriate only in **application-level files** — `main.cpp`, `wasm_bindings.cpp`, and the handler files — where you're a *consumer* of the library, not a *definer*.

---

## 2 — `HandlerFn` is currently defined at global scope in two headers

`HtmlRenderer.hpp` and `HandlerRegistry.hpp` both have:

```cpp
using HandlerFn = std::function<std::string(const std::string &)>;
```

at global scope. This is a violation of the "no pollution of the global namespace" principle. Both occurrences move inside `namespace markdown_parser`, and the one in `HtmlRenderer.hpp` is removed — `HtmlRenderer.hpp` will include `HandlerRegistry.hpp` to get the type.

---

## Corrected plan

| File type | Approach |
|---|---|
| All headers | Wrap in `namespace markdown_parser { }` |
| Library `.cpp` files | Wrap in `namespace markdown_parser { }` |
| `main.cpp`, `wasm_bindings.cpp` | `using namespace markdown_parser` at function scope |
| Handler `.cpp` files | `using namespace markdown_parser` at file scope (they are consumers, not library code) |
| `HandlerFn` | Defined once in `HandlerRegistry.hpp` inside the namespace, removed from `HtmlRenderer.hpp` |

Shall I proceed with implementation?

## ▸ Prompt — 2026-06-14 10:32

> Let's implement the changes

Let me read all headers and source files in parallel before touching anything.

Good. Now I'll make all header changes in parallel, then all source files.

**Types.hpp**

I only added the opening brace but not the closing one. Let me read the current state and complete it.

Just need to add the closing brace at the end.

No response requested.

## ▸ Prompt — 2026-06-14 10:33

> I interrupted you by mistake, please recover and continue

No problem. Let me check exactly what's been done so far.

Only `Types.hpp` and `parser.hpp` are done. Everything else still needs wrapping. I'll do all remaining headers in parallel, then sources.

I only added the opening brace to `BlockNode.hpp`, not the closing one. Let me add it and continue with all remaining headers at once.

Now all remaining headers in parallel:

Now add the closing braces for the files that need them, and handle the remaining headers:

Now `string_utils.hpp`, `SpineHandler.hpp`, `InlineParser.hpp`, `block_rules.hpp`, and the three renderer headers:

Now add closing braces and closing braces to the headers that need them:

Now the three renderer headers and HandlerRegistry — fix the `HandlerFn` duplication at the same time:

Now fix `HtmlRenderer.hpp` — remove the duplicate `HandlerFn`, add include of `HandlerRegistry.hpp`, wrap in namespace:

All headers done. Now wrap all library `.cpp` files:

Now wrap each library `.cpp` file. The utility files (`entities.cpp`, `string_utils.cpp`, `block_rules.cpp`, `unicode_fold.cpp`) already have an inner namespace — I just need to add the outer `markdown_parser` wrapper around them:

Now I'll do all the source file wrapping in parallel:

The user had already modified those files (with debug output). Let me check their actual current content:

Now add closing braces to all source files that need them:

The utility files already have their inner namespace closing, but need the outer `markdown_parser` closing brace. The others need a single closing brace. I'll add them all:

Now update `main.cpp`, `wasm_bindings.cpp`, and handler files:

Now build:

The errors are from stdlib headers (`<unordered_map>`) being included **inside** the namespace — that happened in `SpineHandler.hpp` and `InlineParser.hpp` where my edit placed the namespace opening before the remaining stdlib includes. Stdlib headers must always be included before opening any namespace.

Confirmed. Moving all stdlib includes above the namespace opening in both files:

Clean build. Let me verify it still works:

All done. One issue surfaced and was fixed during the build: stdlib headers (`<unordered_map>`, `<memory>`, etc.) in `SpineHandler.hpp` and `InlineParser.hpp` had ended up inside the namespace opening — these must always be included before any `namespace` block since the standard library uses `::std` internal names that break when parsed from inside a user namespace. Moved them above the namespace and the build went clean.

## ▸ Prompt — 2026-06-14 11:35

> Check for code/structure repeatition across the headers/sources and report

The source files are too large to dump in one shot. Let me do targeted searches for the specific patterns:

Here's everything found, in order of significance:

---

## 1 — Parse pipeline duplicated in three places (significant)

The same 5-line sequence appears in `main.cpp`, `wasm_bindings.cpp`, and `parser.hpp`:

```cpp
InlineParser inline_parser;
SpineHandler spine(inline_parser, ...);
// feed lines...
spine.finalize();
auto doc = spine.releaseDocument();
```

`wasm_bindings.cpp` already extracted this into `runParser()`, but `main.cpp` still duplicates it inline, and `parser.hpp` has a third copy in its `parse()` inline function. The fix would be to move `runParser` (or an equivalent) into the library itself — it's essentially what `parser.hpp::parse()` already is, but `main.cpp` doesn't use it.

---

## 2 — `render()` boilerplate duplicated between `HtmlRenderer` and `HtmlRendererDebug`

[src/HtmlRenderDebug.cpp:9-11](src/HtmlRenderDebug.cpp#L9-L11) repeats exactly:
```cpp
out_.clear();
tight_ = false;
this->visit(root);
```
which is already in [src/HtmlRenderer.cpp:107-109](src/HtmlRenderer.cpp#L107-L109). `HtmlRendererDebug::render` should just call `HtmlRenderer::render(root)` and then post-process `out_`, rather than re-implementing the reset+visit sequence.

---

## 3 — `LinkDef` and `LinkData` share two identical fields

In [include/markdown_parser/Types.hpp:82-95](include/markdown_parser/Types.hpp#L82-L95):
```cpp
struct LinkDef  { std::string destination; std::optional<std::string> title; };
struct LinkData { std::string destination; std::optional<std::string> title; std::optional<std::string> label; };
```
`LinkData` is a strict superset of `LinkDef` with one extra field (`label`). Whether this warrants merging depends on their semantics — `LinkDef` is a reference definition, `LinkData` is a resolved inline link — so the duplication may be intentional to keep the two concepts separate.

---

## 4 — `escapeHtml` / `escapeUrl` are private to `HtmlRenderer`

These are `static` methods on `HtmlRenderer` but they are general-purpose HTML utilities. If `JsonRenderer` or any future renderer ever needs HTML escaping, it would have to duplicate them. They belong in a utility namespace (e.g. `string_utils`) rather than as members of one specific class.

---

## 5 — `<unordered_map>` included twice in `HtmlRenderer.hpp`

`HtmlRenderer.hpp` includes `HandlerRegistry.hpp` (which includes `<unordered_map>`) and also includes `<unordered_map>` itself directly. The direct include is redundant — `#pragma once` prevents a double-compilation but the intent is cleaner without it.

---

## Summary table

| Issue | Files | Priority |
|---|---|---|
| Parse pipeline in 3 places | `main.cpp`, `wasm_bindings.cpp`, `parser.hpp` | High |
| `render()` boilerplate in `HtmlRendererDebug` | `HtmlRenderDebug.cpp` | Medium |
| `escapeHtml`/`escapeUrl` tied to `HtmlRenderer` | `HtmlRenderer.cpp/.hpp` | Low |
| `LinkDef` / `LinkData` field overlap | `Types.hpp` | Informational |
| Redundant `<unordered_map>` include | `HtmlRenderer.hpp` | Trivial |

