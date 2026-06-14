#pragma once

#include "Types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstddef>

namespace markdown_parser {

struct InlineNode {
    InlineType   type;
    std::string  literal;
    InlineData   data;
    std::vector<std::unique_ptr<InlineNode>> children;
};

struct Delimiter {
    char        ch;
    int         num;
    int         orig_num;  // length at scan time, for sum-rule
    bool        can_open;
    bool        can_close;
    InlineNode* node = nullptr;
};

struct BracketEntry {
    bool        is_image;
    InlineNode* node      = nullptr;
    std::size_t delim_top = 0;
    std::size_t src_pos   = 0; // input position right after the opening '['
};

} // namespace markdown_parser
