# Smart pointers in C++

**Source:** claude.ai — project *C++ Markdown Parser* (`019e8773`)  
**Conversation:** `94a7b259-5313-4c75-8031-ac1edf7ddfac`  
**Created:** 2026-06-02  
**Messages:** 14 (7 prompts)

**Related commits:** Learning that informed the ownership model:

- `18303cc replaced raw pointers with smart pointers in AST types`
- `9a057dd solve ownership problem for spine handler`

---

## ▸ Prompt — 2026-06-02 13:33

> What are smart pointers in cpp? what problems do they solve? Are they similar to any other languages construct?

## Smart Pointers in C++

Smart pointers are wrapper classes around raw pointers that **automatically manage memory lifetime**. They live in the `<memory>` header (C++11+).

---

### The Problem They Solve

Raw pointers in C++ have several painful failure modes:

**1. Memory leaks** — forgetting to `delete`
```cpp
void foo() {
    int* p = new int(42);
    if (someCondition) return; // leak! delete never called
    delete p;
}
```

**2. Double-free / use-after-free**
```cpp
int* p = new int(42);
delete p;
delete p;   // undefined behavior
*p = 10;    // use-after-free — also UB
```

**3. Exception safety** — if an exception is thrown between `new` and `delete`, the memory leaks.

Smart pointers solve all of this via **RAII** (Resource Acquisition Is Initialization): the destructor automatically frees the resource when the object goes out of scope.

---

### The Three Smart Pointers

#### `unique_ptr` — sole ownership
```cpp
auto p = std::make_unique<int>(42);
// p is automatically deleted when it goes out of scope
// Cannot be copied, only moved
auto p2 = std::move(p); // p is now null
```
- Zero overhead vs. a raw pointer
- Use this as your **default choice**

#### `shared_ptr` — shared ownership
```cpp
auto a = std::make_shared<MyObj>();
auto b = a; // both a and b own it; ref count = 2
// Object deleted only when LAST owner is destroyed
```
- Uses **reference counting** under the hood
- Small overhead for the control block
- Use when multiple owners genuinely need to share a resource

#### `weak_ptr` — non-owning observer
```cpp
std::shared_ptr<Node> a = std::make_shared<Node>();
std::weak_ptr<Node> w = a; // doesn't increment ref count

if (auto locked = w.lock()) { // safely check if still alive
    locked->doSomething();
}
```
- Breaks **circular reference cycles** that would cause `shared_ptr` to leak
- Must be "locked" into a `shared_ptr` before use

---

### Analogues in Other Languages

| C++ | Language | Equivalent | Notes |
|---|---|---|---|
| `unique_ptr` | **Rust** | `Box<T>` | Rust's entire ownership system is this idea, enforced by the compiler |
| `shared_ptr` | **Rust** | `Rc<T>` / `Arc<T>` | `Arc` is thread-safe (like `shared_ptr` with a mutex) |
| `weak_ptr` | **Rust** | `Weak<T>` | Same cycle-breaking role |
| `shared_ptr` | **Swift** | ARC (automatic) | Swift uses reference counting pervasively, but implicitly |
| `weak_ptr` | **Swift** | `weak var` | Same semantics — non-owning reference |
| All of them | **Java/C#/Python** | GC | These languages use a garbage collector instead — you get safety but lose deterministic destruction and pay a runtime cost |

---

### The key insight

Rust took C++'s smart pointer ideas and made them the **core of the language** — you *cannot* use memory unsafely without explicitly opting into `unsafe {}`. C++ smart pointers give you the same guarantees, but by convention rather than enforcement. The compiler won't stop you from mixing them with raw `new`/`delete` — discipline is still required.

**Rule of thumb:** in modern C++, if you find yourself writing `new` outside of `make_unique`/`make_shared`, stop and ask why.

## ▸ Prompt — 2026-06-02 13:43

> What happens if I do 
>
> auto p2 = p
>
> Will I get a compiler exception?

Yes — you'll get a **compiler error** (not a runtime exception). `unique_ptr` explicitly deletes its copy constructor and copy assignment operator.

```cpp
auto p = std::make_unique<int>(42);
auto p2 = p; // ❌ error: use of deleted function
```

The error from GCC/Clang looks something like:
```
error: call to deleted constructor of 'std::unique_ptr<int>'
note: 'unique_ptr(const unique_ptr &)' is explicitly deleted
```

This is **intentional by design** — copying would mean two `unique_ptr`s think they solely own the same resource, and both would try to `delete` it on destruction (double-free).

