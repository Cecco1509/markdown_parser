#pragma once

#include <string_view>

namespace string_utils {

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

} // namespace string_utils
