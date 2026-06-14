#include "markdown_parser/string_utils.hpp"
#include "markdown_parser/entities.hpp"

namespace markdown_parser {
namespace string_utils {

std::string processEscapesAndEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t j = 0; j < s.size(); ) {
    char ch = s[j];
    if (ch == '\\' && j + 1 < s.size()) {
      auto u = static_cast<unsigned char>(s[j + 1]);
      bool is_punct = (u >= '!' && u <= '/') || (u >= ':' && u <= '@') ||
                      (u >= '[' && u <= '`') || (u >= '{' && u <= '~');
      if (is_punct) {
        out += s[j + 1];
        j += 2;
        continue;
      }
    }
    if (ch == '&') {
      std::string dec = entities::decode(s, j);
      if (!dec.empty()) {
        out += dec;
        continue;
      }
    }
    out += ch;
    ++j;
  }
  return out;
}

} // namespace string_utils
} // namespace markdown_parser
