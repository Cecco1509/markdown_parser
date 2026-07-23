#pragma once

#include <cstddef>

namespace markdown_parser {
namespace commonmark {

// Tab stops are every 4 columns (CommonMark spec §2.1).
constexpr std::size_t kTabStop = 4;

// An indented code block requires exactly this many columns of indentation.
constexpr std::size_t kCodeBlockIndent = 4;

// Any block-opening marker indented more than this is treated as indented code.
constexpr std::size_t kMaxBlockIndent = 3;

// Maximum block and inline nesting depth. The parser, the renderers and the
// node destructors all recurse once per level, so an unbounded tree would
// overflow the stack — a crash the standard gives no way to catch. The
// CommonMark spec sets no limit, but the deepest tree in its 652-example
// suite is 11 levels, so this leaves ample headroom for real documents.
// Anything deeper stays literal text rather than opening a new node.
constexpr std::size_t kMaxNesting = 100;

// ── Block-level spec limits ─────────────────────────────────────────────────

// ATX heading levels are 1–6 (spec §4.2): one to six leading '#'.
constexpr std::size_t kMinHeadingLevel = 1;
constexpr std::size_t kMaxHeadingLevel = 6;

// An ordered-list marker's number has at most 9 digits (spec §5.2).
constexpr std::size_t kMaxListDigits = 9;

// A fenced code block opens with a run of at least 3 ` or ~ (spec §4.5).
constexpr std::size_t kFenceMinRun = 3;

// A thematic break needs at least 3 of -, _, or * (spec §4.1).
constexpr std::size_t kThematicBreakMinChars = 3;

// A list marker followed by more than 4 spaces of content indentation is
// treated as a single space, the rest becoming indented code (spec §5.2).
// Numerically this is just the indented-code threshold.
constexpr std::size_t kMaxListMarkerSpaces = kCodeBlockIndent;

// ── Inline-level spec limits ────────────────────────────────────────────────

// A link label holds at most 999 characters between the brackets (spec §4.7);
// anything longer cannot be a valid label.
constexpr std::size_t kMaxLinkLabelLen = 999;

// An autolink URI scheme is 2–32 characters (spec §6.5).
constexpr std::size_t kMaxAutolinkSchemeLen = 32;

// Length of the "![CDATA[" prefix that opens a CDATA section (spec §6.6).
constexpr std::size_t kCDataPrefixLen = 8;

// (The numeric-character-reference digit limits live in utils/entities.cpp,
// since the entity decoder is a utils-layer component and does not depend on
// this parser-layer header.)

} // namespace commonmark
} // namespace markdown_parser
