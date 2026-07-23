# GitHub workflows and automated testing

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `b1ad2adc-9250-4636-b1d0-c9620a3f705b`  
**Created:** 2026-06-03  
**Messages:** 4 (2 prompts)

**Related commits:**

- `0e2b8a7 add CI github workflow`
- `ceba43f Edit CI workflow`

---

## ▸ Prompt — 2026-06-03 13:56

> how does GitHub workflows work, can I run my tests with them?

GitHub Actions workflows let you automate tasks (like running tests) directly in your repository. Here's how they work:

## How GitHub Workflows Work

Workflows are YAML files stored in `.github/workflows/` in your repo. They define **when** to run and **what** to do.

**Core concepts:**

- **Trigger (on)** — what event starts the workflow (push, pull request, schedule, manual, etc.)
- **Jobs** — units of work that run on a virtual machine (they can run in parallel or sequentially)
- **Steps** — individual commands or actions within a job
- **Actions** — reusable plugins from the GitHub Marketplace (e.g. `actions/checkout`, `actions/setup-node`)

---

## Running Tests — Example

Here's a simple workflow that runs tests on every push and pull request:

```yaml
# .github/workflows/test.yml
name: Run Tests

on:
  push:
    branches: [main]
  pull_request:

jobs:
  test:
    runs-on: ubuntu-latest   # or windows-latest, macos-latest

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Set up Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Install dependencies
        run: npm ci

      - name: Run tests
        run: npm test
```

For Python it's similar — just swap the setup step:

```yaml
      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Install dependencies
        run: pip install -r requirements.txt

      - name: Run tests
        run: pytest
```

---

## Key Features for Testing

- **Matrix builds** — run tests across multiple OS/language versions simultaneously
- **Caching** — cache `node_modules`, pip packages, etc. to speed up runs
- **Test reporting** — upload results as artifacts or use marketplace actions to annotate PRs with failures
- **Secrets** — store API keys or credentials securely under *Settings → Secrets*
- **Status badges** — embed a pass/fail badge in your README

---

## Getting Started

1. Create the `.github/workflows/` folder in your repo
2. Add a `.yml` file (like the example above)
3. Push to GitHub — the workflow appears under the **Actions** tab
4. Each run shows logs per step, making it easy to debug failures

What language/framework are you using? I can give you a more tailored example.

## ▸ Prompt — 2026-06-03 13:57

> Is it feasible for cpp?

Yes, absolutely! C++ is well-supported on GitHub Actions. Here's what you need to know:

## Compiler Options

You can use any of the major compilers:
- **GCC / G++** — pre-installed on `ubuntu-latest`
- **Clang** — also pre-installed on Ubuntu
- **MSVC** — available on `windows-latest`

## Basic Example (CMake + CTest)

```yaml
# .github/workflows/test.yml
name: C++ Tests

on:
  push:
    branches: [main]
  pull_request:

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Install dependencies
        run: sudo apt-get install -y cmake build-essential

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build

      - name: Run tests
        run: ctest --test-dir build --output-on-failure
```

## Matrix Build (multiple compilers/OS)

A very common pattern for C++ — test across platforms and compilers at once:

```yaml
jobs:
  test:
    runs-on: ${{ matrix.os }}

    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
        compiler: [gcc, clang]
        exclude:
          - os: windows-latest
            compiler: clang  # optional: skip combos that don't make sense

    steps:
      - uses: actions/checkout@v4

      - name: Set compiler (Linux)
        if: matrix.compiler == 'clang'
        run: |
          sudo apt-get install -y clang
          echo "CC=clang" >> $GITHUB_ENV
          echo "CXX=clang++" >> $GITHUB_ENV

      - name: Configure & Build
        run: |
          cmake -B build
          cmake --build build

      - name: Test
        run: ctest --test-dir build --output-on-failure
```

## Popular C++ Testing Frameworks

All of these work great with GitHub Actions:

| Framework | Notes |
|---|---|
| **Google Test (gtest)** | Most widely used, integrates natively with CMake |
| **Catch2** | Header-only, easy setup |
| **Boost.Test** | Good if you already use Boost |
| **doctest** | Lightweight, single header |

## Google Test Example

If you use `FetchContent` in CMake to pull in gtest, it just works out of the box — no extra setup needed in the workflow:

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(my_tests test_main.cpp)
target_link_libraries(my_tests gtest_main)
add_test(NAME MyTests COMMAND my_tests)
```

Then `ctest` in the workflow picks it up automatically.

## Tips

- **Cache your build directory** with `actions/cache` to avoid full recompiles on every push
- **`--output-on-failure`** in ctest prints test output only when something breaks, keeping logs clean
- **Sanitizers** (ASan, UBSan) are easy to add — just pass `-DCMAKE_CXX_FLAGS="-fsanitize=address"` to CMake for extra coverage

What build system and test framework are you using (or planning to use)?

