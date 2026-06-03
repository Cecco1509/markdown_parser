#pragma once

#include "Types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstddef>

struct InlineNode {
    InlineType   type;
    std::string  literal;
    InlineData   data;
    std::vector<std::unique_ptr<InlineNode>> children;
};

struct Delimiter {
    char        ch;
    int         num;
    bool        can_open;
    bool        can_close;
    InlineNode* node = nullptr;
};

struct BracketEntry {
    bool        is_image;
    InlineNode* node      = nullptr;
    std::size_t delim_top = 0;
};
