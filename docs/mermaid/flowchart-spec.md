# Flowchart syntax fixtures
#
# One diagram per construct group. Each diagram is self-contained and
# independently parseable. Together they cover every syntactic feature
# of the `flowchart` / `graph` diagram type.
#
# Intended use: paste each block into mermaid.live to visually verify,
# or feed them to your parser as a test corpus.

---

## F-01 · Diagram keywords and directions

Every valid opening keyword and direction code.

```mermaid
%% 'flowchart' keyword, all five directions
flowchart TB
    A --> B

%% 'graph' is an alias for flowchart
graph LR
    A --> B

%% remaining direction codes (each is a separate valid diagram)
%% flowchart BT    (bottom to top)
%% flowchart RL    (right to left)
%% flowchart TD    (alias for TB)
```

---

## F-02 · Node declarations (shape zoo)

Every bracket syntax that maps to a distinct shape.

```mermaid
flowchart LR
    %% --- classic bracket shapes ---
    n_rect[Rectangle]
    n_round(Round edges)
    n_stadium([Stadium])
    n_sub[[Subroutine]]
    n_cyl[(Cylinder)]
    n_circle((Circle))
    n_asym>Asymmetric]
    n_diamond{Diamond}
    n_hex{{Hexagon}}
    n_lean_r[/Lean right/]
    n_lean_l[\Lean left\]
    n_trap[/Trapezoid\]
    n_trap_alt[\Trapezoid alt/]
    n_dbl(((Double circle)))

    %% --- v11.3+ @{shape:} syntax ---
    n_new1@{ shape: rect,    label: "Process" }
    n_new2@{ shape: diam,    label: "Decision" }
    n_new3@{ shape: stadium, label: "Terminal" }
    n_new4@{ shape: cyl,     label: "Database" }
    n_new5@{ shape: doc,     label: "Document" }
    n_new6@{ shape: delay,   label: "Delay" }
    n_new7@{ shape: tri,     label: "Extract" }
    n_new8@{ shape: fork,    label: "Fork/Join" }
    n_new9@{ shape: f-circ,  label: "Junction" }
```

---

## F-03 · Node labels and text content

All the ways a label can be expressed.

```mermaid
flowchart TD
    %% id == label (bare node)
    bare_node

    %% explicit text label
    id1[Plain text label]

    %% quoted label (required for special chars)
    id2["Label with spaces & punctuation: a > b"]

    %% HTML entity escaping inside quotes
    id3["A &amp; B #lt; C #35; hash"]

    %% Unicode inside quotes
    id4["Ünïcödé テスト 中文"]

    %% Markdown string label (bold / italic / newline)
    id5["`**bold** and *italic*
    and a newline`"]

    %% FontAwesome icon in label (fa: prefix)
    id6[fa:fa-car Vehicle]

    %% Reuse: second definition for same id is ignored
    id1[This second label is silently dropped]

    id1 --> id2 --> id3 --> id4 --> id5 --> id6
```

---

## F-04 · Edge / link types

Every arrow and line variant.

```mermaid
flowchart LR
    %% --- arrow heads ---
    A --> B          %% solid arrow
    C --- D          %% open link (no head)
    E --o F          %% circle head
    G --x H          %% cross head
    I <--> J         %% bidirectional arrow
    K <--o L         %% bidirectional circle
    M <--x N         %% bidirectional cross

    %% --- stroke styles ---
    P -.-> Q         %% dotted arrow
    R -.- S          %% dotted open
    T ==> U          %% thick arrow
    V === W          %% thick open

    %% --- inline label (two syntaxes) ---
    X -- label text --> Y
    X -->|pipe label| Z
    X -. dotted label .-> Z
    X == thick label ==> Z
```

---

## F-05 · Edge length (extra dashes)

More dashes / dots / equals = longer rank span.

```mermaid
flowchart TD
    A -->   B       %% length 1
    A --->  C       %% length 2
    A ----> D       %% length 3

    E -.->  F       %% dotted length 1
    E -..-> G       %% dotted length 2

    H ==>   I       %% thick length 1
    H ===>  J       %% thick length 2

    %% label in the middle: extra dashes go on the right side
    K -- mid label ---> L
```

---

## F-06 · Multi-target chaining

Multiple edges declared on one line.

```mermaid
flowchart LR
    %% chain: A→B→C→D
    A --> B --> C --> D

    %% fan-out with &
    E & F --> G

    %% fan-in with &
    H --> I & J

    %% combined
    K & L --> M & N
```

---

## F-07 · Subgraphs

Flat, nested, with explicit IDs, with per-subgraph direction, and
edges that cross subgraph boundaries.

```mermaid
flowchart TB
    %% --- basic subgraph ---
    subgraph sg1[First group]
        A --> B
    end

    %% --- subgraph with explicit id ---
    subgraph sg2_id[Second group]
        C --> D
    end

    %% --- nested subgraph ---
    subgraph outer[Outer]
        subgraph inner[Inner]
            E --> F
        end
        G --> inner
    end

    %% --- per-subgraph direction (LR inside a TB diagram) ---
    subgraph sideways[Sideways]
        direction LR
        H --> I --> J
    end

    %% --- edges between subgraphs and nodes ---
    sg1 --> sg2_id
    B --> sideways
    D --> outer
```

