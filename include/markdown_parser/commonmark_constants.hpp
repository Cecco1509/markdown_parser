#pragma once

#include <cstddef>

namespace commonmark {

// Tab stops are every 4 columns (CommonMark spec §2.1).
constexpr std::size_t kTabStop = 4;

// An indented code block requires exactly this many columns of indentation.
constexpr std::size_t kCodeBlockIndent = 4;

// Any block-opening marker indented more than this is treated as indented code.
constexpr std::size_t kMaxBlockIndent = 3;

} // namespace commonmark
