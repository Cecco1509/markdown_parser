#pragma once

#include <string_view>
#include <cstddef>

struct ScannedLine {
    std::string_view content;
    std::size_t      indent;
    std::size_t      virtual_indent;
    std::size_t      next_non_space;
    bool             is_blank;
    std::size_t      base_col = 0; // absolute column where this scan started
};
