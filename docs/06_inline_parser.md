# 6. InlineParser — phase 2

← [5. SpineHandler — phase 1](05_spine_handler.md) | [Index](index.md) | Next: [7. Tab algorithm](07_tab_algorithm.md) →

---

The InlineParser runs on a single leaf block's `string_content` at a time,
building a linked list of `InlineNode` children. It has no interaction with the
spine or the block tree structure. All state is reset between `parse()` calls.

`InlineParser::parse()` is invoked by [`SpineHandler::finalize()`](05_spine_handler.md#56-finalize-and-the-phase-boundary) after all blocks are closed and `ref_map_` is fully populated. The inline node types it produces are defined in [§2.5](02_data_types.md#25-inlinenode-delimiter-bracketentry).

---

## 6.1 State

```cpp
// include/markdown_parser/InlineParser.hpp

class InlineParser {
public:
    // Entry point. Parses block->string_content and attaches the resulting
    // inline node list to block->inline_children. Requires a fully-populated
    // ref_map for reference-style link resolution.
    // Must not be called before SpineHandler::finalize() returns.
    void parse(BlockNode*                                         block,
               const std::unordered_map<std::string, LinkDef>&   ref_map);

private:
    // ── per-parse state (reset at the top of each parse() call) ──────────

    std::string_view         input_;        // block->string_content view
    std::size_t              pos_ = 0;      // current byte offset into input_

    // Delimiter stack for * and _ runs. Entries accumulate as the scanner
    // encounters delimiter characters; the processEmphasis() post-pass
    // matches opener/closer pairs and emits Emph/Strong nodes.
    std::vector<Delimiter>   delimiters_;

    // Bracket stack for [ and ![ openers. LIFO; entries are popped and
    // resolved (or discarded) when a matching ] is encountered.
    std::vector<BracketEntry> brackets_;

    // Non-owning reference to the document's link reference map.
    const std::unordered_map<std::string, LinkDef>* ref_map_ = nullptr;
};
```

`Delimiter` and `BracketEntry` are defined in [§2.5](02_data_types.md#25-inlinenode-delimiter-bracketentry). The `delim_top` field of `BracketEntry` and its interaction with `processEmphasis` are clarified in [§9.4](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction).

---

## 6.2 InlineParser methods

```cpp
// ── parse (entry point) ───────────────────────────────────────────────────

void InlineParser::parse(
    BlockNode*                                        block,
    const std::unordered_map<std::string, LinkDef>&   ref_map)
{
    // Reset all per-parse state.
    input_      = block->string_content;
    pos_        = 0;
    ref_map_    = &ref_map;
    delimiters_.clear();
    brackets_.clear();

    // Scan the string character by character, building inline nodes.
    // Each parseInline() call advances pos_ by at least one byte.
    // Nodes are accumulated into a temporary vector; ownership is transferred
    // to block->inline_children at the end.
    std::vector<std::unique_ptr<InlineNode>> nodes;
    while (pos_ < input_.size()) {
        std::unique_ptr<InlineNode> node = parseInline();
        if (!node) continue;
        nodes.push_back(std::move(node));
    }

    // Process emphasis: match opener/closer pairs on the delimiter stack.
    // Emits Emph and Strong nodes wrapping their children.
    processEmphasis(std::nullopt);

    block->inline_children = std::move(nodes);
}

// ── dispatch ──────────────────────────────────────────────────────────────

// Dispatch on the current character. Returns the constructed InlineNode (owned),
// or nullptr if the character is absorbed without producing a node (e.g. a
// delimiter that was pushed onto the delimiter stack).
//
// '\n' handling (spec §6.7): when the current character is '\n' (the line
// separator written by appendText), parseInline() looks backward in the
// accumulated literal text:
//   - Two or more trailing spaces before '\n'  → emit LineBreak, strip spaces.
//   - A backslash immediately before '\n'       → emit LineBreak, strip backslash.
//   - Otherwise                                 → emit SoftBreak.
// In all cases the '\n' byte itself is consumed without being added to any
// literal text node.

std::unique_ptr<InlineNode> InlineParser::parseInline();

// ── leaf scanners ─────────────────────────────────────────────────────────

// Scan a backtick string: find matching closing backtick run of the same
// length. Normalise interior content per spec §6.1:
//   1. Line endings are converted to spaces.
//   2. If the result begins AND ends with a space, and is not entirely spaces,
//      strip exactly one leading and one trailing space.
//      (e.g. " foo " → "foo", "  " → "  " — all-spaces strings are not stripped)
// Returns a Code node (owned), or nullptr if no closing run is found.
std::unique_ptr<InlineNode> InlineParser::parseBacktickString();

// Match <URI> or <email> autolink forms. Returns a Link node (owned) on success,
// nullptr if the < does not start a valid autolink.
std::unique_ptr<InlineNode> InlineParser::parseAutolink();

// Match open tag, closing tag, HTML comment, processing instruction,
// declaration, or CDATA section. Returns an HtmlInline node (owned), or nullptr.
std::unique_ptr<InlineNode> InlineParser::parseHtmlInline();

// ── link and image ────────────────────────────────────────────────────────

// Push a BracketEntry onto the bracket stack when '[' or '![' is seen.
// Records whether this is an image opener and the current delimiter stack depth
// (so delimiters inside an invalid link can be deactivated later).
void InlineParser::handleBracketOpener(bool is_image);

// Called when ']' is seen. Pop the bracket stack and attempt to resolve:
//   1. Inline link:     ] followed by ( destination "title" )
//   2. Full ref link:   ] followed by [label]
//   3. Collapsed ref:   ] followed by []
//   4. Shortcut ref:    ] with the bracket text used as the label
// On failure (no matching opener or no valid link), emit ']' as a Text node
// and leave the bracket stack intact.
std::unique_ptr<InlineNode> InlineParser::handleBracketCloser();

// Scan a link destination in one of two forms:
//   - Angle-bracket form: < ... > (no spaces, no unescaped </>)
//   - Bare form: balanced parentheses, no ASCII control characters
// Returns the destination string on success, std::nullopt on failure.
std::optional<std::string> InlineParser::scanLinkDestination();

// Scan a link title in one of three delimiter forms:
//   - "title" (double-quoted)
//   - 'title' (single-quoted)
//   - (title) (parenthesised)
// Returns the title string on success, std::nullopt on failure.
std::optional<std::string> InlineParser::scanLinkTitle();

// ── emphasis ──────────────────────────────────────────────────────────────

// Called when a run of * or _ is scanned. Determines left-flanking /
// right-flanking status per spec §6.4 rules, then either pushes a Delimiter
// entry (can_open), attempts to close an existing opener (can_close), or
// emits the run as literal Text if it can neither open nor close.
void InlineParser::handleEmphasis(char delim_char, std::size_t run_len);

// Post-processing pass over the delimiter stack. Matches the innermost
// opener/closer pairs first, wraps their content in Emph or Strong nodes,
// and removes the matched entries. stack_bottom limits the search range;
// std::nullopt means process the entire stack.
// Called once at end of parse() with std::nullopt, and again with a saved
// depth when a valid link/image is resolved (to process emphasis inside it).
void InlineParser::processEmphasis(std::optional<std::size_t> stack_bottom);

// ── utilities ─────────────────────────────────────────────────────────────

// Collapse Unicode whitespace runs to a single space and fold to Unicode
// case-fold for link reference label matching (spec §4.7).
static std::string InlineParser::normaliseLabel(std::string_view label);

// Allocate a new InlineNode of the given type with default-initialised fields.
std::unique_ptr<InlineNode> InlineParser::makeNode(InlineType type);
```

`normaliseLabel` must apply Unicode case folding — the implementation options are discussed in [§9.5](09_open_decisions.md#95-unicode-case-folding-for-link-reference-labels).

The `processEmphasis` / bracket deactivation interaction (including the meaning of `stack_bottom`) is detailed in [§9.4](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction).

Link reference definitions resolved via `ref_map_` are populated by [`SpineHandler`](05_spine_handler.md#51-state) during phase 1 and stored as [`LinkDef`](02_data_types.md#22-blockdata-union) values.

---

← [5. SpineHandler — phase 1](05_spine_handler.md) | [Index](index.md) | Next: [7. Tab algorithm](07_tab_algorithm.md) →
