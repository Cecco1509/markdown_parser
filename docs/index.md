# CommonMark parser — design specification

> Language: C++20  
> Reference: CommonMark Spec v0.31.2  
> External libs: `nlohmann/json` (spec test loading), `googletest` (unit tests)

---

## Table of contents

1. [Project structure](01_project_structure.md)
2. [Data types and node structures](02_data_types.md)
   - 2.1 [Enumerations](02_data_types.md#21-enumerations)
   - 2.2 [BlockData union](02_data_types.md#22-blockdata-union)
   - 2.3 [InlineData union](02_data_types.md#23-inlinedata-union)
   - 2.4 [BlockNode](02_data_types.md#24-blocknode)
   - 2.5 [InlineNode, Delimiter, BracketEntry](02_data_types.md#25-inlinenode-delimiter-bracketentry)
3. [Continuation, open, and close rules](03_continuation_rules.md)
   - 3.1 [Continuation rules per block type](03_continuation_rules.md#31-continuation-rules-per-block-type)
   - 3.2 [Open block rules — step 2 triggers](03_continuation_rules.md#32-open-block-rules--step-2-triggers)
   - 3.3 [Close block rules](03_continuation_rules.md#33-close-block-rules)
4. [PreScanner](04_prescanner.md)
   - 4.1 [ScannedLine](04_prescanner.md#41-scannedline)
   - 4.2 [PreScanner methods](04_prescanner.md#42-prescanner-methods)
5. [SpineHandler — phase 1](05_spine_handler.md)
   - 5.1 [State](05_spine_handler.md#51-state)
   - 5.2 [SpineMatchResult](05_spine_handler.md#52-spinematchresult)
   - 5.3 [Per-line loop — three steps](05_spine_handler.md#53-per-line-loop--three-steps)
   - 5.4 [Tree mutation primitives](05_spine_handler.md#54-tree-mutation-primitives)
   - 5.5 [Tab accounting](05_spine_handler.md#55-tab-accounting)
   - 5.6 [finalize and the phase boundary](05_spine_handler.md#56-finalize-and-the-phase-boundary)
6. [InlineParser — phase 2](06_inline_parser.md)
   - 6.1 [State](06_inline_parser.md#61-state)
   - 6.2 [InlineParser methods](06_inline_parser.md#62-inlineparser-methods)
7. [Tab algorithm](07_tab_algorithm.md)
   - 7.1 [Virtual column arithmetic](07_tab_algorithm.md#71-virtual-column-arithmetic)
   - 7.2 [Partial tab splitting](07_tab_algorithm.md#72-partial-tab-splitting)
   - 7.3 [Worked example — spec §2.2 example 5](07_tab_algorithm.md#73-worked-example--spec-22-example-5)
8. [Data flow through phases](08_data_flow.md)
   - 8.1 [End-to-end pipeline](08_data_flow.md#81-end-to-end-pipeline)
   - 8.2 [Lifecycle summary](08_data_flow.md#82-lifecycle-summary)
   - 8.3 [Ownership model](08_data_flow.md#83-ownership-model)
9. [Open design decisions](09_open_decisions.md)
   - 9.1 [Memory ownership](09_open_decisions.md#91-memory-ownership--blocknode-and-inlinenode)
   - 9.2 [Link ref def scanning timing](09_open_decisions.md#92-maybescanlinkRefdefs--when-to-scan)
   - 9.3 [appendText from_byte when a tab is split](09_open_decisions.md#93-appendtext-from_byte-when-a-tab-is-split)
   - 9.4 [processEmphasis / bracket deactivation](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction)
   - 9.5 [Unicode case folding](09_open_decisions.md#95-unicode-case-folding-for-link-reference-labels)
