#pragma once

#include "ScannedLine.hpp"
#include <string_view>
#include <tuple>
#include <cstddef>

class PreScanner {
public:
    explicit PreScanner(bool debug = false) : debug_(debug) {}

    ScannedLine scan(std::string_view raw) const;
    ScannedLine scanWithOffset(std::string_view raw, std::size_t base_col) const;

private:
    bool debug_ = false;

    static std::tuple<std::size_t, std::size_t, std::size_t>
    computeVirtualIndent(std::string_view content, std::size_t base_col) noexcept;

    static std::string_view stripLineEnding(std::string_view raw) noexcept;
    static bool             isBlank(std::string_view content) noexcept;
};