---

## F-08 · Styling — inline node styles

Per-node `style` overrides.

```mermaid
flowchart LR
    A[Styled node]
    B[Another]
    C[Third]

    A --> B --> C

    style A fill:#ff9,stroke:#333,stroke-width:3px
    style B fill:#9bf,stroke:#06f,stroke-width:1px,color:#000
    style C fill:#fff,stroke:#f00,stroke-dasharray:5 5
```

---

## F-09 · Styling — classDef and class assignment

Named class definitions and all three ways to attach them.

```mermaid
flowchart TD
    %% --- class definition ---
    classDef primary fill:#4a90d9,stroke:#2c6fa8,color:#fff,stroke-width:2px
    classDef danger  fill:#e74c3c,stroke:#c0392b,color:#fff
    classDef default fill:#eee,stroke:#999       %% applies to all unclassed nodes

    A[Start]:::primary --> B{Decision}:::primary
    B -->|yes| C[OK]:::primary
    B -->|no|  D[Fail]:::danger

    %% explicit class statement (single node)
    class C primary

    %% explicit class statement (multiple nodes at once)
    class D,B danger
```

---

## F-10 · Styling — linkStyle

Per-edge style by 0-based index, and multi-edge selector.

```mermaid
flowchart LR
    A --> B --> C --> D --> E

    %% edge 0 = A→B, edge 1 = B→C, etc.
    linkStyle 0 stroke:#f00,stroke-width:3px
    linkStyle 1 stroke:#0a0,stroke-width:2px,stroke-dasharray:4 2
    linkStyle 2,3 stroke:#00f
    linkStyle default stroke:#999
```

---

## F-11 · Click interactions

URL links, JS callbacks, and tooltips. (Requires securityLevel:'loose'.)

```mermaid
flowchart LR
    A[GitHub] --> B[Callback] --> C[Call syntax] --> D[href alias]

    click A "https://github.com" "Open GitHub" _blank
    click B callback "Tooltip on B"
    click C call myFunction() "Tooltip on C"
    click D href "https://example.com" _self
```

---

## F-12 · Front-matter config block

YAML front-matter that configures layout, look, theme, and curve.

```mermaid
---
title: Front-matter demo
config:
  look: handDrawn
  theme: forest
  flowchart:
    curve: cardinal
    defaultRenderer: dagre
---
flowchart LR
    A[Start] --> B{Gate} -->|pass| C[Done]
    B -->|fail| D[Retry] --> A
```

---

## F-13 · Init directive (legacy inline config)

The `%%{init: ...}%%` directive as an alternative to front-matter.

```mermaid
%%{init: {"theme": "dark", "flowchart": {"curve": "linear", "nodeSpacing": 50}}}%%
flowchart TD
    A --> B --> C
```

---

## F-14 · Comments

Comment lines (ignored by parser) in various positions.

```mermaid
flowchart LR
    %% This whole line is a comment
    A --> B  %% inline comment after a statement is NOT supported — this text is part of the line
    %% but a line starting with %% is always a comment
    B --> C
    %% Comments can appear between any statements
    C --> D
```

> Note: `%%` only starts a comment when it is the very first non-whitespace content on a line.
> Trailing `%%` after a statement is **not** a comment — it is a parse error or ignored
> depending on the version.

---

## F-15 · Edge IDs and animation (v11.10+)

Named edges, per-edge curve override, and animation properties.

```mermaid
flowchart LR
    A e1@--> B
    B e2@-.-> C
    C e3@==> D

    e1@{ animate: true, animation: fast }
    e2@{ curve: stepAfter }
    e3@{ animate: true, animation: slow, curve: cardinal }
```

---

## F-16 · Special characters and entity escaping

Characters that would otherwise break the parser.

```mermaid
flowchart LR
    A["a > b and a < b"]
    B["quote: &quot;hello&quot;"]
    C["hash: #35;"]
    D["amp: #amp;"]
    E["newline via <br/>second line"]

    A --> B --> C --> D --> E
```

---

## F-17 · Invisible links (layout hints)

`~~~` forces extra space between nodes without drawing an edge.

```mermaid
flowchart LR
    A --> B
    A --> C
    B ~~~ C   %% invisible link pushes C further right for layout
```

---

## F-18 · Composite: real-world diagram using most features together

A single diagram that exercises the majority of constructs in combination.