Your options are:

```cpp
// 1. MOVE — transfers ownership, p becomes null
auto p2 = std::move(p);

// 2. Share ownership — upgrade to shared_ptr
auto p  = std::make_shared<int>(42);
auto p2 = p; // ✅ ref count goes to 2

// 3. Borrow the raw pointer — if p2 is just observing
int* raw = p.get(); // p still owns it; don't delete raw!

// 4. Borrow as weak_ptr (from a shared_ptr)
std::weak_ptr<int> w = p; // non-owning
```

The compiler catching this at compile time is exactly the point — it's enforcing the ownership contract so you never accidentally get a double-free at runtime.

## ▸ Prompt — 2026-06-02 13:46

> What are cpp templates? What problems do they solve?

## C++ Templates

Templates are C++'s **compile-time code generation** mechanism. You write one generic definition, and the compiler stamps out concrete versions for each type you use it with.

---

### The Problem They Solve

Without templates, to write a `max` function for every type:

```cpp
int    max(int a,    int b)    { return a > b ? a : b; }
float  max(float a,  float b)  { return a > b ? a : b; }
double max(double a, double b) { return a > b ? a : b; }
// ...identical logic, repeated forever
```

The alternative before templates was `void*` — which throws away all type information and is a footgun:
```cpp
void* max(void* a, void* b) { ... } // no type safety whatsoever
```

---

### Function Templates

```cpp
template <typename T>
T max(T a, T b) {
    return a > b ? a : b;
}

max(1, 2);        // compiler generates max<int>
max(1.0, 2.0);    // compiler generates max<double>
max("a", "b");    // compiler generates max<const char*>
                  // (compiles, but compares pointers — careful!)
```

The compiler **deduces** `T` from the arguments. You can also be explicit: `max<float>(1, 2)`.

---

### Class Templates

```cpp
template <typename T>
class Stack {
    std::vector<T> data;
public:
    void push(T val) { data.push_back(val); }
    T    pop()       { auto v = data.back(); data.pop_back(); return v; }
};

Stack<int>         ints;
Stack<std::string> strings;
Stack<MyClass>     objects;
```

This is exactly how `std::vector<T>`, `std::map<K,V>`, `std::optional<T>` etc. are implemented in the standard library.

---

### Multiple & Non-Type Parameters

```cpp
// Multiple type params
template <typename K, typename V>
class Map { ... };  // like std::map<K, V>

// Non-type params (values, not types)
template <typename T, int N>
class FixedArray {
    T data[N];  // stack-allocated, size known at compile time
};

FixedArray<int, 10>  a;  // int[10] on the stack
FixedArray<int, 100> b;  // int[100] — different type entirely!
```

---

### Template Specialization

You can override the generic behavior for a specific type:

```cpp
template <typename T>
std::string describe(T val) { return "some value"; }

// Full specialization for bool
template <>
std::string describe<bool>(bool val) {
    return val ? "true" : "false";
}
```

---

### Concepts (C++20) — Constraining Templates

Raw templates give terrible error messages when misused. **Concepts** let you express requirements clearly:

```cpp
// Without concepts — error message is a wall of gibberish
template <typename T>
T add(T a, T b) { return a + b; }

// With concepts — clear constraint + readable errors
template <typename T>
requires std::integral<T> || std::floating_point<T>
T add(T a, T b) { return a + b; }

add(1, 2);        // ✅
add("hi", "ho");  // ❌ clear error: constraint not satisfied
```

---

### Key Properties

| Property | Detail |
|---|---|
| **Resolved at** | Compile time — zero runtime overhead |
| **Type safe** | Unlike `void*` — the compiler checks everything |
| **Code bloat** | Each instantiation generates separate machine code |
| **Errors** | Can be cryptic (pre-C++20); Concepts fix this |
| **Defined in headers** | The compiler needs the full definition to instantiate |

---

### Analogues in Other Languages

| C++ | Language | Equivalent | Key Difference |
|---|---|---|---|
| Templates | **Java/C#** | Generics | Generics use **type erasure** at runtime; templates generate real separate code per type |
| Templates | **Rust** | Generics + Traits | Similar monomorphization; Traits are like Concepts |
| Templates | **Go** | Generics (1.18+) | More limited, simpler |
| Template metaprogramming | **Any** | Nothing direct | C++ templates are Turing-complete at compile time — a whole paradigm |

