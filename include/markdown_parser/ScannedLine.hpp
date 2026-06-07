#pragma once

#include "commonmark_constants.hpp"
#include "string_utils.hpp"
#include <cstddef>
#include <string_view>
#include <tuple>

class ScannedLine {
public:
  // ── Factory ───────────────────────────────────────────────────────────────

  static ScannedLine from(std::string_view raw) {
    std::string_view content = string_utils::stripLineEnding(raw);
    auto [virtual_indent, next_non_space] = computeVirtualIndent(content, 0);
    return ScannedLine{content, 0, virtual_indent, next_non_space,
                       string_utils::isBlank(content), 0};
  }

  // ── Transformation ────────────────────────────────────────────────────────

  // Consume n_cols visual columns and return the remaining ScannedLine.
  // Handles full and partial tab consumption; prefix_spaces carries any
  // remainder of a partially consumed tab.
  ScannedLine consume(std::size_t n_cols) const {
    std::size_t byte_offset = 0;
    std::size_t col         = base_col_;
    std::size_t cols_needed = n_cols;
    std::size_t new_prefix  = 0;

    // Drain virtual spaces left from a previous partial tab.
    // The tab byte itself was already advanced past when this ScannedLine
    // was created, so prefix_spaces have no byte representation in content_.
    if (prefix_spaces_ > 0) {
      const std::size_t take = std::min(prefix_spaces_, cols_needed);
      cols_needed -= take;
      col         += take;
      new_prefix   = prefix_spaces_ - take;
    }

    while (cols_needed > 0 && byte_offset < content_.size()) {
      const unsigned char byte =
          static_cast<unsigned char>(content_[byte_offset]);
      if (byte == ' ') {
        ++byte_offset;
        --cols_needed;
        ++col;
      } else if (byte == '\t') {
        const std::size_t tab_w = (col / commonmark::kTabStop + 1) * commonmark::kTabStop - col;
        if (tab_w <= cols_needed) {
          ++byte_offset;
          cols_needed -= tab_w;
          col         += tab_w;
        } else {
          // Partial tab: advance past the byte and record the remainder.
          ++byte_offset;
          new_prefix  = tab_w - cols_needed;
          col        += cols_needed;
          cols_needed = 0;
        }
      } else {
        ++byte_offset;
        --cols_needed;
        ++col;
      }
    }

    auto [vi, nns] = computeVirtualIndent(content_.substr(byte_offset), col);
    return ScannedLine{content_.substr(byte_offset), new_prefix,
                       vi + new_prefix, nns,
                       string_utils::isBlank(content_.substr(byte_offset)) && new_prefix == 0,
                       col};
  }

  // ── Getters ───────────────────────────────────────────────────────────────

  std::string_view content()       const noexcept { return content_; }
  std::size_t      indent()        const noexcept { return virtual_indent_; }
  std::size_t      next_non_space() const noexcept { return next_non_space_; }
  bool             is_blank()      const noexcept { return is_blank_; }
  std::size_t      base_col()      const noexcept { return base_col_; }
  std::size_t      prefix_spaces() const noexcept { return prefix_spaces_; }

private:
  std::string_view content_;
  std::size_t      prefix_spaces_;
  std::size_t      virtual_indent_; // includes prefix_spaces
  std::size_t      next_non_space_;
  bool             is_blank_;
  std::size_t      base_col_;

  ScannedLine(std::string_view content, std::size_t prefix_spaces,
              std::size_t virtual_indent, std::size_t next_non_space,
              bool is_blank, std::size_t base_col)
      : content_(content), prefix_spaces_(prefix_spaces),
        virtual_indent_(virtual_indent), next_non_space_(next_non_space),
        is_blank_(is_blank), base_col_(base_col) {}

  // Returns {virtual_indent, next_non_space} for the leading whitespace of
  // content starting at absolute column base_col.
  static std::pair<std::size_t, std::size_t>
  computeVirtualIndent(std::string_view content,
                       std::size_t base_col) noexcept {
    std::size_t col = base_col;
    std::size_t i   = 0;
    while (i < content.size()) {
      const unsigned char c = static_cast<unsigned char>(content[i]);
      if (c == ' ')       { ++col; ++i; }
      else if (c == '\t') { col = (col / commonmark::kTabStop + 1) * commonmark::kTabStop; ++i; }
      else                { break; }
    }
    return {col - base_col, i};
  }
};
