#pragma once

#include "Types.hpp"
#include <string>
#include <vector>
#include <memory>

struct InlineNode; // forward declaration

struct BlockNode {
    NodeType  type;
    BlockData data;

    int  start_line = 0;
    int  end_line   = 0;
    int  start_col  = 0;

    bool is_open         = true;
    bool last_line_blank = false;

    std::string string_content;

    std::vector<std::unique_ptr<BlockNode>>  children;
    std::vector<std::unique_ptr<InlineNode>> inline_children;
};
