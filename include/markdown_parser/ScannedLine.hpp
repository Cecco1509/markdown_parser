#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

class ScannedLine {
public:
  static ScannedLine from(std::string_view raw, bool strip_bom = false);
  void consume(std::size_t n_cols);

  std::string_view content()        const noexcept { return content_; }
  std::size_t      indent()         const noexcept { return virtual_indent_; }
  std::size_t      next_non_space() const noexcept { return next_non_space_; }
  bool             is_blank()       const noexcept { return is_blank_; }
  std::size_t      base_col()       const noexcept { return base_col_; }
  std::size_t      prefix_spaces()  const noexcept { return prefix_spaces_; }

private:
  std::string      owned_;
  std::string_view content_;
  std::size_t      prefix_spaces_  = 0;
  std::size_t      virtual_indent_ = 0;
  std::size_t      next_non_space_ = 0;
  bool             is_blank_       = false;
  std::size_t      base_col_       = 0;

  ScannedLine(std::string_view content, std::size_t prefix_spaces,
              std::size_t virtual_indent, std::size_t next_non_space,
              bool is_blank, std::size_t base_col);

  static std::pair<std::size_t, std::size_t>
  computeVirtualIndent(std::string_view content, std::size_t base_col) noexcept;
};
