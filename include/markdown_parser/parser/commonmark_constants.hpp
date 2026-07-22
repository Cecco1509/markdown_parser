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

} // namespace commonmark
} // namespace markdown_parser
