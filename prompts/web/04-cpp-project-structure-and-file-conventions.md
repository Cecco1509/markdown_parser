# C++ project structure and file conventions

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `e3f4db64-0d82-4a3c-9a85-c2abeb53d7a9`  
**Created:** 2026-06-02  
**Messages:** 18 (9 prompts)

**Related commits:**

- `834f683 Project structure initialization based on docs/01_project_structure.md`
- `25b78a8 add CommonMark 31.2 test suite verification`

---

## ▸ Prompt — 2026-06-02 14:19

> What is the typical structure of a cpp project, what are the conventions for .hpp and .cpp files, for testing and also for the project building such as makefile or Cmake

## C++ Project Structure

Here's a typical modern C++ project layout:

```
my_project/
├── CMakeLists.txt          # Top-level build config
├── Makefile                # Optional, sometimes wraps CMake
├── README.md
├── .gitignore
│
├── include/                # Public headers (.hpp)
│   └── my_project/
│       ├── foo.hpp
│       └── bar.hpp
│
├── src/                    # Implementation files (.cpp)
│   ├── main.cpp
│   ├── foo.cpp
│   └── bar.cpp
│
├── tests/                  # Test files
│   ├── CMakeLists.txt
│   ├── test_foo.cpp
│   └── test_bar.cpp
│
├── libs/ or third_party/   # External dependencies
│   └── googletest/
│
├── build/                  # Generated build artifacts (git-ignored)
│
└── docs/                   # Documentation
```

---

## `.hpp` vs `.cpp` Conventions

**Headers (`.hpp`)** declare the interface — what exists:
- Class declarations
- Function signatures
- Templates (full definition must be here)
- `inline` functions
- Constants and `using` aliases

```cpp
// include/my_project/math_utils.hpp
#pragma once  // preferred over include guards

namespace myproject {

class MathUtils {
public:
    static int add(int a, int b);
    static double square(double x);  // inline candidate

    template<typename T>
    static T clamp(T val, T lo, T hi) {  // must be defined here
        return val < lo ? lo : val > hi ? hi : val;
    }
};

} // namespace myproject
```

**Source files (`.cpp`)** contain the implementation — how it works:

```cpp
// src/math_utils.cpp
#include "my_project/math_utils.hpp"

namespace myproject {

int MathUtils::add(int a, int b) {
    return a + b;
}

double MathUtils::square(double x) {
    return x * x;
}

} // namespace myproject
```

Key rules:
- Always use `#pragma once` (or include guards `#ifndef MY_HEADER_HPP`)
- Headers include only what they need; prefer forward declarations
- `.cpp` files include their own header first
- One class per file, filename matches class name

---

## Testing

The most common frameworks are **Google Test**, **Catch2**, and **doctest**.

**Google Test** (most widely used in larger projects):

```cpp
// tests/test_math_utils.cpp
#include <gtest/gtest.h>
#include "my_project/math_utils.hpp"

using namespace myproject;

TEST(MathUtilsTest, AddPositiveNumbers) {
    EXPECT_EQ(MathUtils::add(2, 3), 5);
}

TEST(MathUtilsTest, AddNegativeNumbers) {
    EXPECT_EQ(MathUtils::add(-1, -1), -2);
}

TEST(MathUtilsTest, ClampValue) {
    EXPECT_EQ(MathUtils::clamp(10, 0, 5), 5);
    EXPECT_EQ(MathUtils::clamp(3, 0, 5), 3);
}
```

**Catch2** (header-only, great for smaller projects):

```cpp
// tests/test_math_utils.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "my_project/math_utils.hpp"

TEST_CASE("MathUtils::add", "[math]") {
    REQUIRE(myproject::MathUtils::add(2, 3) == 5);

    SECTION("negative numbers") {
        REQUIRE(myproject::MathUtils::add(-1, -1) == -2);
    }
}
```

---

## Build Systems

### CMake (industry standard)

**Top-level `CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_project VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library target (reusable by main and tests)
add_library(my_lib
    src/foo.cpp
    src/bar.cpp
)
target_include_directories(my_lib PUBLIC include)

# Executable
add_executable(my_project src/main.cpp)
target_link_libraries(my_project PRIVATE my_lib)

# Tests
enable_testing()
add_subdirectory(tests)
```

**`tests/CMakeLists.txt`:**

```cmake
find_package(GTest REQUIRED)

add_executable(run_tests
    test_foo.cpp
    test_bar.cpp
)
target_link_libraries(run_tests PRIVATE my_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(run_tests)
```

**Building with CMake:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build                          # compile
ctest --test-dir build                       # run tests
```

### Makefile

Good for smaller projects or wrapping CMake commands:

```makefile
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude
LDFLAGS  :=

