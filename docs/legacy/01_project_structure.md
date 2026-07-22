> # ⚠️ SUPERSEDED — ORIGINAL DESIGN SPEC
>
> This file is the **pre-implementation design specification**, kept for
> historical reference and for the AI-usage report (it records what was
> *planned* before the code existed). It does **not** describe the code as
> built — names, file layout and data structures have since changed.
>
> **See the current documentation in [`docs/`](../index.md).**

---

# 1. Project structure

← [Index](index.md) | Next: [2. Data types and node structures](02_data_types.md) →

---

```
markdown_parser/
├── CMakeLists.txt                  # top-level build config
├── README.md
├── .gitignore
│
├── include/
│   └── markdown_parser/
│       ├── Types.hpp               # NodeType, InlineType, BlockData, InlineData
│       ├── ScannedLine.hpp         # ScannedLine struct
│       ├── BlockNode.hpp           # BlockNode struct
│       ├── InlineNode.hpp          # InlineNode, Delimiter, BracketEntry
│       ├── PreScanner.hpp          # PreScanner class
│       ├── SpineHandler.hpp        # SpineHandler class + SpineMatchResult
│       ├── block_rules.hpp         # ContinuationResult, OpenResult, block_rules namespace
│       └── InlineParser.hpp        # InlineParser class
│
├── src/
│   ├── main.cpp
│   ├── PreScanner.cpp
│   ├── SpineHandler.cpp
│   ├── block_rules.cpp             # continuation/open/close predicates (§3)
│   └── InlineParser.cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_prescanner.cpp
│   ├── test_spine.cpp
│   ├── test_inline.cpp
│   └── test_spec.cpp               # CommonMark spec.json conformance suite
│
├── third_party/
│   ├── nlohmann/                   # JSON — spec test loading
│   └── googletest/                 # unit test framework
│
├── build/                          # git-ignored generated artifacts
└── docs/
```

## CMake targets

```cmake
cmake_minimum_required(VERSION 3.20)
project(markdown_parser CXX)
set(CMAKE_CXX_STANDARD 20)

add_library(md_parser
    src/PreScanner.cpp
    src/SpineHandler.cpp
    src/block_rules.cpp
    src/InlineParser.cpp
)
target_include_directories(md_parser PUBLIC include)
target_include_directories(md_parser PRIVATE third_party)

add_executable(md_parser_bin src/main.cpp)
target_link_libraries(md_parser_bin PRIVATE md_parser)

add_subdirectory(third_party/googletest)
add_subdirectory(tests)
```

## Header dependency order

`Types.hpp` has no internal deps. `ScannedLine.hpp` includes only `Types.hpp`.
`BlockNode.hpp` and `InlineNode.hpp` include `Types.hpp`. `block_rules.hpp`
includes `BlockNode.hpp` and `ScannedLine.hpp`. Component headers (`PreScanner`,
`SpineHandler`, `InlineParser`) include the node headers they operate on.
`SpineHandler.hpp` includes `block_rules.hpp` indirectly via its `.cpp`.
No circular dependencies.

The types defined in these headers are described in [§2 Data types and node structures](02_data_types.md).

---

← [Index](index.md) | Next: [2. Data types and node structures](02_data_types.md) →
