#pragma once

#include <string>
#include <string_view>

namespace markdown_parser {
namespace string_utils {

// Process backslash escapes (\X where X is ASCII punctuation → X) and HTML
// entity references (&name;, &#N;, &#xN;) in s. Used for fenced-code info
// strings and other block-level text that allows both transformations.
std::string processEscapesAndEntities(std::string_view s);

// Normalize a raw input line before scanning:
//   - If strip_bom is true, removes a leading UTF-8 BOM (U+FEFF, bytes EF BB
//   BF).
//   - Replaces every NUL byte (\0) with the UTF-8 encoding of U+FFFD (EF BF
//   BD).
// Returns an owned string; the caller must keep it alive as long as any
// string_view into it (e.g. ScannedLine::content()) is in use.
inline std::string line_init(std::string_view raw, bool strip_bom = false) {
  if (strip_bom && raw.size() >= 3 &&
      static_cast<unsigned char>(raw[0]) == 0xEF &&
      static_cast<unsigned char>(raw[1]) == 0xBB &&
      static_cast<unsigned char>(raw[2]) == 0xBF)
    raw.remove_prefix(3);

  std::string result;
  result.reserve(raw.size());
  for (char c : raw) {
    if (c == '\0')
      result += "\xEF\xBF\xBD"; // U+FFFD replacement character
    else
      result += c;
  }

  return result;
}

inline std::string_view stripLineEnding(std::string_view raw) noexcept {
  if (!raw.empty() && raw.back() == '\n') {
    raw.remove_suffix(1);
    if (!raw.empty() && raw.back() == '\r')
      raw.remove_suffix(1);
  } else if (!raw.empty() && raw.back() == '\r') {
    raw.remove_suffix(1);
  }
  return raw;
}

inline bool isBlank(std::string_view content) noexcept {
  for (unsigned char c : content)
    if (c != ' ' && c != '\t')
      return false;
  return true;
}

inline std::string_view trimRight(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  return s;
}


} // namespace string_utils
} // namespace markdown_parser