SRC   := $(wildcard src/*.cpp)
OBJ   := $(SRC:src/%.cpp=build/%.o)
BIN   := build/my_project

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

test: all
	cd build && ctest --output-on-failure

clean:
	rm -rf build
```

---

## Quick Reference Summary

| Concern | Convention |
|---|---|
| Public API | `include/<project>/foo.hpp` |
| Implementation | `src/foo.cpp` |
| Header guard | `#pragma once` |
| Namespaces | Mirror directory structure |
| Test files | `tests/test_foo.cpp` |
| Test framework | Google Test or Catch2 |
| Build system | CMake for anything non-trivial |
| Build dir | `build/` — always git-ignored |
| C++ standard | C++17 or C++20 minimum today |

CMake is the clear default for any serious project — it handles cross-platform builds, dependency discovery, and IDE integration (VS Code, CLion, etc.) far better than raw Makefiles.

## ▸ Prompt — 2026-06-02 14:25

> how to use namespaces properly, which are the good practices?

## C++ Namespaces — Good Practices

### Basic Structure

Namespaces should mirror your directory/module structure:

```cpp
// include/myproject/net/socket.hpp
namespace myproject::net {   // C++17 nested syntax

class Socket { ... };

}
```

```cpp
// include/myproject/utils/string.hpp
namespace myproject::utils {

std::string trim(std::string_view s);

}
```

---

### ✅ Do's

**1. Use a top-level project namespace for everything**
```cpp
namespace myproject {
    class Foo { ... };
    void bar();
}
```

**2. Nest namespaces to reflect modules (C++17 syntax)**
```cpp
namespace myproject::audio::codec {
    class Mp3Decoder { ... };
}
```

**3. Use anonymous namespaces instead of `static` for file-local symbols**
```cpp
// src/foo.cpp
namespace {
    // Only visible in this translation unit — replaces C-style `static`
    int helperFunction(int x) { return x * 2; }
    const int INTERNAL_CONSTANT = 42;
}
```

**4. Use namespace aliases to shorten deep paths**
```cpp
#include "myproject/audio/codec/mp3.hpp"

namespace codec = myproject::audio::codec;  // alias
codec::Mp3Decoder decoder;
```

**5. Use `inline namespace` for versioning/ABI**
```cpp
namespace myproject {
    inline namespace v2 {     // v2 is the default
        class Foo { ... };
    }
    namespace v1 {            // old version still accessible explicitly
        class Foo { ... };
    }
}

myproject::Foo f;        // uses v2 automatically
myproject::v1::Foo old; // explicit fallback
```

**6. Close namespaces with a comment**
```cpp
namespace myproject::net {

class Socket { ... };

} // namespace myproject::net
```

---

### ❌ Don'ts

**1. Never `using namespace std` in a header** — it pollutes every file that includes it
```cpp
// ❌ NEVER in a header
using namespace std;

// ✅ OK only in .cpp files, and even then use sparingly
// ✅ Prefer specific imports
using std::string;
using std::vector;
```

**2. Never put `using namespace` at file scope in a .cpp if avoidable** — prefer scoping it
```cpp
// ❌ Pollutes the entire file
using namespace myproject::utils;

void foo() { trim("hello"); }
void bar() { trim("world"); }

// ✅ Scope it where you need it
void foo() {
    using namespace myproject::utils;
    trim("hello");
}
```

**3. Never define things in `namespace std`** (except explicit specializations)
```cpp
// ❌ Undefined behavior
namespace std {
    void myHelper() { ... }
}

// ✅ The one exception: template specializations
namespace std {
    template<>
    struct hash<myproject::MyType> { ... };  // OK
}
```

**4. Avoid deeply nesting just for the sake of it**
```cpp
// ❌ Overkill
namespace myproject::utils::string::internal::helpers::v1 { ... }

// ✅ Flat where possible — 2-3 levels max
namespace myproject::utils { ... }
```

---

### Namespace vs. Class for Grouping

A common question — use namespaces for grouping free functions, not classes with only static members:

```cpp
// ❌ Anti-pattern: static-only class used as a namespace
class StringUtils {
public:
    static std::string trim(std::string_view s);
    static bool startsWith(std::string_view s, std::string_view prefix);
};

// ✅ Prefer a namespace with free functions
namespace myproject::utils {
    std::string trim(std::string_view s);
    bool startsWith(std::string_view s, std::string_view prefix);
}
```

---

### ADL — Argument Dependent Lookup

Understanding ADL is key to placing things correctly in namespaces. Functions are found in the namespace of their arguments automatically:

```cpp
namespace myproject {

struct Vec2 { float x, y; };

// Place operator+ in the same namespace as Vec2
Vec2 operator+(const Vec2& a, const Vec2& b) {
    return { a.x + b.x, a.y + b.y };
}

// swap should also live here so std::sort and others can find it
void swap(Vec2& a, Vec2& b) noexcept { ... }

} // namespace myproject

// ADL finds myproject::operator+ automatically:
myproject::Vec2 a{1,0}, b{0,1};
auto c = a + b;   // works — no explicit namespace needed
```

---

### Summary Table

| Practice | Rule |
|---|---|
| Top-level namespace | Always wrap your code in one |
| Nesting | Mirror module/directory structure, max 2–3 levels |
| Anonymous namespace | Use instead of `static` for file-local symbols |
| `using namespace` in headers | **Never** |
| `using namespace std` | Avoid; prefer `using std::vector` etc. |
| Namespace aliases | Use to shorten deep paths in `.cpp` files |
| Free functions vs static classes | Prefer free functions in a namespace |
| Closing brace comment | Always: `} // namespace myproject::net` |
| Versioning | Use `inline namespace vN` for ABI stability |
| ADL | Put operators and `swap` in the same namespace as their types |

## ▸ Prompt — 2026-06-02 14:29

> Can you do an example of a proper usage of namespaces?

Let me build a small but realistic project — a simple **2D geometry library** with a clean namespace structure.
Here's the full project — a small 2D geometry library with `shapes`, `utils`, and `ops` sub-namespaces, then a diagram of the structure, followed by all the code files.
Now here's the full project with every good practice in place:

---

### `include/geo/shapes.hpp` — type definitions
```cpp
#pragma once

namespace geo::shapes {

struct Point {
    double x{0.0};
    double y{0.0};
};

struct Circle {
    Point  center;
    double radius{1.0};
};

struct Rect {
    Point  origin;   // top-left
    double width{1.0};
    double height{1.0};
};

// ADL: operator== lives in the same namespace as its types
bool operator==(const Point& a,  const Point& b)  noexcept;
bool operator==(const Circle& a, const Circle& b) noexcept;
bool operator==(const Rect& a,   const Rect& b)   noexcept;

} // namespace geo::shapes
```

---

### `include/geo/utils.hpp` — math helpers
```cpp
#pragma once

namespace geo::utils {

constexpr double PI = 3.14159265358979323846;

// Floating-point comparison with a tolerance
bool approxEqual(double a, double b, double epsilon = 1e-9) noexcept;

constexpr double toRadians(double degrees) noexcept {
    return degrees * (PI / 180.0);
}

constexpr double toDegrees(double radians) noexcept {
    return radians * (180.0 / PI);
}

} // namespace geo::utils
```

---

### `include/geo/ops.hpp` — geometry operations
```cpp
#pragma once
#include "geo/shapes.hpp"   // include by project-relative path

namespace geo::ops {

// Free functions — preferred over a static utility class
double area      (const geo::shapes::Circle& c) noexcept;
double area      (const geo::shapes::Rect&   r) noexcept;

double perimeter (const geo::shapes::Circle& c) noexcept;
double perimeter (const geo::shapes::Rect&   r) noexcept;

double distance  (const geo::shapes::Point& a,
                  const geo::shapes::Point& b) noexcept;

bool   contains  (const geo::shapes::Rect&   r,
                  const geo::shapes::Point&  p) noexcept;

} // namespace geo::ops
```

---

### `include/geo/geo.hpp` — convenience umbrella header
```cpp
#pragma once

// Users can include this single header to get everything,
// or include sub-headers individually for faster compile times.
#include "geo/shapes.hpp"
#include "geo/utils.hpp"
#include "geo/ops.hpp"
```

---

### `src/shapes.cpp` — operators implementation
```cpp
#include "geo/shapes.hpp"
#include "geo/utils.hpp"   // for approxEqual

namespace geo::shapes {

bool operator==(const Point& a, const Point& b) noexcept {
    return geo::utils::approxEqual(a.x, b.x)
        && geo::utils::approxEqual(a.y, b.y);
}

bool operator==(const Circle& a, const Circle& b) noexcept {
    return a.center == b.center
        && geo::utils::approxEqual(a.radius, b.radius);
}

bool operator==(const Rect& a, const Rect& b) noexcept {
    return a.origin == b.origin
        && geo::utils::approxEqual(a.width,  b.width)
        && geo::utils::approxEqual(a.height, b.height);
}

} // namespace geo::shapes
```

---

### `src/utils.cpp`
```cpp
#include "geo/utils.hpp"
#include <cmath>

namespace geo::utils {

bool approxEqual(double a, double b, double epsilon) noexcept {
    return std::abs(a - b) < epsilon;
}

} // namespace geo::utils
```

---

### `src/ops.cpp`
```cpp
#include "geo/ops.hpp"
#include "geo/utils.hpp"
#include <cmath>

namespace geo::ops {

// Anonymous namespace — these helpers are invisible outside this .cpp
namespace {
    double sq(double x) noexcept { return x * x; }
} // anonymous namespace

double area(const geo::shapes::Circle& c) noexcept {
    return geo::utils::PI * sq(c.radius);
}

double area(const geo::shapes::Rect& r) noexcept {
    return r.width * r.height;
}

double perimeter(const geo::shapes::Circle& c) noexcept {
    return 2.0 * geo::utils::PI * c.radius;
}

double perimeter(const geo::shapes::Rect& r) noexcept {
    return 2.0 * (r.width + r.height);
}

double distance(const geo::shapes::Point& a,
                const geo::shapes::Point& b) noexcept {
    return std::sqrt(sq(b.x - a.x) + sq(b.y - a.y));
}

bool contains(const geo::shapes::Rect& r,
              const geo::shapes::Point& p) noexcept {
    return p.x >= r.origin.x && p.x <= r.origin.x + r.width
        && p.y >= r.origin.y && p.y <= r.origin.y + r.height;
}

} // namespace geo::ops
```

---

### `src/main.cpp` — demonstrating usage
```cpp
#include "geo/geo.hpp"
#include <iostream>

int main() {
    // Namespace alias — shortens deep paths in .cpp files only
    namespace shapes = geo::shapes;
    namespace ops    = geo::ops;

    shapes::Circle c{ {0.0, 0.0}, 5.0 };
    shapes::Rect   r{ {1.0, 1.0}, 4.0, 3.0 };
    shapes::Point  p{ 2.0, 2.0 };

    std::cout << "Circle area:      " << ops::area(c)      << "\n";
    std::cout << "Rect perimeter:   " << ops::perimeter(r) << "\n";
    std::cout << "Rect contains p:  " << ops::contains(r, p) << "\n";

    // ADL: operator== found via argument type's namespace (geo::shapes)
    shapes::Circle c2 = c;
    std::cout << "Circles equal:    " << (c == c2) << "\n";
}
```

---

### `tests/test_ops.cpp` — Google Test
```cpp
#include <gtest/gtest.h>
#include "geo/geo.hpp"

// Scoped using — only inside this test file
using namespace geo::shapes;
using namespace geo::ops;
using geo::utils::approxEqual;

TEST(OpsTest, CircleArea) {
    Circle c{ {0, 0}, 1.0 };
    EXPECT_TRUE(approxEqual(area(c), 3.14159265358979, 1e-6));
}

TEST(OpsTest, RectPerimeter) {
    Rect r{ {0, 0}, 4.0, 3.0 };
    EXPECT_DOUBLE_EQ(perimeter(r), 14.0);
}

TEST(OpsTest, DistancePythagoras) {
    Point a{0, 0}, b{3, 4};
    EXPECT_DOUBLE_EQ(distance(a, b), 5.0);
}

TEST(OpsTest, ContainsPoint) {
    Rect r{ {0, 0}, 10.0, 10.0 };
    EXPECT_TRUE (contains(r, {5.0, 5.0}));
    EXPECT_FALSE(contains(r, {11.0, 5.0}));
}

TEST(ShapesTest, EqualityOperatorADL) {
    Circle a{ {0, 0}, 5.0 };
    Circle b{ {0, 0}, 5.0 };
    Circle c{ {0, 0}, 6.0 };
    // operator== found by ADL — no explicit geo::shapes:: needed
    EXPECT_TRUE (a == b);
    EXPECT_FALSE(a == c);
}
```

---

### What each practice demonstrates

| File / snippet | Practice shown |
|---|---|
| `geo::shapes`, `geo::ops`, `geo::utils` | Top-level project namespace + sub-namespaces mirroring directory layout |
| `geo.hpp` umbrella header | Optional single include; sub-headers available separately |
| `operator==` in `geo::shapes` | ADL — operator found automatically via argument type |
| `namespace { sq() }` in `ops.cpp` | Anonymous namespace instead of `static` for file-local helpers |
| `namespace shapes = geo::shapes` in `main.cpp` | Alias scoped to `.cpp`, never in headers |
| `using namespace` only inside `main()` or test file scope | Avoiding header pollution |
| Free functions in `geo::ops` instead of `struct OpsUtils` | Namespaces for grouping, not static-only classes |
| `} // namespace geo::ops` closing comments | Clarity on large files |

## ▸ Prompt — 2026-06-02 14:38

> Can you explain this cmake file?

Sure! Here's a breakdown of each section:

---

### Project setup

```cmake
cmake_minimum_required(VERSION 3.20)
```
Declares the minimum CMake version needed. This protects against running with an older CMake that might not support the features you use.

```cmake
project(my_project VERSION 1.0 LANGUAGES CXX)
```
Names the project, sets a version (accessible later as `my_project_VERSION`), and tells CMake this is a C++ project so it finds the right compiler.

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```
Enforces C++17. `REQUIRED ON` makes the build **fail** if the compiler doesn't support it — without it, CMake silently falls back to an older standard.

---

### Library target

```cmake
add_library(my_lib
    src/foo.cpp
    src/bar.cpp
)
```
Compiles `foo.cpp` and `bar.cpp` into a static library called `my_lib` (static by default). This separates your logic from `main.cpp` so both the executable and tests can link against it without recompiling.

```cmake
target_include_directories(my_lib PUBLIC include)
```
Tells CMake where the headers live. `PUBLIC` is the key word here — it means:
- `my_lib` itself can find headers in `include/`
- **any target that links `my_lib` also automatically gets `include/`** added to its include path

So neither `my_project` nor the tests need to manually specify `include/` themselves.

---

### Executable

```cmake
add_executable(my_project src/main.cpp)
target_link_libraries(my_project PRIVATE my_lib)
```
Compiles `main.cpp` into the final binary and links `my_lib` into it. `PRIVATE` means this dependency isn't propagated further — nothing that depends on `my_project` (if anything did) would inherit `my_lib`.

---

### Tests

```cmake
enable_testing()
add_subdirectory(tests)
```
`enable_testing()` activates the `ctest` runner for this project. `add_subdirectory(tests)` tells CMake to go into the `tests/` folder and process its own `CMakeLists.txt`, which is where the test executable and `gtest_discover_tests()` would be defined.

---

### The `PUBLIC` / `PRIVATE` / `INTERFACE` distinction

This is the most important concept in modern CMake and worth internalising:

| Keyword | Who gets the setting |
|---|---|
| `PRIVATE` | Only this target |
| `PUBLIC` | This target **and** anything linking it |
| `INTERFACE` | Only things linking it (not the target itself) |

A library uses `PUBLIC` for its include dirs (consumers need the headers) but `PRIVATE` for its own internal compile flags. An executable almost always uses `PRIVATE` since nothing links against it.

## ▸ Prompt — 2026-06-02 14:45

> How integration testing is done?

Integration testing in C++ tests how **multiple components work together**, as opposed to unit tests which test a single class/function in isolation.

---

### The difference in practice

```
Unit test:    test Circle::area() in isolation — mock nothing, no dependencies
Integration: test that ops::area() + the full shapes pipeline gives correct end-to-end results
System test: compile and run the whole binary, check stdout/exit code
```

---

### With Google Test (most common approach)

Integration tests live in their own folder and link against the real library — no mocks:

```
tests/
├── unit/
│   ├── CMakeLists.txt
│   ├── test_ops.cpp        ← tests one function at a time
│   └── test_utils.cpp
├── integration/
│   ├── CMakeLists.txt
│   └── test_geometry_pipeline.cpp   ← tests components working together
└── CMakeLists.txt
```

**`tests/integration/test_geometry_pipeline.cpp`:**
```cpp
#include <gtest/gtest.h>
#include "geo/geo.hpp"

// Integration test: shapes + ops + utils all working together
// No mocks — uses the real implementations
TEST(GeoPipelineTest, CircleRoundtrip) {
    geo::shapes::Circle c{ {0.0, 0.0}, 5.0 };

    double a = geo::ops::area(c);
    double p = geo::ops::perimeter(c);

    // Derived relationship between area and perimeter must hold
    // area = PI*r^2,  perimeter = 2*PI*r  =>  area = p*r/2
    EXPECT_TRUE(geo::utils::approxEqual(a, p * c.radius / 2.0, 1e-9));
}

TEST(GeoPipelineTest, PointDistanceAndContainment) {
    geo::shapes::Rect  r{ {0.0, 0.0}, 10.0, 10.0 };
    geo::shapes::Point center{ 5.0, 5.0 };
    geo::shapes::Point corner{ 0.0, 0.0 };
    geo::shapes::Point outside{ 11.0, 11.0 };

    // distance + contains working together
    double d = geo::ops::distance(center, corner);
    EXPECT_TRUE (geo::ops::contains(r, center));
    EXPECT_TRUE (geo::ops::contains(r, corner));   // edge is included
    EXPECT_FALSE(geo::ops::contains(r, outside));
    EXPECT_TRUE (geo::utils::approxEqual(d, std::sqrt(50.0), 1e-9));
}
```

**`tests/integration/CMakeLists.txt`:**
```cmake
add_executable(integration_tests
    test_geometry_pipeline.cpp
)

# Links against the REAL library — no mocks
target_link_libraries(integration_tests PRIVATE geo_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(integration_tests)
```

---

### Separating unit vs integration in CMake

You can tag tests so you can run them separately:

```cmake
# Run only unit tests
ctest --test-dir build -L unit

# Run only integration tests  
ctest --test-dir build -L integration

# Run everything
ctest --test-dir build
```

To make this work, label your tests:

```cmake
# In tests/unit/CMakeLists.txt
gtest_discover_tests(unit_tests PROPERTIES LABELS "unit")

# In tests/integration/CMakeLists.txt
gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
```

---

### When you need external dependencies (DB, HTTP, files)

Real integration tests often need real external resources. The common patterns are:

**1. Test fixtures — set up and tear down real state:**
```cpp
class DatabaseIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Spin up a real (or in-memory) DB before each test
        db = std::make_unique<Database>(":memory:");
        db->runMigrations();
    }

    void TearDown() override {
        db.reset();   // clean state between tests
    }

    std::unique_ptr<Database> db;
};

TEST_F(DatabaseIntegrationTest, SaveAndRetrieveShape) {
    geo::shapes::Circle c{ {1.0, 2.0}, 3.0 };
    db->save(c);

    auto loaded = db->loadCircle(1);
    EXPECT_TRUE(loaded == c);   // ADL finds operator==
}
```

**2. Test doubles — only mock the I/O boundary, not internal logic:**
```cpp
// Mock only the HTTP client, not the business logic
class MockHttpClient : public IHttpClient {
public:
    MOCK_METHOD(std::string, get, (const std::string& url), (override));
};

TEST(ApiIntegrationTest, FetchAndParseShapes) {
    MockHttpClient mock;
    EXPECT_CALL(mock, get("https://api.example.com/shapes"))
        .WillOnce(Return(R"([{"type":"circle","radius":5}])"));

    ShapeService service(mock);   // real service, fake HTTP
    auto shapes = service.fetchAll();
    EXPECT_EQ(shapes.size(), 1);
}
```

---

### Typical test pyramid

```
        /\
       /  \    E2E / system tests
      /----\   (few, slow, full binary)
     /      \
    /--------\  Integration tests
   /          \ (moderate, real components)
  /------------\
 /              \ Unit tests
/________________\ (many, fast, isolated)
```

In practice for a C++ library like `geo`:

| Layer | What it tests | Speed | Count |
|---|---|---|---|
| Unit | `area(circle)` alone | ~ms | Many |
| Integration | `area` + `utils` + `shapes` together | ~ms–s | Moderate |
| System | Full binary, check output/exit code | ~s | Few |

---

### Running system-level tests with CTest directly

For the very top of the pyramid — testing the compiled binary itself:

```cmake
# In tests/CMakeLists.txt
add_test(
    NAME system_circle_output
    COMMAND $<TARGET_FILE:my_project> --shape circle --radius 5
)

# Assert the binary exits cleanly and prints expected output
set_tests_properties(system_circle_output PROPERTIES
    PASS_REGULAR_EXPRESSION "area: 78.539"
    LABELS "system"
)
```

This is the simplest form of system testing — no test framework needed, just CTest checking the process exit code and stdout.

## ▸ Prompt — 2026-06-02 14:51

> For my markdown parser I have a single file provided by CommonMark which consists in a long list of test cases, what is the best approach to cover it?
>
> I would like to know for each test if it passed or not and if not what was the expected and what instead was the actual

This is a perfect use case for **parameterized tests** in Google Test. You parse the CommonMark spec file once, generate one test case per entry automatically, and get a detailed per-test report.

---

### The CommonMark spec format

The spec file uses this pattern:

```
```````````````````````````````` example
*foo*
.
<p><em>foo</em></p>
````````````````````````````````
```

So the parser needs to split on the delimiter, separate markdown from expected HTML at the `.` line.

---

### Step 1 — Parser for the spec file

```cpp
// tests/common_mark_spec.hpp
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

struct SpecCase {
    int         number;     // sequential index for identification
    std::string section;    // e.g. "Emphasis and strong emphasis"
    std::string markdown;
    std::string expected_html;
};

inline std::vector<SpecCase> loadSpec(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open spec file: " + path);

    std::vector<SpecCase> cases;
    std::string line;
    std::string current_section;
    int case_number = 0;

    enum class State { Scanning, InMarkdown, InHtml };
    State state = State::Scanning;

    SpecCase current;

    while (std::getline(file, line)) {
        // Track section headings (CommonMark spec uses ## headings)
        if (line.rfind("## ", 0) == 0) {
            current_section = line.substr(3);
            continue;
        }

        if (line == "```````````````````````````````` example") {
            state = State::InMarkdown;
            current = SpecCase{};
            current.section = current_section;
            current.number  = ++case_number;
            continue;
        }

        if (state == State::InMarkdown && line == ".") {
            state = State::InHtml;
            continue;
        }

        if (state == State::InHtml &&
            line == "````````````````````````````````") {
            cases.push_back(std::move(current));
            state = State::Scanning;
            continue;
        }

        if (state == State::InMarkdown)
            current.markdown     += line + "\n";
        else if (state == State::InHtml)
            current.expected_html += line + "\n";
    }

    return cases;
}
```

---

### Step 2 — Parameterized Google Test

```cpp
// tests/test_commonmark_spec.cpp
#include <gtest/gtest.h>
#include "common_mark_spec.hpp"
#include "myparser/parser.hpp"   // your markdown parser

// -- Test fixture ------------------------------------------------------------

class CommonMarkTest : public ::testing::TestWithParam<SpecCase> {};

TEST_P(CommonMarkTest, MatchesExpectedHtml) {
    const SpecCase& tc = GetParam();

    std::string actual = myparser::parse(tc.markdown);

    EXPECT_EQ(tc.expected_html, actual)
        << "\n--- SECTION ---\n" << tc.section
        << "\n--- CASE #" << tc.number << " ---"
        << "\n--- MARKDOWN INPUT ---\n" << tc.markdown
        << "\n--- EXPECTED HTML ---\n" << tc.expected_html
        << "\n--- ACTUAL HTML ---\n" << actual;
}

// -- Load all cases at startup -----------------------------------------------

// SPEC_FILE is injected by CMake via a compile definition
static const std::vector<SpecCase> kAllCases =
    loadSpec(SPEC_FILE_PATH);

INSTANTIATE_TEST_SUITE_P(
    Spec,
    CommonMarkTest,
    ::testing::ValuesIn(kAllCases),

    // Human-readable test name: "Spec/CommonMarkTest/42_Emphasis_and_strong"
    [](const ::testing::TestParamInfo<SpecCase>& info) {
        std::string name = std::to_string(info.param.number)
                         + "_"
                         + info.param.section.substr(0, 30);
        // GTest names can't have spaces or special chars
        for (char& c : name)
            if (!std::isalnum(c)) c = '_';
        return name;
    }
);
```

---

### Step 3 — CMake wires the spec file path in

```cmake
# tests/CMakeLists.txt
find_package(GTest REQUIRED)

add_executable(spec_tests
    test_commonmark_spec.cpp
)

target_link_libraries(spec_tests PRIVATE my_parser_lib GTest::gtest_main)

# Inject the spec file path at compile time — no hardcoded paths in code
target_compile_definitions(spec_tests PRIVATE
    SPEC_FILE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spec/commonmark_spec.txt"
)

include(GoogleTest)
gtest_discover_tests(spec_tests PROPERTIES LABELS "spec")
```

---

### What the output looks like

On a passing run:
```
[----------] 652 tests from Spec/CommonMarkTest
[ RUN      ] Spec/CommonMarkTest/1_Tabs
[       OK ] Spec/CommonMarkTest/1_Tabs (0 ms)
[ RUN      ] Spec/CommonMarkTest/2_Tabs
[       OK ] Spec/CommonMarkTest/2_Tabs (0 ms)
...
[  PASSED  ] 649 tests
[  FAILED  ] 3 tests
```

On a failure:
```
[ RUN      ] Spec/CommonMarkTest/42_Emphasis_and_strong_emphasis
[  FAILED  ] Spec/CommonMarkTest/42_Emphasis_and_strong_emphasis

SECTION: Emphasis and strong emphasis
CASE #42
MARKDOWN INPUT:
*foo bar*

EXPECTED HTML:
<p><em>foo bar</em></p>

ACTUAL HTML:
<p>*foo bar*</p>
```

---

### Useful run commands

```bash
# Run the full spec suite
ctest --test-dir build -L spec

# Run only failing tests (re-run after a fix)
ctest --test-dir build -L spec --rerun-failed

# Filter to a specific section by name pattern
./build/spec_tests --gtest_filter="*Emphasis*"

# Stop after first failure (useful during active development)
./build/spec_tests --gtest_break_on_failure

# Get a summary of all failures at the end
./build/spec_tests --gtest_print_time=1 2>&1 | grep FAILED
```

---

### Optional — skip known failures cleanly

While your parser is incomplete you can mark specific cases as expected failures rather than letting them pollute the output:

```cpp
TEST_P(CommonMarkTest, MatchesExpectedHtml) {
    const SpecCase& tc = GetParam();

    // Known unimplemented sections — tracked, not ignored silently
    static const std::set<int> kKnownFailures = { 42, 87, 103 };
    if (kKnownFailures.count(tc.number)) {
        GTEST_SKIP() << "Case #" << tc.number
                     << " not yet implemented (section: " << tc.section << ")";
    }

    std::string actual = myparser::parse(tc.markdown);
    EXPECT_EQ(tc.expected_html, actual);
}
```

This way skipped tests show up as `SKIPPED` in the report — clearly tracked, not silently passing.

## ▸ Prompt — 2026-06-03 10:09

> Create a .md file with detailed specification to implement the testing feature. Which has to use the list of test cases. Remember I have a json file, for json I'd like to use nlohmann json library. Also adjust the SpecCase struct to math the cases structure in 
>
> https://spec.commonmark.org/0.31.2/spec.json

The JSON structure is clear. Each entry has `markdown`, `html`, `example`, `start_line`, `end_line`, and `section`. Now I'll write the spec.
The spec covers everything end-to-end. A few things worth highlighting:

**The `SpecCase` struct** maps exactly to the JSON fields — `markdown`, `html`, `example`, `start_line`, `end_line`, `section`. The `from_json` free function lets nlohmann deserialize the entire array into `std::vector<SpecCase>` in a single call.

**The test name format** zero-pads the example number (`001_Tabs`, `042_Emphasis_and_strong_emphasis`) so that `--gtest_filter="*042*"` always finds exactly one test, and the output sorts stably by number.

**The `kKnownFailures` set** is the key workflow tool while you're building the parser — it keeps deferred cases visible as `SKIPPED` rather than silently excluded, so you always know exactly how many cases remain unimplemented.

**nlohmann/json via FetchContent** means no manual install step; CMake fetches and builds it automatically on first configure.

## ▸ Prompt — 2026-06-03 12:24

> What is std::monostate?

`std::monostate` is a simple empty struct defined in `<variant>`, designed specifically to be used as the first type in a `std::variant` to give it a "valueless" or "empty" state.

---

### The problem it solves

`std::variant` always holds exactly one of its types, and by default it must hold a value — it has no built-in "null" state. Also, every type in the variant must be default-constructible for `variant` itself to be default-constructible. `std::monostate` solves both of these:

```cpp
#include <variant>

// Won't compile if any type lacks a default constructor
struct NoDefault {
    NoDefault(int x) {}  // no default constructor
};

// ❌ Can't be default-constructed — which state would it start in?
std::variant<NoDefault, int> v;

// ✅ Monostate provides a default "empty" state
std::variant<std::monostate, NoDefault, int> v;  // starts as monostate
```

---

### Common use cases

**1. Optional variant — representing "no value"** without using `std::optional`:

```cpp
using Result = std::variant<std::monostate, std::string, int>;

Result parse(const std::string& input) {
    if (input.empty())  return std::monostate{};  // no result
    if (input == "42")  return 42;
    return input;
}

Result r = parse("");
if (std::holds_alternative<std::monostate>(r)) {
    // nothing was parsed
}
```

**2. State machines** — `monostate` as the clean initial/uninitialized state:

```cpp
struct Idle {};
struct Connecting { std::string host; };
struct Connected  { int socket_fd; };
struct Error      { std::string message; };

using State = std::variant<std::monostate, Idle, Connecting, Connected, Error>;

State current;  // starts as monostate — not yet initialized
current = Idle{};
current = Connecting{"example.com"};
```

**3. Lazy initialization** — the variant can exist before it's ready to hold real data:

```cpp
class Parser {
    std::variant<std::monostate, Lexer, AstBuilder> phase;
public:
    Parser() = default;  // monostate — nothing running yet

    void start() { phase = Lexer{}; }
};
```

---

### Checking for monostate

```cpp
std::variant<std::monostate, int, std::string> v;

// Three equivalent ways to check
if (std::holds_alternative<std::monostate>(v)) { ... }
if (v.index() == 0) { ... }
if (std::get_if<std::monostate>(&v) != nullptr) { ... }
```

---

### `monostate` is comparable and hashable

It's fully ordered and all instances compare equal, which makes variants containing it sortable and usable in maps/sets:

```cpp
std::monostate a, b;
a == b;  // true — all monostates are equal
a < b;   // false
```

---

### Contrast with `std::optional`

| | `std::optional<T>` | `std::variant<std::monostate, ...>` |
|---|---|---|
| Holds one type or nothing | ✅ | ✅ |
| Holds one of several types | ❌ | ✅ |
| Overhead | Minimal | Slightly more |
| Use when | Single optional type | Multiple possible types, one being "empty" |

In short — if you just need "a value or nothing", use `std::optional`. If you need "one of several types, possibly nothing", `std::variant` with `std::monostate` as the first alternative is the right tool.

## ▸ Prompt — 2026-06-06 18:17

> How to shrink the test results line to something more readable?
>
> [  FAILED  ] Spec/CommonMarkSpecTest.MatchesExpectedHtml/006_Tabs, where GetParam() = 88-byte object <3E-09 09-66 6F-6F 0A-00 00-00 00-00 00-00 00-00 00-00 00-00 00-00 00-07 00-B8 00-24 07-00 00-00 3A-00 00-00 00-00 00-00 40-00 00-00 00-00 00-80 06-00 00-00 A2-01 00-00 A9-01 00-00 00-00 00-00 54-61 62-73 00-00 00-00 00-00 00-00 00-00 00-00 00-00 00-00-00-00 00-04> (0 ms)
>
> Also this line is always different from the previous test which makes difficult to track actual changes with diffs

That raw hex dump is GTest's default for unknown types. Fix both problems by adding a `PrintTo` function — GTest calls it automatically when it needs to print your type.

Add this to `commonmark_spec_case.hpp`:

```cpp
// Tells GTest how to print a SpecCase — replaces the hex dump
inline void PrintTo(const SpecCase& tc, std::ostream* os) {
    *os << "#" << tc.example << " [" << tc.section << "]";
}
```

That's the minimal fix. The failure line becomes:

```
[  FAILED  ] Spec/CommonMarkSpecTest.MatchesExpectedHtml/006_Tabs,
             where GetParam() = #6 [Tabs]
```

---

### Why this works

GTest looks for `PrintTo(const T&, std::ostream*)` via ADL in the same namespace as `T`. Since `SpecCase` is in `commonmark::testing`, put `PrintTo` there too:

```cpp
namespace commonmark::testing {

struct SpecCase { ... };

inline void PrintTo(const SpecCase& tc, std::ostream* os) {
    *os << "#" << tc.example << " [" << tc.section << "]";
}

} // namespace commonmark::testing
```

---

### Stable diffs

The hex dump changes whenever the struct layout changes (new field, reordered members, different string contents) — making diffs noisy. With `PrintTo` you control exactly what's emitted, so the only thing that appears in diffs is meaningful content.

If you want the output even more diff-friendly, use a deterministic single-line format:

```cpp
inline void PrintTo(const SpecCase& tc, std::ostream* os) {
    *os << "example=" << tc.example
        << " section=\"" << tc.section << "\""
        << " lines=" << tc.start_line << "-" << tc.end_line;
}
```

Which produces the stable, greppable:

```
[  FAILED  ] ...where GetParam() = example=6 section="Tabs" lines=395-407
```

