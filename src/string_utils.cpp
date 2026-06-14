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

static bool isHrefSafe(unsigned char c) {
  // clang-format off
  static const bool safe[256] = {
    //       0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
    /* 0 */ false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    /* 1 */ false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    /* 2 */ false,true, false,true, true, true, false,false,true, true, true, true, true, true, true, true,
    //       sp    !     "     #     $     %     &     '     (     )     *     +     ,     -     .     /
    /* 3 */ true, true, true, true, true, true, true, true, true, true, true, true,false,true, false,true,
    //       0     1     2     3     4     5     6     7     8     9     :     ;     <     =     >     ?
    /* 4 */ true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    //       @     A     B     C     D     E     F     G     H     I     J     K     L     M     N     O
    /* 5 */ true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,true,
    //       P     Q     R     S     T     U     V     W     X     Y     Z     [     \     ]     ^     _
    /* 6 */ false,true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
    //       `     a     b     c     d     e     f     g     h     i     j     k     l     m     n     o
    /* 7 */ true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,false,
    //       p     q     r     s     t     u     v     w     x     y     z     {     |     }     ~     DEL
    // 0x80-0xFF: all false (non-ASCII must be percent-encoded)
  };
  // clang-format on
  return safe[c];
}

static const char kHex[] = "0123456789ABCDEF";

std::string escapeHtml(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&':  out += "&amp;";  break;
    case '<':  out += "&lt;";   break;
    case '>':  out += "&gt;";   break;
    case '"':  out += "&quot;"; break;
    default:   out += c;        break;
    }
  }
  return out;
}

std::string escapeUrl(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (unsigned char c : s) {
    if (c == '&')
      out += "&amp;";
    else if (isHrefSafe(c))
      out += static_cast<char>(c);
    else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

} // namespace string_utils
} // namespace markdown_parser
