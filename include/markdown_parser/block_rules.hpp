#pragma once

#include "BlockNode.hpp"
#include "ScannedLine.hpp"
#include <optional>
#include <string>

namespace block_rules {

// ── §3.1 Continuation ────────────────────────────────────────────────────────

struct ContinuationResult {
    bool        matched;
    std::size_t cols_to_consume = 0;
    // True for a fenced-code closing fence: block fails but the line must be
    // swallowed (not appended as content).
    bool        swallow_line    = false;
};

// current_col is the absolute column position already consumed by parent
// containers; used to compute the relative cols_to_consume for Item nodes.
// debug=true emits a one-line reason for each match/no-match to stderr.
ContinuationResult continuationMatches(const BlockNode& node, const ScannedLine& line,
                                       std::size_t current_col = 0,
                                       bool debug = false);

// Post-append check for HTML block types 1–5: returns true when `line_content`
// satisfies the type-specific end condition.  Call after step-3 appends the line.
bool htmlBlockEndMet(const BlockNode& node, std::string_view line_content);

// True when `line` is a setext underline (===... or ---..., indent 0–3).
// Used by SpineHandler::incorporatesLazyContinuation to suppress lazy continuation.
bool isSetextUnderline(const ScannedLine& line);

// ── §3.2 Open ────────────────────────────────────────────────────────────────

struct OpenResult {
    NodeType              type;
    BlockData             data;
    // Companion list descriptor when type == Item; used to open/reuse a List.
    std::optional<ListData> list_data;
    // Pre-parsed leaf content (ATX heading text).
    std::string           extracted_content;
    // True for single-line openers (ATX heading, ThematicBreak, fenced-code
    // opener): SpineHandler must not call appendText for this line.
    bool                  swallow_line  = false;
    // Columns consumed by the opener marker; SpineHandler calls consumeColumns
    // with this value after pushing the new block onto the spine.
    std::size_t           cols_consumed = 0;
};

// Returns the first matching opener in priority order (§3.2 steps 1–7),
// or std::nullopt when no opener fires.
//
// Does NOT return a Paragraph fallback — SpineHandler::step3AppendText opens
// a Paragraph implicitly when the tip is a container block.
//
// tip_is_paragraph     – enables paragraph-interruption constraints (§5.3, §4.6).
// inside_list_blank    – suppresses indented-code-block opener inside a list
//                        item that last saw a blank line (§3.2 point 7).
std::optional<OpenResult> tryOpen(const ScannedLine& line,
                                  bool tip_is_paragraph,
                                  bool inside_list_blank = false);

// ── §3.3 Close (finalization hook) ───────────────────────────────────────────

// Called by SpineHandler::closeBlock() before the node is moved to its parent.
// Handles type-specific cleanup:
//   - Indented code block: strip trailing blank lines.
//   - Setext heading: strip leading/trailing blank lines from string_content.
void onClose(BlockNode& node);

} // namespace block_rules
