# CommonMark Spec Testing — Implementation Specification

## Overview

Implement a fully parameterized test suite that reads every test case from the
official CommonMark JSON spec file and runs each one as a named Google Test,
reporting the section, example number, markdown input, expected HTML, and actual
HTML on every failure.

---

## Spec file

Download the JSON from:

```
https://spec.commonmark.org/0.31.2/spec.json
```

Place it at:

```
tests/spec/commonmark_spec.json
```

Each entry in the array has the following shape:

```json
{
  "markdown":   "\tfoo\tbaz\t\tbim\n",
  "html":       "<pre><code>foo\tbaz\t\tbim\n</code></pre>\n",
  "example":    1,
  "start_line": 355,
  "end_line":   360,
  "section":    "Tabs"
}
```

---

## Directory layout

```
tests/
├── CMakeLists.txt
├── spec/
│   └── commonmark_spec.json        # downloaded spec file
├── unit/
│   └── ...                         # existing unit tests (unchanged)
└── spec_tests/
    ├── CMakeLists.txt
    ├── commonmark_spec_case.hpp     # SpecCase struct + JSON loader
    └── test_commonmark_spec.cpp     # parameterized Google Test
```

---

## Dependencies

### nlohmann/json

Use nlohmann/json for JSON parsing. Integrate it via CMake FetchContent so no
manual installation is required:

```cmake
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)
```

Link it as:

```cmake
target_link_libraries(spec_tests PRIVATE nlohmann_json::nlohmann_json)
```

### Google Test

Assumed to already be available. If not, add it via FetchContent alongside
nlohmann/json using the same pattern.

---

## `SpecCase` struct

Mirrors the JSON object exactly. Defined in `tests/spec_tests/commonmark_spec_case.hpp`.

```cpp
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace commonmark::testing {

struct SpecCase {
    std::string markdown;
    std::string html;          // expected output
    int         example;       // 1-based sequential number from the spec
    int         start_line;    // line in the spec source where the case begins
    int         end_line;      // line in the spec source where the case ends
    std::string section;       // e.g. "Emphasis and strong emphasis"
};

// Deserializer — lets nlohmann::json fill a SpecCase directly
inline void from_json(const nlohmann::json& j, SpecCase& c) {
    j.at("markdown")  .get_to(c.markdown);
    j.at("html")      .get_to(c.html);
    j.at("example")   .get_to(c.example);
    j.at("start_line").get_to(c.start_line);
    j.at("end_line")  .get_to(c.end_line);
    j.at("section")   .get_to(c.section);
}

inline std::vector<SpecCase> loadSpec(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open spec file: " + path);

    nlohmann::json j;
    file >> j;
    return j.get<std::vector<SpecCase>>();
}

} // namespace commonmark::testing
```

---

## Parameterized test

`tests/spec_tests/test_commonmark_spec.cpp`:

```cpp
#include <gtest/gtest.h>
#include <set>
#include "commonmark_spec_case.hpp"
#include "markdown_parser/parser.hpp"        // your markdown parser header

using commonmark::testing::SpecCase;
using commonmark::testing::loadSpec;

// ---------------------------------------------------------------------------
// Load all cases once at static-init time.
// SPEC_FILE_PATH is injected by CMake (see below).
// ---------------------------------------------------------------------------
static const std::vector<SpecCase> kAllCases = loadSpec(SPEC_FILE_PATH);

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class CommonMarkSpecTest : public ::testing::TestWithParam<SpecCase> {};

TEST_P(CommonMarkSpecTest, MatchesExpectedHtml) {
    const SpecCase& tc = GetParam();

    // Optional: skip cases that are known to not yet be implemented.
    // Keep this list tracked in source so failures are visible, not silent.
    static const std::set<int> kKnownFailures = {
        // example numbers go here, e.g.: 42, 87, 103
    };
    if (kKnownFailures.count(tc.example)) {
        GTEST_SKIP() << "Example #" << tc.example
                     << " not yet implemented"
                     << " (section: " << tc.section << ","
                     << " lines " << tc.start_line << "-" << tc.end_line << ")";
    }

    const std::string actual = markdown_parser::parse(tc.markdown);

    EXPECT_EQ(tc.html, actual)
        << "\n"
        << "┌─ Section    : " << tc.section                             << "\n"
        << "│  Example #  : " << tc.example                             << "\n"
        << "│  Spec lines : " << tc.start_line << "-" << tc.end_line    << "\n"
        << "├─ Markdown input ──────────────────────────────────────────\n"
        << tc.markdown
        << "├─ Expected HTML ───────────────────────────────────────────\n"
        << tc.html
        << "├─ Actual HTML ─────────────────────────────────────────────\n"
        << actual
        << "└───────────────────────────────────────────────────────────\n";
}

// ---------------------------------------------------------------------------
// Instantiation — one named test per spec case
// Name format: "Spec/CommonMarkSpecTest/001_Tabs"
// ---------------------------------------------------------------------------
INSTANTIATE_TEST_SUITE_P(
    Spec,
    CommonMarkSpecTest,
    ::testing::ValuesIn(kAllCases),
    [](const ::testing::TestParamInfo<SpecCase>& info) {
        // Zero-pad example number for stable alphabetical sorting in output
        std::string num = std::to_string(info.param.example);
        num = std::string(3 - std::min<int>(3, num.size()), '0') + num;

        std::string section = info.param.section.substr(0, 32);
        for (char& c : section)
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';

        return num + "_" + section;
    }
);
```

