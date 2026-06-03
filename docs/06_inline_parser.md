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

`Delimiter` and `BracketEntry` are defined in [§2.5](02_data_types.md#25-inlinenode-delimiter-bracketentry). `delim_top` is the size of `delimiters_` at the moment the bracket entry is pushed; see `handleBracketOpener` / `handleBracketCloser` in [§6.2](#62-inlineparser-methods) and [§9.4](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction).

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
// Records whether this is an image opener and stores delim_top =
// delimiters_.size() at the moment of the push — the exclusive lower bound
// for deactivation if the bracket later turns out to be invalid (§9.4).
void InlineParser::handleBracketOpener(bool is_image);

// Called when ']' is seen. Pop the bracket stack and attempt to resolve:
//   1. Inline link:     ] followed by ( destination "title" )
//   2. Full ref link:   ] followed by [label]
//   3. Collapsed ref:   ] followed by []
//   4. Shortcut ref:    ] with the bracket text used as the label
//
// On success: call processEmphasis(bracket.delim_top) to process emphasis
// inside the link content before constructing the Link/Image node. Then pop
// the bracket entry.
//
// On failure (no matching opener or none of the four forms match): emit ']'
// as a Text node, set can_open = false and can_close = false on every
// delimiter in delimiters_[bracket.delim_top .. top] to deactivate them
// (CommonMark appendix algorithm §6.2), and pop the bracket entry.
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
// and removes the matched entries.
//
// stack_bottom is an exclusive lower bound on the delimiter indices searched:
//   - std::nullopt  → process the entire stack (called once at end of parse())
//   - bracket.delim_top → process only delimiters added after the '[' opener
//     (called from handleBracketCloser() when a valid link/image is resolved,
//      so emphasis inside the link content is processed before the link node
//      is constructed — §9.4)
void InlineParser::processEmphasis(std::optional<std::size_t> stack_bottom);

// ── utilities ─────────────────────────────────────────────────────────────

// Collapse Unicode whitespace runs to a single space and apply Unicode simple
// case folding for link reference label matching (spec §4.7).
//
// Implementation: uses an embedded ~1400-entry lookup table derived from
// Unicode CaseFolding.txt (the same approach as cmark). The table maps each
// code point with a non-trivial simple fold to its folded code point(s).
// ASCII letters are handled by a fast path before the table lookup.
// This covers all CommonMark spec test cases without any external dependency.
// See §9.5 (option B) for the rationale.
static std::string InlineParser::normaliseLabel(std::string_view label);

// Allocate a new InlineNode of the given type with default-initialised fields.
std::unique_ptr<InlineNode> InlineParser::makeNode(InlineType type);
```

`normaliseLabel` applies Unicode simple case folding via an embedded lookup table (option B from [§9.5](09_open_decisions.md#95-unicode-case-folding-for-link-reference-labels)).

The `processEmphasis` / bracket deactivation interaction — `delim_top` as a stack-size snapshot, deactivation on failed brackets, and `stack_bottom` on valid links — is specified in [§9.4](09_open_decisions.md#94-processemphasis--bracket-deactivation-interaction).

Link reference definitions resolved via `ref_map_` are populated by [`SpineHandler`](05_spine_handler.md#51-state) during phase 1 and stored as [`LinkDef`](02_data_types.md#22-blockdata-union) values.

---

← [5. SpineHandler — phase 1](05_spine_handler.md) | [Index](index.md) | Next: [7. Tab algorithm](07_tab_algorithm.md) →
