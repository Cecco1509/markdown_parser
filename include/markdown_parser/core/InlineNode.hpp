#pragma once

#include "markdown_parser/core/Types.hpp"
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

    // Height of the subtree rooted here (1 = leaf). Maintained as the tree is
    // built so InlineParser can enforce commonmark::kMaxNesting in O(1) per
    // node instead of re-walking the subtree at every wrap.
    int depth = 1;
};

struct Delimiter {
    char        ch;
    int         num;
    int         orig_num;  // length at scan time, for sum-rule
    bool        can_open;
    bool        can_close;
    std::size_t node_idx = 0; // index into InlineParser::nodes_
};

struct BracketEntry {
    bool        is_image;
    std::size_t node_idx  = 0;    // index into InlineParser::nodes_
    bool        active    = false; // false when deactivated (nested link rule)
    std::size_t delim_top = 0;
    std::size_t src_pos   = 0; // input position right after the opening '['
};

} // namespace markdown_parser