---

## CMake

### `tests/spec_tests/CMakeLists.txt`

```cmake
include(FetchContent)

# nlohmann/json ---------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Test executable -------------------------------------------------------------
add_executable(spec_tests
    test_commonmark_spec.cpp
)

target_link_libraries(spec_tests PRIVATE
    my_parser_lib               # your parser library
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

# Inject the spec file path at compile time — no hardcoded paths in the source
target_compile_definitions(spec_tests PRIVATE
    SPEC_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../spec/commonmark_spec.json"
)

include(GoogleTest)
gtest_discover_tests(spec_tests PROPERTIES LABELS "spec")
```

### `tests/CMakeLists.txt` (additions)

```cmake
enable_testing()
add_subdirectory(unit)
add_subdirectory(spec_tests)    # add this line
```

---

## Build and run

```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run the entire spec suite
ctest --test-dir build -L spec --output-on-failure

# Run only failures from the last run (fast iteration loop)
ctest --test-dir build -L spec --rerun-failed --output-on-failure

# Run a specific section by name pattern
./build/tests/spec_tests/spec_tests --gtest_filter="*Emphasis*"

# Run a specific example number
./build/tests/spec_tests/spec_tests --gtest_filter="*042*"

# List all test names without running them
./build/tests/spec_tests/spec_tests --gtest_list_tests

# Stop on first failure
./build/tests/spec_tests/spec_tests --gtest_break_on_failure

# Show a failure summary at the end of a full run
./build/tests/spec_tests/spec_tests 2>&1 | grep -E "FAILED|PASSED|SKIPPED"
```

---

## Expected output

**Passing run:**

```
[----------] 652 tests from Spec/CommonMarkSpecTest
[ RUN      ] Spec/CommonMarkSpecTest/001_Tabs
[       OK ] Spec/CommonMarkSpecTest/001_Tabs (0 ms)
[ RUN      ] Spec/CommonMarkSpecTest/002_Tabs
[       OK ] Spec/CommonMarkSpecTest/002_Tabs (0 ms)
...
[  PASSED  ] 649 tests.
[  FAILED  ] 3 tests, listed below:
[  FAILED  ] Spec/CommonMarkSpecTest/042_Emphasis_and_strong_emphasis
```

**On a failure the full diagnostic is printed:**

```
[ RUN      ] Spec/CommonMarkSpecTest/042_Emphasis_and_strong_emphasis
[  FAILED  ] Spec/CommonMarkSpecTest/042_Emphasis_and_strong_emphasis

┌─ Section    : Emphasis and strong emphasis
│  Example #  : 42
│  Spec lines : 6749-6758
├─ Markdown input ──────────────────────────────────────────
*foo bar*
├─ Expected HTML ───────────────────────────────────────────
<p><em>foo bar</em></p>
├─ Actual HTML ─────────────────────────────────────────────
<p>*foo bar*</p>
└───────────────────────────────────────────────────────────
```

**A skipped known failure:**

```
[ RUN      ] Spec/CommonMarkSpecTest/087_Links
[  SKIPPED ] Spec/CommonMarkSpecTest/087_Links
  Example #87 not yet implemented (section: Links, lines 4512-4521)
```

---

## Implementation checklist

- [ ] Download `spec.json` and place it at `tests/spec/commonmark_spec.json`
- [ ] Add `tests/spec_tests/` directory with the files above
- [ ] Wire `add_subdirectory(spec_tests)` into `tests/CMakeLists.txt`
- [ ] Verify `nlohmann_json` fetches and links cleanly: `cmake --build build 2>&1 | grep -i json`
- [ ] Run `--gtest_list_tests` and confirm 652 test names are printed
- [ ] Run the full suite once to establish a baseline failure count
- [ ] Add the `spec` CTest label to CI so the suite runs on every push
