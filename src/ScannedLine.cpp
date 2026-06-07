#include "markdown_parser/ScannedLine.hpp"
#include "markdown_parser/commonmark_constants.hpp"
#include "markdown_parser/string_utils.hpp"

ScannedLine ScannedLine::from(std::string_view raw, bool strip_bom) {
  std::string norm = string_utils::line_init(raw, strip_bom);
  std::string_view full = string_utils::stripLineEnding(norm);
  // Compute offset before moving norm — SSO may relocate the buffer on move.
  const std::size_t offset =
      static_cast<std::size_t>(full.data() - norm.data());
  const std::size_t len = full.size();
  auto [vi, nns] = computeVirtualIndent(full, 0);
  ScannedLine sl{full, 0, vi, nns, string_utils::isBlank(full), 0};
  sl.owned_   = std::move(norm);
  sl.content_ = std::string_view(sl.owned_.data() + offset, len);
  return sl;
}

void ScannedLine::consume(std::size_t n_cols) {
  std::size_t byte_offset = 0;
  std::size_t col         = base_col_;
  std::size_t cols_needed = n_cols;
  std::size_t new_prefix  = 0;

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
      ++byte_offset; --cols_needed; ++col;
    } else if (byte == '\t') {
      const std::size_t tab_w =
          (col / commonmark::kTabStop + 1) * commonmark::kTabStop - col;
      if (tab_w <= cols_needed) {
        ++byte_offset; cols_needed -= tab_w; col += tab_w;
      } else {
        ++byte_offset;
        new_prefix  = tab_w - cols_needed;
        col        += cols_needed;
        cols_needed = 0;
      }
    } else {
      ++byte_offset; --cols_needed; ++col;
    }
  }

  content_        = content_.substr(byte_offset);
  base_col_       = col;
  prefix_spaces_  = new_prefix;
  auto [vi, nns]  = computeVirtualIndent(content_, col);
  virtual_indent_ = vi + new_prefix;
  next_non_space_ = nns;
  is_blank_       = string_utils::isBlank(content_) && new_prefix == 0;
}

ScannedLine::ScannedLine(std::string_view content, std::size_t prefix_spaces,
                         std::size_t virtual_indent, std::size_t next_non_space,
                         bool is_blank, std::size_t base_col)
    : content_(content), prefix_spaces_(prefix_spaces),
      virtual_indent_(virtual_indent), next_non_space_(next_non_space),
      is_blank_(is_blank), base_col_(base_col) {}

std::pair<std::size_t, std::size_t>
ScannedLine::computeVirtualIndent(std::string_view content,
                                  std::size_t base_col) noexcept {
  std::size_t col = base_col;
  std::size_t i   = 0;
  while (i < content.size()) {
    const unsigned char c = static_cast<unsigned char>(content[i]);
    if (c == ' ')       { ++col; ++i; }
    else if (c == '\t') {
      col = (col / commonmark::kTabStop + 1) * commonmark::kTabStop;
      ++i;
    }
    else { break; }
  }
  return {col - base_col, i};
}