The biggest conceptual difference: Java/C# generics are a **runtime** mechanism (one compiled version, types erased), while C++ templates are a **compile-time** mechanism (separate compiled version per type, full optimization possible). Rust works like C++ here — it's called **monomorphization**.

## ▸ Prompt — 2026-06-02 13:55

> What are variants, what does it mean type safe union?

## `std::variant` — Type-Safe Unions

### First, what's a raw `union`?

A C union lets multiple types share the **same memory location**:

```cpp
union Data {
    int    i;
    float  f;
    char   c;
};

Data d;
d.i = 42;
cout << d.f; // 💥 undefined behavior — you read the wrong field
```

The union itself has **no idea which field is active**. You can write an `int` and read a `float` — the compiler won't stop you. That's the unsafety.

---

### `std::variant` — the safe version

```cpp
#include <variant>

std::variant<int, float, std::string> v;

v = 42;           // holds int
v = 3.14f;        // now holds float
v = "hello";      // now holds string

// Safe access — throws if wrong type
int i = std::get<int>(v);       // ❌ throws std::bad_variant_access
                                //    (v currently holds string)

// Safe check first
if (auto* p = std::get_if<std::string>(&v)) {
    cout << *p;  // ✅ only runs if v holds a string
}
```

The key guarantee: **it always knows which type it currently holds**, and accessing the wrong type is a catchable error, not silent memory corruption.

---

### The Real Power — `std::visit`

The idiomatic way to handle all cases, like a type-safe switch:

```cpp
std::variant<int, float, std::string> v = "hello";

std::visit([](auto&& val) {
    using T = std::decay_t<decltype(val)>;

    if constexpr (std::is_same_v<T, int>)
        cout << "int: "    << val;
    else if constexpr (std::is_same_v<T, float>)
        cout << "float: "  << val;
    else
        cout << "string: " << val;
}, v);
```

Or with overloaded lambdas (a common pattern):

```cpp
auto visitor = overloaded {
    [](int i)         { cout << "int: "    << i; },
    [](float f)       { cout << "float: "  << f; },
    [](std::string s) { cout << "string: " << s; }
};

std::visit(visitor, v);
```

The compiler **forces you to handle every type** in the variant — if you miss one, it's a compile error.

---

### A Real Use Case — Result/Error handling

```cpp
using Result = std::variant<std::string, std::error_code>;

Result readFile(const std::string& path) {
    if (/* success */) return std::string{"file contents..."};
    else               return std::make_error_code(std::errc::no_such_file_or_directory);
}

auto result = readFile("data.txt");

std::visit(overloaded{
    [](const std::string& contents) { cout << contents;        },
    [](const std::error_code& err)  { cout << err.message();   }
}, result);
```

This pattern avoids exceptions for expected failure paths — very common in systems/embedded code.

---

### `std::variant` vs. the alternatives

| | Raw `union` | `void*` / `any` | `variant` |
|---|---|---|---|
| **Type safe** | ❌ | ❌ / ✅ | ✅ |
| **Knows active type** | ❌ (you track it) | ✅ | ✅ |
| **Heap allocation** | ❌ | `any` may | ❌ |
| **Exhaustive handling** | ❌ | ❌ | ✅ (via visit) |
| **Performance** | fastest | slower | same as union |

---

### Analogues in Other Languages

| C++ | Language | Equivalent |
|---|---|---|
| `variant` | **Rust** | `enum` with data — this is idiomatic Rust, e.g. `Option<T>`, `Result<T,E>` |
| `variant` | **Haskell/F#/OCaml** | Algebraic Data Types (ADTs) — the original inspiration |
| `variant` | **Swift** | `enum` with associated values |
| `variant` | **TypeScript** | Discriminated unions |
| `std::visit` | **Rust** | `match` expression |

Rust's `match` is the gold standard here — it's built into the language syntax and exhaustiveness checking is mandatory, not a library add-on like `std::visit`. In C++ you can forget to call `visit` entirely and just never handle the cases — Rust won't let you.

## ▸ Prompt — 2026-06-02 14:02

> What is static_cast, when is it a good practice to use?

## `static_cast` in C++

C++ has **5 named casts**, each with a specific purpose. `static_cast` is the workhorse — the one you should reach for first.

---

### What it does

It performs conversions that are **known and checkable at compile time**. The compiler verifies the cast makes sense — unlike C-style casts which just force it through blindly.

```cpp
// C-style cast — no safety, just raw reinterpretation
int i = (int)3.14;   // works, but silently accepts nonsense too

// static_cast — explicit, compiler-checked
int i = static_cast<int>(3.14);  // ✅ clearly intentional
```

