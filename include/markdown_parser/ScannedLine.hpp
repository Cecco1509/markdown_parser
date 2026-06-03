#pragma once

#include <string_view>
#include <cstddef>

struct ScannedLine {
    std::string_view content;
    std::size_t      indent;
    std::size_t      virtual_indent;
    std::size_t      next_non_space;
    bool             is_blank;
};