```mermaid
---
title: CI/CD pipeline
config:
  theme: default
  flowchart:
    curve: basis
---
flowchart TD
    classDef ok      fill:#2ecc71,stroke:#27ae60,color:#fff
    classDef warn    fill:#f39c12,stroke:#e67e22,color:#fff
    classDef fail    fill:#e74c3c,stroke:#c0392b,color:#fff
    classDef neutral fill:#ecf0f1,stroke:#bdc3c7

    START(["fa:fa-play Start"]):::ok

    subgraph ci[CI]
        direction LR
        LINT[Lint]:::neutral
        TEST[Unit tests]:::neutral
        BUILD[Build]:::neutral
        LINT --> TEST --> BUILD
    end

    subgraph cd[CD]
        direction LR
        STAGE([Staging deploy]):::warn
        SMOKE[Smoke tests]:::neutral
        PROD([Production deploy]):::ok
        STAGE --> SMOKE --> PROD
    end

    FAIL(["fa:fa-times Failed"]):::fail
    DONE(["fa:fa-check Done"]):::ok

    START --> ci
    BUILD e1@--> STAGE
    SMOKE --x FAIL
    PROD --> DONE

    %% invisible link to nudge FAIL below SMOKE
    FAIL ~~~ DONE

    style DONE stroke-width:3px
    linkStyle default stroke:#999,stroke-width:1.5px
    e1@{ animate: true, animation: slow }
```

---

## Construct coverage checklist

| # | Construct | Fixture(s) |
|---|-----------|------------|
| 1 | `flowchart` keyword | F-01 |
| 2 | `graph` alias | F-01 |
| 3 | Directions: TB TD BT LR RL | F-01 |
| 4 | Bare node (id only) | F-03 |
| 5 | Node shapes — classic bracket syntax (14 shapes) | F-02 |
| 6 | Node shapes — `@{shape:}` syntax | F-02 |
| 7 | Plain text label | F-03 |
| 8 | Quoted label | F-03 |
| 9 | HTML entity in label (`#nn;` and `&name;`) | F-03, F-16 |
| 10 | Unicode in label | F-03 |
| 11 | Markdown string label (backtick) | F-03 |
| 12 | FontAwesome icon in label (`fa:`) | F-03, F-18 |
| 13 | Node id reuse (last text wins) | F-03 |
| 14 | Arrow edge `-->` | F-04 |
| 15 | Open edge `---` | F-04 |
| 16 | Circle head `--o` | F-04 |
| 17 | Cross head `--x` | F-04 |
| 18 | Bidirectional `<-->` `<--o` `<--x` | F-04 |
| 19 | Dotted `-.->` `-.-` | F-04 |
| 20 | Thick `==>` `===` | F-04 |
| 21 | Inline label `-- text -->` | F-04 |
| 22 | Pipe label `-->|text|` | F-04 |
| 23 | Edge length (extra dashes/dots/equals) | F-05 |
| 24 | Chain `A --> B --> C` | F-06 |
| 25 | Multi-target `&` syntax | F-06 |
| 26 | Subgraph (basic) | F-07 |
| 27 | Subgraph with explicit id | F-07 |
| 28 | Nested subgraphs | F-07 |
| 29 | Per-subgraph `direction` | F-07 |
| 30 | Edges to/from subgraph id | F-07 |
| 31 | `style` per-node override | F-08 |
| 32 | `classDef` definition | F-09 |
| 33 | `class` assignment (single + multi) | F-09 |
| 34 | `:::className` inline syntax | F-09 |
| 35 | `default` classDef | F-09 |
| 36 | `linkStyle` by index | F-10 |
| 37 | `linkStyle` multi-index | F-10 |
| 38 | `linkStyle default` | F-10 |
| 39 | `click` URL | F-11 |
| 40 | `click` callback | F-11 |
| 41 | `click call` syntax | F-11 |
| 42 | `click href` alias | F-11 |
| 43 | Link target (`_blank` `_self` etc.) | F-11 |
| 44 | Tooltip on click | F-11 |
| 45 | YAML front-matter block | F-12 |
| 46 | `%%{init:}%%` directive | F-13 |
| 47 | `%%` comment line | F-14 |
| 48 | Edge IDs `e1@-->` | F-15 |
| 49 | `@{ animate: }` on edge | F-15 |
| 50 | Per-edge curve `@{ curve: }` | F-15 |
| 51 | Special chars in quoted labels | F-16 |
| 52 | `<br/>` in label | F-16 |
| 53 | Invisible link `~~~` | F-17 |
| 54 | Combined real-world usage | F-18 |

---

# Warnings

F-14 (comments) has a subtle rule that's easy to get wrong: %% only opens a comment when it is the first non-whitespace characters on a line. Trailing %% after a valid statement is not a comment — it either causes a parse error or is silently consumed depending on the version. Don't implement trailing comment stripping.
F-06 (& chaining) is one of the trickiest constructs to parse because A & B --> C & D is syntactic sugar that expands to four separate edges. The & groups on both sides of the arrow need to be cross-producted.
F-03 (label reuse) — when the same node id appears multiple times, only the last label definition wins. This matters if you're building a node table: don't deduplicate by first-seen, deduplicate by last-seen.
F-15 (edge IDs and animation) is v11.10+ only, so if you're targeting an older mermaid version you can skip that fixture entirely.