---

### When to use it

#### 1. Numeric conversions
```cpp
double d = 3.99;
int i = static_cast<int>(d);  // ✅ truncates to 3, intentional

float f = 1.0f;
double d2 = static_cast<double>(f);  // ✅ widening
```
The cast signals to the reader: *"I know this truncates/narrows, it's intentional."*

#### 2. Up and downcasting in an inheritance hierarchy
```cpp
class Animal { virtual void speak() {} };
class Dog : public Animal {};

// Upcast (always safe — no cast needed, but explicit is fine)
Dog* dog = new Dog();
Animal* a = static_cast<Animal*>(dog);  // ✅

// Downcast (you're asserting you KNOW the runtime type)
Animal* a = getAnimal();
Dog* d = static_cast<Dog*>(a);  // ✅ only safe if a really IS a Dog
                                // no runtime check — that's dynamic_cast's job
```

#### 3. `void*` conversions (e.g. C APIs)
```cpp
void* raw = malloc(sizeof(int));
int* p = static_cast<int*>(raw);  // ✅ converting void* back to typed pointer
```

#### 4. Enum to int and back
```cpp
enum class Color { Red, Green, Blue };

int raw = static_cast<int>(Color::Green);   // ✅ enum → int
Color c = static_cast<Color>(1);            // ✅ int → enum (you assert it's valid)
```

#### 5. Resolving arithmetic ambiguity
```cpp
int a = 5, b = 2;
double result = static_cast<double>(a) / b;  // ✅ 2.5, not 2
```

---

### The 5 casts and when to use each

| Cast | Use for | Runtime check? |
|---|---|---|
| `static_cast` | Well-defined conversions, numeric, hierarchy | ❌ |
| `dynamic_cast` | Downcasting with safety — returns `nullptr` if wrong | ✅ |
| `const_cast` | Adding/removing `const` only | ❌ |
| `reinterpret_cast` | Reinterpreting raw bits (dangerous) | ❌ |
| C-style `(T)` | Tries all of the above in order — avoid | ❌ |

```cpp
// dynamic_cast — safe downcast, needs virtual function in base
Animal* a = getAnimal();
Dog* d = dynamic_cast<Dog*>(a);
if (d) { /* it really is a Dog */ }
else   { /* it wasn't — handle gracefully */ }

// reinterpret_cast — last resort, basically memcpy semantics
int i = 42;
float* f = reinterpret_cast<float*>(&i);  // 💥 almost always wrong
```

---

### Why not just use C-style casts?

```cpp
// C-style — what does this actually do?
Dog* d = (Dog*)getAnimal();   // is this a static? dynamic? reinterpret? const removal?
                              // unclear, and it silently picks the most dangerous option

// Named cast — intent is explicit and greppable
Dog* d = static_cast<Dog*>(getAnimal());  // clearly a hierarchy downcast
```

Three concrete reasons to prefer named casts:
- **Intent is explicit** — `static_cast` documents why you're casting
- **Greppable** — easy to audit all casts in a codebase
- **Constrained** — each named cast refuses operations outside its scope; C-style won't

---

### The golden rule

> Use `static_cast` when the compiler already knows the conversion is *meaningful*. If you need a runtime check → `dynamic_cast`. If you're fighting `const` → `const_cast`. If you're reinterpreting raw memory → `reinterpret_cast` (and reconsider your design).

The need for `reinterpret_cast` in application code is almost always a design smell. `static_cast` covering your needs is the happy path.

## ▸ Prompt — 2026-06-02 14:08

> Does std::move remove the ownership of the pointer from the previous unique_ptr?
>
> what is wrong with this code?
> unique_ptr a
>
> b = std::move(a)
> c = std::move(a)

## Yes — and that's exactly the bug

`std::move` **transfers** ownership and leaves `a` in a **null/empty state** after the first move. The second move is moving from an already-null `unique_ptr`.

```cpp
auto a = std::make_unique<int>(42);

auto b = std::move(a);  // ✅ a is now null, b owns the int
auto c = std::move(a);  // ⚠️  a is already null — c is just null too
```

No crash here, but `c` is a null `unique_ptr` holding nothing. If you try to use it:

```cpp
*c;        // 💥 undefined behavior — dereferencing null
c->foo();  // 💥 same
```

---

### What `std::move` actually does

