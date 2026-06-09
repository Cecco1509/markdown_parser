#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace entities {

// Try to decode a single HTML entity reference starting at input[pos] (which
// must be '&'). On success returns the decoded UTF-8 string and advances pos
// past the closing ';'. On failure returns an empty string and leaves pos
// unchanged.
std::string decode(std::string_view input, std::size_t &pos);

// Decode all entity references in a string (used for link destinations/titles).
std::string decodeAll(std::string_view input);

} // namespace entities
