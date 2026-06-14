#pragma once

#include "markdown_parser/core/BlockNode.hpp"

#include <concepts>
#include <string>

namespace markdown_parser {

template <typename T>
concept Renderer = requires(T r, const BlockNode &node) {
    { r.render(node) } -> std::convertible_to<std::string>;
};

} // namespace markdown_parser