It's commonly misunderstood. `std::move` itself **does nothing to memory** — it's just a cast to an rvalue reference, which *enables* the move constructor to run.

```cpp
// std::move is literally just this:
template<typename T>
T&& move(T& val) {
    return static_cast<T&&>(val);
}
```

The actual ownership transfer happens in `unique_ptr`'s **move constructor**, which:
1. Copies the raw pointer from `a` to `b`
2. Sets `a`'s internal pointer to `nullptr`

So after `std::move(a)` into `b`, `a` is a valid but **empty** `unique_ptr`.

---

### The fix depends on intent

```cpp
// Intent 1: b and c should share ownership → use shared_ptr
auto a = std::make_shared<int>(42);
auto b = a;  // ✅ ref count = 2
auto c = a;  // ✅ ref count = 3

// Intent 2: pass to a function without transferring ownership → borrow
void foo(int* p);         // raw pointer borrow
foo(a.get());             // ✅ a still owns it

// Intent 3: sequential transfer (a→b, then b→c)
auto b = std::move(a);    // a is null, b owns it
auto c = std::move(b);    // b is null, c owns it — chain is fine
```

The core rule: **a `unique_ptr` can only be moved once** — after that it's empty and moving it again is a no-op that produces another null.

## ▸ Prompt — 2026-06-02 14:11

> What is std::string_view?

## `std::string_view` (C++17)

It's a **non-owning read-only view** into a string — essentially just a pointer + length pair, with no heap allocation and no copy of the data.

```cpp
struct string_view {
    const char* ptr;  // points into existing string
    size_t      len;
};
```

---

### The Problem It Solves

Passing strings around in C++ used to force an ugly choice:

```cpp
// Option 1: const std::string& — forces std::string specifically
//           can't accept string literals without a hidden allocation
void greet(const std::string& name);

// Option 2: const char* — accepts literals but loses length info,
//           can't accept std::string without .c_str()
void greet(const char* name);
```

Neither works cleanly for all callers. `string_view` unifies them:

```cpp
void greet(std::string_view name);   // ✅ accepts everything, zero copy

std::string s = "Alice";
greet(s);            // ✅ no copy
greet("Bob");        // ✅ no allocation
greet(s.substr(0,3));// ✅ view into substring — no allocation
```

---

### Key Properties

```cpp
std::string_view sv = "Hello, World";

sv.size();          // 12
sv.substr(0, 5);    // returns another string_view — no allocation!
sv[0];              // 'H'
sv.starts_with("Hello");  // ✅ (C++20)
sv.find("World");   // works like std::string

// It does NOT own memory — no heap allocation ever
// The underlying string must outlive the view!
```

---

### The Critical Danger — Dangling Views

Since `string_view` doesn't own its data, **lifetime bugs** are easy to make:

```cpp
std::string_view sv;

{
    std::string s = "hello";
    sv = s;
}  // s is destroyed here

cout << sv;  // 💥 dangling — sv points to freed memory
```

Another subtle trap:

```cpp
// Temporary string is destroyed at end of the expression!
std::string_view sv = std::string("hello");  // 💥 immediate dangle
```

---

### When to use it

```cpp
// ✅ Function parameters — read-only, no ownership needed
void parse(std::string_view input);
bool startsWith(std::string_view str, std::string_view prefix);

// ✅ Parsing / tokenizing — slicing without allocating
std::string_view token = input.substr(start, len);  // zero cost!

// ❌ Storing long-term — lifetime too hard to guarantee
class Config {
    std::string_view name;  // ❌ dangerous — what does it point to?
    std::string name;       // ✅ own it if you're storing it
};

// ❌ Return value from function (usually)
std::string_view getName() {
    std::string s = "Alice";
    return s;   // 💥 returns view of local variable
}
```

---

### Comparison with other languages

| C++ | Language | Equivalent |
|---|---|---|
| `string_view` | **Rust** | `&str` — a borrowed string slice, lifetime enforced by compiler |
| `string_view` | **Go** | `string` — Go strings are already immutable slices, always |
| `string_view` | **C# (.NET 8)** | `ReadOnlySpan<char>` — same idea, stack only |

Rust's `&str` is the closest analogue — same pointer+length design, same non-owning semantics. The key difference is Rust's **borrow checker enforces** that the underlying data outlives the view at compile time. C++ gives you the same performance but lets you shoot yourself in the foot with dangling views.

The pattern in C++ is: **own with `std::string`, view with `std::string_view`** — just like `unique_ptr` owns and raw pointer borrows.

