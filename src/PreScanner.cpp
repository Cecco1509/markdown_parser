#include "markdown_parser/PreScanner.hpp"

ScannedLine PreScanner::scan(std::string_view raw) const {
    return scanWithOffset(raw, 0);
}

ScannedLine PreScanner::scanWithOffset(std::string_view raw, std::size_t base_col) const {
    std::string_view content = stripLineEnding(raw);
    auto [indent, virtual_indent, next_non_space] = computeVirtualIndent(content, base_col);
    return ScannedLine{content, /*prefix_spaces=*/0, indent, virtual_indent, next_non_space, isBlank(content), base_col};

}

std::tuple<std::size_t, std::size_t, std::size_t>
PreScanner::computeVirtualIndent(std::string_view content, std::size_t base_col) noexcept {
    std::size_t indent        = 0;
    std::size_t col           = base_col;
    std::size_t i             = 0;

    while (i < content.size()) {
        unsigned char c = static_cast<unsigned char>(content[i]);
        if (c == ' ') {
            ++indent; ++col; ++i;
        } else if (c == '\t') {
            ++indent;
            col = (col / 4 + 1) * 4;
            ++i;
        } else {
            break;
        }
    }
    return {indent, col - base_col, i};
}

std::string_view PreScanner::stripLineEnding(std::string_view raw) noexcept {
    if (!raw.empty() && raw.back() == '\n') {
        raw.remove_suffix(1);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
    } else if (!raw.empty() && raw.back() == '\r') {
        raw.remove_suffix(1);
    }
    return raw;
}

bool PreScanner::isBlank(std::string_view content) noexcept {
    for (unsigned char c : content)
        if (c != ' ' && c != '\t') return false;
    return true;
}
